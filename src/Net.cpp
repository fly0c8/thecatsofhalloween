#include "Net.h"

#include <cstdlib>
#include <cstring>
#include <deque>
#include <random>
#include <string>

// ============================================================================
// Web build: the browser does the websocket work for us
// ============================================================================
#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

// Older Emscripten releases (e.g. Ubuntu's 3.1.6) lack this GLFW 3.3 function that raylib 5.5 calls.
// The game's canvas has a fixed pixel size, so a no-op fallback is fine. Weak, so a real one wins.
extern "C" __attribute__((weak)) void* glfwSetWindowContentScaleCallback(void*, void*) { return nullptr; }

struct NetClient::Impl {
    EMSCRIPTEN_WEBSOCKET_T ws = 0;
    Status status = Status::Idle;
    std::string error;
    std::deque<std::string> inbox;
};

static EM_BOOL OnOpen(int, const EmscriptenWebSocketOpenEvent*, void* user)
{
    static_cast<NetClient::Impl*>(user)->status = NetClient::Status::Open;
    return EM_TRUE;
}
static EM_BOOL OnError(int, const EmscriptenWebSocketErrorEvent*, void* user)
{
    auto* d = static_cast<NetClient::Impl*>(user);
    if (d->status == NetClient::Status::Connecting) { d->status = NetClient::Status::Failed; d->error = "CANNOT REACH SERVER"; }
    return EM_TRUE;
}
static EM_BOOL OnClose(int, const EmscriptenWebSocketCloseEvent*, void* user)
{
    auto* d = static_cast<NetClient::Impl*>(user);
    if (d->status == NetClient::Status::Connecting) { d->status = NetClient::Status::Failed; d->error = "CANNOT REACH SERVER"; }
    else d->status = NetClient::Status::Closed;
    return EM_TRUE;
}
static EM_BOOL OnMessage(int, const EmscriptenWebSocketMessageEvent* e, void* user)
{
    if (e->isText) static_cast<NetClient::Impl*>(user)->inbox.emplace_back((const char*)e->data);
    return EM_TRUE;
}

NetClient::NetClient() : impl_(std::make_unique<Impl>()) {}
NetClient::~NetClient() { Close(); }

void NetClient::Connect(const std::string& url)
{
    Close();
    impl_->inbox.clear();
    impl_->error.clear();
    if (!emscripten_websocket_is_supported()) { impl_->status = Status::Failed; impl_->error = "NO WEBSOCKETS IN THIS BROWSER"; return; }
    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = url.c_str();
    impl_->ws = emscripten_websocket_new(&attr);
    if (impl_->ws <= 0) { impl_->status = Status::Failed; impl_->error = "BAD SERVER ADDRESS"; return; }
    impl_->status = Status::Connecting;
    emscripten_websocket_set_onopen_callback(impl_->ws, impl_.get(), OnOpen);
    emscripten_websocket_set_onerror_callback(impl_->ws, impl_.get(), OnError);
    emscripten_websocket_set_onclose_callback(impl_->ws, impl_.get(), OnClose);
    emscripten_websocket_set_onmessage_callback(impl_->ws, impl_.get(), OnMessage);
}

void NetClient::Close()
{
    if (impl_->ws > 0) {
        emscripten_websocket_close(impl_->ws, 1000, "bye");
        emscripten_websocket_delete(impl_->ws);
        impl_->ws = 0;
    }
    if (impl_->status == Status::Open || impl_->status == Status::Connecting) impl_->status = Status::Closed;
}

void NetClient::Send(const std::string& text)
{
    if (impl_->status == Status::Open) emscripten_websocket_send_utf8_text(impl_->ws, text.c_str());
}

bool NetClient::Poll(std::string& message)
{
    if (impl_->inbox.empty()) return false;
    message = std::move(impl_->inbox.front());
    impl_->inbox.pop_front();
    return true;
}

NetClient::Status NetClient::GetStatus() const { return impl_->status; }
std::string NetClient::Error() const { return impl_->error; }

