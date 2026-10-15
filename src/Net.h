// Net.h - minimal websocket client for the game.
//  * Desktop: plain sockets (Winsock / BSD), RFC 6455 framing, ws:// only.
//  * Web (Emscripten): the browser's WebSocket via <emscripten/websocket.h>.
// Deliberately does NOT include any OS headers, so it can sit next to raylib.h.
#pragma once
#include <memory>
#include <string>

class NetClient {
public:
    enum class Status { Idle, Connecting, Open, Failed, Closed };

    NetClient();
    ~NetClient();
    NetClient(const NetClient&) = delete;
    NetClient& operator=(const NetClient&) = delete;

    void Connect(const std::string& url);   // returns immediately; watch GetStatus()
    void Close();
    void Send(const std::string& text);     // one text frame
    bool Poll(std::string& message);        // next received text message, if any

    Status GetStatus() const;
    std::string Error() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
};

// Server address: first command line argument that isn't an --option, else env CATS_SERVER, else
// ws://localhost:8080/ws. In the browser: the host the page was loaded from.
std::string DefaultServerUrl(int argc, char** argv);