std::string DefaultServerUrl(int, char**)
{
    const char* u = emscripten_run_script_string(
        "(location.host ? ((location.protocol === 'https:' ? 'wss://' : 'ws://') + location.host + '/ws') : 'ws://localhost:8080/ws')");
    return u ? u : "ws://localhost:8080/ws";
}

// ============================================================================
// Desktop build: a small RFC 6455 client on top of plain sockets
// ============================================================================
#else

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using sock_t = SOCKET;
static const sock_t BAD_SOCK = INVALID_SOCKET;
static void CloseSock(sock_t s) { closesocket(s); }
static bool WouldBlock() { return WSAGetLastError() == WSAEWOULDBLOCK; }
static void SetNonBlocking(sock_t s) { u_long on = 1; ioctlsocket(s, FIONBIO, &on); }
static void SetRecvTimeout(sock_t s, int ms) { DWORD t = (DWORD)ms; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&t, sizeof t); }
static void NetStartup() { static std::once_flag once; std::call_once(once, [] { WSADATA w; WSAStartup(MAKEWORD(2, 2), &w); }); }
#else
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
using sock_t = int;
static const sock_t BAD_SOCK = -1;
static void CloseSock(sock_t s) { ::shutdown(s, SHUT_RDWR); ::close(s); }
static bool WouldBlock() { return errno == EAGAIN || errno == EWOULDBLOCK; }
static void SetNonBlocking(sock_t s) { fcntl(s, F_SETFL, fcntl(s, F_GETFL, 0) | O_NONBLOCK); }
static void SetRecvTimeout(sock_t s, int ms) { timeval tv{ ms / 1000, (ms % 1000) * 1000 }; setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); }
static void NetStartup() {}
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#endif

struct NetClient::Impl {
    std::atomic<Status> status{ Status::Idle };
    std::atomic<sock_t> sock{ BAD_SOCK };
    std::thread worker;
    mutable std::mutex errMu;
    std::string error;

    std::string host, port, path;
    std::string rbuf;                  // received bytes not yet parsed (main thread once Open)
    std::string fragment;              // continuation frames
    std::deque<std::string> inbox;
    std::mt19937 rng{ std::random_device{}() };

    void Fail(const std::string& why)
    {
        { std::lock_guard<std::mutex> l(errMu); error = why; }
        status = Status::Failed;
    }

    bool SendAll(const char* data, size_t len)
    {
        const sock_t s = sock;
        size_t sent = 0;
        int spins = 0;
        while (sent < len) {
#if defined(_WIN32)
            const int n = ::send(s, data + sent, (int)(len - sent), 0);
#else
            const ssize_t n = ::send(s, data + sent, len - sent, MSG_NOSIGNAL);
#endif
            if (n > 0) { sent += (size_t)n; continue; }
            if (n < 0 && WouldBlock() && ++spins < 2000) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            return false;
        }
        return true;
    }

    void SendFrame(int opcode, const std::string& payload)
    {
        std::string f;
        f.push_back((char)(0x80 | opcode));
        const size_t n = payload.size();
        if (n < 126) f.push_back((char)(0x80 | n));
        else if (n < 65536) { f.push_back((char)(0x80 | 126)); f.push_back((char)(n >> 8)); f.push_back((char)(n & 255)); }
        else { f.push_back((char)(0x80 | 127)); for (int i = 7; i >= 0; --i) f.push_back((char)((uint64_t)n >> (i * 8))); }
        unsigned char mask[4];
        for (auto& m : mask) m = (unsigned char)(rng() & 255);
        f.append((const char*)mask, 4);
        for (size_t i = 0; i < n; ++i) f.push_back((char)(payload[i] ^ mask[i & 3]));   // clients must mask
        if (!SendAll(f.data(), f.size())) status = Status::Closed;
    }

    std::string MakeKey()
    {
        static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        unsigned char raw[16];
        for (auto& b : raw) b = (unsigned char)(rng() & 255);
        std::string out;
        for (int i = 0; i < 16; i += 3) {
            const unsigned v = (raw[i] << 16) | ((i + 1 < 16 ? raw[i + 1] : 0) << 8) | (i + 2 < 16 ? raw[i + 2] : 0);
            out += b64[(v >> 18) & 63];
            out += b64[(v >> 12) & 63];
            out += (i + 1 < 16) ? b64[(v >> 6) & 63] : '=';
            out += (i + 2 < 16) ? b64[v & 63] : '=';
        }
        return out;
    }

    // close s unless Close() on the main thread already took it
    void Drop(sock_t s)
    {
        sock_t expected = s;
        if (sock.compare_exchange_strong(expected, BAD_SOCK)) CloseSock(s);
    }

    // runs on the worker thread
    void DoConnect()
    {
        NetStartup();
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        addrinfo* res = nullptr;
        if (getaddrinfo(host.c_str(), port.c_str(), &hints, &res) != 0 || !res) { Fail("UNKNOWN HOST " + host); return; }
        sock_t s = BAD_SOCK;
        for (addrinfo* a = res; a; a = a->ai_next) {
            s = ::socket(a->ai_family, a->ai_socktype, a->ai_protocol);
            if (s == BAD_SOCK) continue;
            sock = s;
            if (::connect(s, a->ai_addr, (int)a->ai_addrlen) == 0) break;
            Drop(s);
            s = BAD_SOCK;
            if (status != Status::Connecting) break;   // cancelled
        }
        freeaddrinfo(res);
        if (status != Status::Connecting) return;
        if (s == BAD_SOCK) { Fail("NO SERVER AT " + host + ":" + port); return; }

        int one = 1;
        setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof one);
        SetRecvTimeout(s, 5000);

        const std::string req =
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + ":" + port + "\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Key: " + MakeKey() + "\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n";
        if (!SendAll(req.data(), req.size())) { Drop(s); Fail("HANDSHAKE FAILED"); return; }

        std::string resp;
        char buf[1024];
        while (resp.find("\r\n\r\n") == std::string::npos && resp.size() < 8192) {
            const int n = (int)::recv(s, buf, sizeof buf, 0);
            if (n <= 0) { Drop(s); if (status == Status::Connecting) Fail("HANDSHAKE FAILED"); return; }
            resp.append(buf, (size_t)n);
        }
        const size_t end = resp.find("\r\n\r\n");
        if (end == std::string::npos || resp.compare(0, 12, "HTTP/1.1 101") != 0) { Drop(s); Fail("NOT A CATS SERVER"); return; }
        rbuf = resp.substr(end + 4);
        SetNonBlocking(s);
        Status expect = Status::Connecting;
        status.compare_exchange_strong(expect, Status::Open);   // unless cancelled meanwhile
    }

    void ReadAvailable()
    {
        char buf[4096];
        for (;;) {
            const int n = (int)::recv(sock.load(), buf, sizeof buf, 0);
            if (n > 0) { rbuf.append(buf, (size_t)n); continue; }
            if (n < 0 && WouldBlock()) break;
            status = Status::Closed;   // 0 = orderly close, <0 = error
            break;
        }
        ParseFrames();
    }

    void ParseFrames()
    {
        for (;;) {
            if (rbuf.size() < 2) return;
            const unsigned char b0 = (unsigned char)rbuf[0], b1 = (unsigned char)rbuf[1];
            const bool fin = b0 & 0x80;
            const int op = b0 & 0x0F;
            const bool masked = b1 & 0x80;
            uint64_t len = b1 & 0x7F;
            size_t hdr = 2;
            if (len == 126) {
                if (rbuf.size() < 4) return;
                len = ((unsigned char)rbuf[2] << 8) | (unsigned char)rbuf[3];
                hdr = 4;
            } else if (len == 127) {
                if (rbuf.size() < 10) return;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | (unsigned char)rbuf[2 + i];
                hdr = 10;
            }
            const size_t maskAt = hdr;
            if (masked) hdr += 4;
            if (rbuf.size() < hdr + len) return;
            std::string payload = rbuf.substr(hdr, (size_t)len);
            if (masked)
                for (size_t i = 0; i < payload.size(); ++i) payload[i] ^= rbuf[maskAt + (i & 3)];
            rbuf.erase(0, hdr + (size_t)len);

            switch (op) {
            case 0x0:   // continuation
                fragment += payload;
                if (fin) { inbox.push_back(std::move(fragment)); fragment.clear(); }
                break;
            case 0x1: case 0x2:
                if (fin) inbox.push_back(std::move(payload));
                else fragment = std::move(payload);
                break;
            case 0x8:   // close
                SendFrame(0x8, "");
                status = Status::Closed;
                return;
            case 0x9:   // ping -> pong
                SendFrame(0xA, payload);
                break;
            default: break;
            }
        }
    }
};

NetClient::NetClient() : impl_(std::make_unique<Impl>()) {}
NetClient::~NetClient() { Close(); }

void NetClient::Connect(const std::string& url)
{
    Close();
    Impl& d = *impl_;
    d.rbuf.clear(); d.fragment.clear(); d.inbox.clear();
    { std::lock_guard<std::mutex> l(d.errMu); d.error.clear(); }

    // ws://host[:port][/path]
    std::string rest = url;
    if (rest.rfind("ws://", 0) == 0) rest = rest.substr(5);
    else if (rest.rfind("wss://", 0) == 0) { d.Fail("WSS NEEDS THE WEB BUILD - USE ws://"); return; }
    const size_t slash = rest.find('/');
    d.path = slash == std::string::npos ? "/ws" : rest.substr(slash);
    std::string hostport = rest.substr(0, slash);
    if (!hostport.empty() && hostport[0] == '[') {            // [IPv6]:port
        const size_t rb = hostport.find(']');
        d.host = hostport.substr(1, rb == std::string::npos ? std::string::npos : rb - 1);
        d.port = (rb != std::string::npos && rb + 1 < hostport.size() && hostport[rb + 1] == ':') ? hostport.substr(rb + 2) : "80";
    } else {
        const size_t colon = hostport.find(':');
        d.host = hostport.substr(0, colon);
        d.port = colon == std::string::npos ? "80" : hostport.substr(colon + 1);
    }
    if (d.host.empty()) { d.Fail("BAD SERVER ADDRESS"); return; }

    d.status = Status::Connecting;
    d.worker = std::thread([&d] { d.DoConnect(); });
}

void NetClient::Close()
{
    Impl& d = *impl_;
    const bool wasOpen = d.status == Status::Open;
    if (d.status == Status::Connecting || wasOpen) d.status = Status::Closed;
    if (wasOpen) d.SendFrame(0x8, "");
    const sock_t s = d.sock.exchange(BAD_SOCK);
    if (s != BAD_SOCK) CloseSock(s);       // also unblocks a pending connect/recv in the worker
    if (d.worker.joinable()) d.worker.join();
}

void NetClient::Send(const std::string& text)
{
    if (impl_->status == Status::Open) impl_->SendFrame(0x1, text);
}

bool NetClient::Poll(std::string& message)
{
    Impl& d = *impl_;
    if (d.status == Status::Open) d.ReadAvailable();
    if (d.inbox.empty()) return false;
    message = std::move(d.inbox.front());
    d.inbox.pop_front();
    return true;
}

NetClient::Status NetClient::GetStatus() const { return impl_->status; }

std::string NetClient::Error() const
{
    std::lock_guard<std::mutex> l(impl_->errMu);
    return impl_->error;
}

std::string DefaultServerUrl(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)                 // first argument that isn't an option like --fullscreen
        if (argv[i] && argv[i][0] && argv[i][0] != '-') return argv[i];
#if defined(_WIN32)
    char* env = nullptr;
    size_t len = 0;
    if (_dupenv_s(&env, &len, "CATS_SERVER") == 0 && env) {
        std::string v = env;
        free(env);
        if (!v.empty()) return v;
    }
#else
    if (const char* env = std::getenv("CATS_SERVER")) if (env[0]) return env;
#endif
    return "ws://localhost:8080/ws";
}

#endif
