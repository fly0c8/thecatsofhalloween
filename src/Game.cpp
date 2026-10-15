#include "Game.h"
#include "Amiga.h"
#include "Input.h"
#include "Music.h"
#include "Rumble.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <sstream>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

using namespace amiga;

static constexpr float STEP = 1.0f / 60.0f;
static constexpr int HUD_H = 24;
static constexpr float SEND_INTERVAL = 1.0f / 30.0f;   // our cat's state, 30 times a second
static constexpr float CONNECT_TIMEOUT = 8.0f;

static const char* HIT_WORDS[] = { "SCRATCH!", "HISS!", "POW!", "MRRROW!", "FFFT!", "WHACK!", "YOWL!" };

// ----------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------
void Game::Init(const std::string& serverUrl)
{
    serverUrl_ = serverUrl;
    screen_ = LoadRenderTexture(SCREEN_W, SCREEN_H);
    view_ = LoadRenderTexture(SCREEN_W, VIEW_H);
    SetTextureFilter(screen_.texture, TEXTURE_FILTER_POINT);

    world_.Generate(0x0CA75u);   // replaced by the server's seed on WELCOME
    world_.Load();
    bg_.Load();
    sheets_[0] = BuildCatSheet(0);
    sheets_[1] = BuildCatSheet(1);
    BuildLogo();

    cats_[0].id = 0; cats_[0].name = "MORTIMER"; cats_[0].color = RGB12(0xCF4);
    cats_[1].id = 1; cats_[1].name = "GINGER";   cats_[1].color = RGB12(0xF92);
    state_ = State::Title;
}

void Game::Shutdown()
{
    rumble::Stop();
    net_.Close();
    UnloadRenderTexture(screen_);
    if (upscale_) UnloadRenderTexture(upscaled_);
    UnloadRenderTexture(view_);
    if (logo_.id) UnloadTexture(logo_);
    world_.Unload();
    bg_.Unload();
    UnloadCatSheet(sheets_[0]);
    UnloadCatSheet(sheets_[1]);
}

void Game::BuildLogo()
{
    // Chunky logo from the built-in 8-bit font, with a copper gradient, outline and drop shadow
    auto line = [](const char* text, int size, Color a, Color b, Color c) {
        Image t = ImageText(text, size, WHITE);
        Image out = GenImageColor(t.width + 4, t.height + 4, BLANK);
        Color* src = LoadImageColors(t);
        for (int pass = 0; pass < 3; ++pass)
            for (int y = 0; y < t.height; ++y)
                for (int x = 0; x < t.width; ++x) {
                    if (src[y * t.width + x].a < 128) continue;
                    if (pass == 0) ImageDrawPixel(&out, x + 3, y + 3, RGB12(0x203));         // shadow
                    if (pass == 1)                                                           // outline
                        for (int oy = -1; oy <= 1; ++oy)
                            for (int ox = -1; ox <= 1; ++ox) ImageDrawPixel(&out, x + 1 + ox, y + 1 + oy, RGB12(0x000));
                    if (pass == 2) {
                        const float k = (float)y / (t.height - 1);
                        const Color col = k < 0.5f ? Lerp12(a, b, k * 2) : Lerp12(b, c, (k - 0.5f) * 2);
                        ImageDrawPixel(&out, x + 1, y + 1, col);
                    }
                }
        UnloadImageColors(src);
        UnloadImage(t);
        return out;
    };
    Image l1 = line("THE CATS", 30, RGB12(0xFFA), RGB12(0xFA2), RGB12(0xC40));
    Image l2 = line("OF HALLOWEEN", 20, RGB12(0xEAF), RGB12(0xB5E), RGB12(0x629));
    const int w = std::max(l1.width, l2.width);
    Image logo = GenImageColor(w, l1.height + l2.height, BLANK);
    ImageDraw(&logo, l1, Rectangle{ 0, 0, (float)l1.width, (float)l1.height },
              Rectangle{ (float)(w - l1.width) / 2, 0, (float)l1.width, (float)l1.height }, WHITE);
    ImageDraw(&logo, l2, Rectangle{ 0, 0, (float)l2.width, (float)l2.height },
              Rectangle{ (float)(w - l2.width) / 2, (float)l1.height, (float)l2.width, (float)l2.height }, WHITE);
    logo_ = LoadTextureFromImage(logo);
    UnloadImage(l1);
    UnloadImage(l2);
    UnloadImage(logo);
}

// ----------------------------------------------------------------------------
// Networking
// ----------------------------------------------------------------------------
void Game::StartConnect()
{
    netMessage_.clear();
    net_.Connect(serverUrl_);
    state_ = State::Connecting;
    stateTime_ = 0;
}

void Game::LeaveGame(const std::string& why)
{
    net_.Close();
    rumble::Stop();
    netMessage_ = why;
    rivalHere_ = false;
    paused_ = false;
    state_ = State::Title;
    stateTime_ = 0;
    music::Restart();
}

void Game::PumpNetwork()
{
    std::string msg;
    while (net_.Poll(msg)) HandleMessage(msg);

    const NetClient::Status st = net_.GetStatus();
    if (state_ == State::Connecting) {
        if (st == NetClient::Status::Failed) LeaveGame(net_.Error());
        else if (st == NetClient::Status::Closed) LeaveGame("THE SERVER CLOSED THE CONNECTION");
        else if (stateTime_ > CONNECT_TIMEOUT) LeaveGame("THE SERVER DID NOT ANSWER");
    } else if (state_ == State::Play || state_ == State::MatchOver) {
        if (st != NetClient::Status::Open) LeaveGame("CONNECTION TO THE SERVER LOST");
    }
}

void Game::HandleMessage(const std::string& msg)
{
    std::istringstream in(msg);
    std::string cmd;
    in >> cmd;
    auto validId = [](int id) { return id == 0 || id == 1; };

    if (cmd == "WELCOME") {
        unsigned long seed = 0x0CA75;
        in >> myId_ >> seed;
        if (!validId(myId_)) { LeaveGame("CONFUSING ANSWER FROM THE SERVER"); return; }
        world_.Generate((uint32_t)seed);
        candyPending_.assign(world_.Candies().size(), 0.0f);
        rivalHere_ = false;
        wins_[0] = wins_[1] = 0;
        StartRound();
        state_ = State::Play;
        stateTime_ = 0;
        music::Play(Sfx::Start);
        Banner(std::string("YOU ARE ") + Me().name + "! FIND YOUR RIVAL!", 3.0f);
    } else if (cmd == "FULL") {
        LeaveGame("SERVER IS FULL - TWO CATS ARE ALREADY FIGHTING");
    } else if (cmd == "JOIN") {
        int id = -1;
        in >> id;
        if (!validId(id) || id == myId_) return;
        rivalHere_ = true;
        Rival().netHasState = false;
        Rival().Reset(world_.SpawnPoint(id));
        Rival().candy = 0;
        music::Play(Sfx::Start);
        Banner(std::string(cats_[id].name) + " JOINED THE HUNT!", 2.5f);
    } else if (cmd == "LEAVE") {
        int id = -1;
        in >> id;
        if (!validId(id) || id == myId_) return;
        rivalHere_ = false;
        Banner(std::string(cats_[id].name) + " RAN AWAY!", 2.5f);
    } else if (cmd == "STATE") {
        int id = -1;
        in >> id;
        if (!validId(id) || id == myId_) return;
        std::string payload;
        std::getline(in, payload);
        if (Rival().ApplyRemote(payload.c_str())) rivalHere_ = true;
    } else if (cmd == "HIT") {
        int attacker = -1, victim = -1;
        float fromX = 0;
        in >> attacker >> victim >> fromX;
        if (victim != myId_ || Me().ko) return;
        Cat& me = Me();
        const bool canHurt = me.invuln <= 0;
        me.Hurt(fromX, 1);
        if (!canHurt) return;
        music::Play(Sfx::Hit);
        const Vector2 hp{ me.pos.x, me.pos.y - 12 };
        Burst(hp, 10, pal::White, 110, true, 0.4f);
        Burst(hp, 6, myId_ == 0 ? RGB12(0x335) : RGB12(0xF92), 80, true, 0.7f);   // tufts of fur
        Pop(Vector2{ me.pos.x, me.pos.y - 26 }, "OUCH!", pal::Red);
        shake_ = 0.25f;
        rumble::Pulse(0.8f, 0.6f, 0.35f);
        if (me.ko) { net_.Send("KO WAS SCRATCHED SILLY"); rumble::Pulse(1.0f, 1.0f, 0.8f); }
    } else if (cmd == "CANDY") {
        int idx = -1, taker = -1;
        in >> idx >> taker;
        auto& candies = world_.Candies();
        if (idx < 0 || idx >= (int)candies.size()) return;
        candies[idx].taken = true;
        if (taker == myId_) {
            Cat& me = Me();
            me.candy++;
            music::Play(Sfx::Candy);
            Burst(candies[idx].pos, 6, pal::Yellow, 50, false, 0.4f);
            if (me.candy % 10 == 0 && me.hp < 9) { me.hp++; Pop(me.pos, "EXTRA LIFE!", pal::Pink); }
        } else if (taker >= 0) {
            Burst(candies[idx].pos, 4, pal::Grey, 40, false, 0.3f);
        }
    } else if (cmd == "CANDYBACK") {
        int idx = -1;
        in >> idx;
        auto& candies = world_.Candies();
        if (idx >= 0 && idx < (int)candies.size()) { candies[idx].taken = false; Burst(candies[idx].pos, 5, pal::White, 30, false, 0.3f); }
    } else if (cmd == "CANDYRESET") {
        for (auto& c : world_.Candies()) c.taken = false;
    } else if (cmd == "SCORE") {
        in >> wins_[0] >> wins_[1];
    } else if (cmd == "KO") {
        int loser = -1;
        in >> loser;
        std::string reason;
        std::getline(in, reason);
        if (!validId(loser)) return;
        if (!reason.empty() && reason[0] == ' ') reason.erase(0, 1);
        music::Play(Sfx::KO);
        const Cat& l = cats_[loser];
        Burst(Vector2{ l.pos.x, l.pos.y - 10 }, 24, pal::White, 140, true, 0.9f);
        Banner(std::string(l.name) + " " + reason + "!", 2.8f);
    } else if (cmd == "MATCH") {
        in >> matchWinner_;
        if (!validId(matchWinner_)) return;
        state_ = State::MatchOver;
        stateTime_ = 0;
        paused_ = false;
        music::Play(Sfx::Win);
        if (matchWinner_ == myId_) rumble::Pulse(0.4f, 0.9f, 0.6f);
    }
}

// ----------------------------------------------------------------------------
// Frame / update
// ----------------------------------------------------------------------------
void Game::StartRound()
{
    Cat& me = Me();
    me.spawn = world_.SpawnPoint(myId_);
    me.Reset(me.spawn);
    me.facing = myId_ == 0 ? 1 : -1;
    me.candy = 0;
    me.invuln = 0;
    sendTimer_ = 0;
    parts_.clear();
    popups_.clear();
    matchWinner_ = -1;
    UpdateCamera(STEP, true);
}

void Game::Frame()
{
    // global keys
    if (IsKeyPressed(KEY_F2)) scanlines_ = !scanlines_;
    if (IsKeyPressed(KEY_M)) music::SetEnabled(!music::Enabled());
#if defined(PLATFORM_WEB)
    // The page owns fullscreen (browsers only allow it inside a key/click handler, see web/shell.html).
    // Here we just keep the canvas' backing store matched to the page at full device resolution.
    {
        const int w = EM_ASM_INT({ return Math.max(320, Math.floor(window.innerWidth * (window.devicePixelRatio || 1))); });
        // Emscripten's GLFW treats "size == monitor size" as a fullscreen request (refused without a
        // click) and drops the resize, so stay one invisible pixel short in that case.
        const int h = EM_ASM_INT({
            var w = Math.floor(window.innerWidth * (window.devicePixelRatio || 1));
            var h = Math.max(256, Math.floor(window.innerHeight * (window.devicePixelRatio || 1)));
            return (w == screen.width && h == screen.height) ? h - 1 : h;
        });
        if (w != GetScreenWidth() || h != GetScreenHeight()) SetWindowSize(w, h);
    }
#else
    if (IsKeyPressed(KEY_F11) || ((IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) && IsKeyPressed(KEY_ENTER)))
        ToggleBorderlessWindowed();   // borderless fullscreen: no mode switch, instant Alt+Tab
#endif

    switch (state_) {
    case State::Title:
        if (AnyFirePressed()) StartConnect();
#if !defined(PLATFORM_WEB)
        if (IsKeyPressed(KEY_ESCAPE)) quit_ = true;
#endif
        break;
    case State::Connecting:
        if (BackPressed()) LeaveGame("");
        break;
    case State::Play:
        if (paused_) {
            if (BackPressed()) LeaveGame("");                         // Esc, B or View
            else if (AnyFirePressed() || PausePressed()) paused_ = false;
        } else if (PausePressed()) {                                  // Esc, P or the Menu button
            paused_ = true;
            stateTime_ = 0;
        }
        break;
    case State::MatchOver:
        if (stateTime_ > 1.5f && AnyFirePressed()) {
            StartRound();
            state_ = State::Play;
            Banner("NEW MATCH - FIGHT!", 2.0f);
            music::Play(Sfx::Start);
        }
        break;
    }

    if (state_ != State::Title) PumpNetwork();

    acc_ += std::min(GetFrameTime(), 0.1f);
    while (acc_ >= STEP) {
        Step(STEP);
        acc_ -= STEP;
    }
    music::Update();
    Draw();
}

void Game::Step(float dt)
{
    time_ += dt;
    stateTime_ += dt;
    world_.Update(dt, time_);
    if (state_ == State::Play || state_ == State::MatchOver) StepPlay(dt);

    for (auto& p : parts_) {
        p.p.x += p.v.x * dt;
        p.p.y += p.v.y * dt;
        if (p.gravity) p.v.y += 500.0f * dt;
        p.life -= dt;
    }
    parts_.erase(std::remove_if(parts_.begin(), parts_.end(), [](const Particle& p) { return p.life <= 0; }), parts_.end());
    for (auto& p : popups_) { p.p.y -= 22.0f * dt; p.life -= dt; }
    popups_.erase(std::remove_if(popups_.begin(), popups_.end(), [](const Popup& p) { return p.life <= 0; }), popups_.end());
    if (bannerTime_ > 0) bannerTime_ -= dt;
    if (shake_ > 0) shake_ -= dt;
    rumble::Update(dt);
}

void Game::StepPlay(float dt)
{
    Cat& me = Me();
    Cat& rival = Rival();
    const bool controls = state_ == State::Play && !paused_;

    // ---- our own cat (we are the authority for it) ----------------------------------
    const bool wasKO = me.ko;
    const CatEvents ev = me.Update(controls ? ReadLocalInput() : PlayerInput{}, world_, dt);
    if (ev.jumped) { music::Play(Sfx::Jump); Burst(me.pos, 4, RGB12(0x667), 40, false, 0.3f); }
    if (ev.landed) Burst(me.pos, 3, RGB12(0x556), 30, false, 0.25f);
    if (ev.attacked) music::Play(GetRandomValue(0, 2) == 0 ? Sfx::Meow : Sfx::Jump);
    if (ev.fellInPit) {
        music::Play(Sfx::Fall);
        rumble::Pulse(0.6f, 0.2f, 0.4f);
        Pop(me.pos, "AAARGH!", me.color);
        me.hp -= 1;
        UpdateCamera(dt, true);
        if (me.hp <= 0) {
            me.hp = 0;
            me.ko = true;
            me.koTimer = 2.8f;
            net_.Send("KO FELL INTO THE DARKNESS");
            rumble::Pulse(1.0f, 1.0f, 0.8f);
        }
    }
    if (wasKO && !me.ko) { UpdateCamera(dt, true); Burst(me.pos, 12, pal::White, 60, false, 0.6f); }

    // ---- the rival, as reported by the server ----------------------------------------
    const bool rivalActive = rivalHere_ && rival.netHasState;
    if (rivalActive) rival.TickRemote(dt);

    // ---- candy corn: ask the server, it decides who was first -------------------------
    auto& candies = world_.Candies();
    for (size_t i = 0; i < candies.size(); ++i) {
        if (candyPending_[i] > 0) candyPending_[i] -= dt;
        const Candy& cd = candies[i];
        if (cd.taken || me.ko || candyPending_[i] > 0) continue;
        if (fabsf(cd.pos.x - me.pos.x) < 10 && cd.pos.y > me.pos.y - Cat::HEIGHT - 6 && cd.pos.y < me.pos.y + 6) {
            net_.Send("CANDY " + std::to_string(i));
            candyPending_[i] = 1.0f;
        }
    }

    // ---- fighting: we judge our own attacks, the victim's client applies the damage ----
    if (controls && rivalActive && !rival.ko && !me.ko && rival.invuln <= 0) {
        bool hit = false;
        const char* word = nullptr;
        if (me.AttackActive() && !me.attackLanded && CheckCollisionRecs(me.AttackBox(), rival.Body())) {
            me.attackLanded = true;
            hit = true;
            word = HIT_WORDS[GetRandomValue(0, 6)];
        }
        const Rectangle rb = rival.Body();
        if (!hit && me.vel.y > 60 && me.pos.y >= rb.y && me.pos.y <= rb.y + 9 &&
            me.pos.x + Cat::HALF_W > rb.x && me.pos.x - Cat::HALF_W < rb.x + rb.width) {
            hit = true;
            word = "SQUISH!";
            me.vel.y = -300.0f;
            music::Play(Sfx::Stomp);
        }
        if (hit) {
            net_.Send("HIT " + std::to_string(rival.id) + " " + std::to_string((int)me.pos.x));
            rival.invuln = 1.1f;   // until the rival's next state packet confirms
            rival.flash = 0.12f;
            music::Play(Sfx::Hit);
            const Vector2 hp{ rival.pos.x, rival.pos.y - 12 };
            Burst(hp, 10, pal::White, 110, true, 0.4f);
            Burst(hp, 6, rival.id == 0 ? RGB12(0x335) : RGB12(0xF92), 80, true, 0.7f);
            Pop(Vector2{ rival.pos.x, rival.pos.y - 26 }, word, pal::Yellow);
            shake_ = 0.2f;
            rumble::Pulse(0.25f, 0.55f, 0.12f);   // a little "thwack" in your hands
        }
    }

    // proximity -> radar alarm
    const float dist = fabsf(me.pos.x - rival.pos.x) + fabsf(me.pos.y - rival.pos.y) * 0.5f;
    nearTime_ = (rivalActive && dist < 220) ? nearTime_ + dt : 0;

    // ---- tell the server where we are --------------------------------------------------
    sendTimer_ -= dt;
    if (sendTimer_ <= 0) {
        net_.Send("STATE " + me.Serialize());
        sendTimer_ += SEND_INTERVAL;
        if (sendTimer_ < 0) sendTimer_ = 0;
    }

    UpdateCamera(dt, false);
}

void Game::UpdateCamera(float dt, bool snap)
{
    const Cat& c = Me();
    Vector2 t{ c.pos.x - SCREEN_W / 2.0f + c.facing * 30.0f, c.pos.y - VIEW_H * 0.6f };
    cam_.x += (t.x - cam_.x) * (snap ? 1.0f : 1.0f - expf(-dt * 5.0f));
    cam_.y += (t.y - cam_.y) * (snap ? 1.0f : 1.0f - expf(-dt * 4.0f));
    cam_.x = std::clamp(cam_.x, 0.0f, world_.PixelW() - SCREEN_W);
    cam_.y = std::clamp(cam_.y, -48.0f, world_.PixelH() - VIEW_H);
}

void Game::Burst(Vector2 at, int n, Color c, float speed, bool gravity, float life)
{
    for (int k = 0; k < n; ++k) {
        const float a = GetRandomValue(0, 359) * DEG2RAD;
        const float s = speed * (0.4f + GetRandomValue(0, 60) / 100.0f);
        parts_.push_back({ at, Vector2{ cosf(a) * s, sinf(a) * s - (gravity ? 60.0f : 10.0f) },
                           life, life, c, gravity });
    }
}

void Game::Pop(Vector2 at, const std::string& text, Color c)
{
    popups_.push_back({ at, text, 1.0f, c });
}

void Game::Banner(const std::string& text, float seconds)
{
    banner_ = text;
    bannerTime_ = seconds;
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
static void ShadowText(const char* t, int x, int y, Color c, int size = 10)
{
    DrawText(t, x + 1, y + 1, size, pal::Black);
    DrawText(t, x, y, size, c);
}

static void CenterText(const char* t, int cx, int y, Color c, int size = 10)
{
    ShadowText(t, cx - MeasureText(t, size) / 2, y, c, size);
}

void Game::Draw()
{
    const bool inGame = state_ == State::Play || state_ == State::MatchOver;
    if (inGame) DrawView();

    BeginTextureMode(screen_);
    ClearBackground(pal::Black);
    if (!inGame) {
        DrawTitle();
    } else {
        DrawTextureRec(view_.texture, Rectangle{ 0, 0, (float)SCREEN_W, (float)-VIEW_H }, Vector2{ 0, 0 }, WHITE);
        DrawHud(VIEW_H);
        if (paused_) DrawGuru();
        if (state_ == State::MatchOver) DrawMatchOver();
    }
    EndTextureMode();

    BlitToWindow();   // scales the 320x256 screen to the window and presents the frame
}

void Game::BlitToWindow()
{
    const float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    const float fit = std::min(sw / SCREEN_W, sh / SCREEN_H);

    // Fill the window as much as possible but keep every Amiga pixel the same size:
    // first blow the 320x256 picture up by a whole number (hard pixels), then fit that
    // to the window with smooth filtering. Only the pixel edges get blended a little.
    const int k = std::max(1, (int)ceilf(fit - 0.001f));
    if (k != upscale_) {
        if (upscale_) UnloadRenderTexture(upscaled_);
        upscaled_ = LoadRenderTexture(SCREEN_W * k, SCREEN_H * k);
        SetTextureFilter(upscaled_.texture, TEXTURE_FILTER_BILINEAR);
        upscale_ = k;
    }
    BeginTextureMode(upscaled_);
    DrawTexturePro(screen_.texture, Rectangle{ 0, 0, (float)SCREEN_W, (float)-SCREEN_H },
                   Rectangle{ 0, 0, (float)(SCREEN_W * k), (float)(SCREEN_H * k) }, Vector2{ 0, 0 }, 0, WHITE);
    EndTextureMode();

    BeginDrawing();
    ClearBackground(BLACK);
    const float w = floorf(SCREEN_W * fit), h = floorf(SCREEN_H * fit);
    const float x = floorf((sw - w) / 2), y = floorf((sh - h) / 2);
    DrawTexturePro(upscaled_.texture, Rectangle{ 0, 0, (float)(SCREEN_W * k), (float)-(SCREEN_H * k) },
                   Rectangle{ x, y, w, h }, Vector2{ 0, 0 }, 0, WHITE);
    if (scanlines_ && fit >= 2.0f) {   // 1084S monitor feeling
        const Color dark{ 0, 0, 0, 70 };
        const float lh = fmaxf(1.0f, floorf(fit / 3.0f));
        for (int i = 0; i < SCREEN_H; ++i) DrawRectangle((int)x, (int)(y + floorf((i + 1) * fit) - lh), (int)w, (int)lh, dark);
    }
    EndDrawing();
}

void Game::DrawView()
{
    Vector2 cam = cam_;
    if (shake_ > 0) {
        cam.x += (float)GetRandomValue(-2, 2);
        cam.y += (float)GetRandomValue(-2, 2);
    }
    cam.x = floorf(cam.x);
    cam.y = floorf(cam.y);
    const int viewH = VIEW_H;
    const Cat& me = Me();
    const Cat& rival = Rival();
    const bool rivalActive = rivalHere_ && rival.netHasState;

    BeginTextureMode(view_);
    ClearBackground(pal::Black);
    bg_.Draw(cam, SCREEN_W, viewH, world_.PixelW(), world_.PixelH(), time_);
    world_.DrawTiles(cam, SCREEN_W, viewH, time_);
    world_.DrawDecor(cam, SCREEN_W, viewH, time_);
    world_.DrawCandies(cam, SCREEN_W, viewH, time_);

    // rival first, own cat on top
    if (rivalActive) rival.Draw(sheets_[rival.id], cam, time_);
    me.Draw(sheets_[myId_], cam, time_);

    for (const auto& p : parts_) {
        const int x = (int)(p.p.x - cam.x), y = (int)(p.p.y - cam.y);
        if (x < 0 || y < 0 || x >= SCREEN_W || y >= viewH) continue;
        DrawPixel(x, y, p.life < p.maxLife * 0.3f ? Lerp12(p.c, pal::Grey, 0.5f) : p.c);
    }
    world_.DrawBats(cam, SCREEN_W, viewH, time_);

    for (const auto& p : popups_) {
        if ((int)(p.life * 20) % 2 && p.life < 0.3f) continue;
        const int w = MeasureText(p.text.c_str(), 10);
        ShadowText(p.text.c_str(), (int)(p.p.x - cam.x) - w / 2, (int)(p.p.y - cam.y), p.c);
    }

    // arrow towards the rival when off screen
    if (rivalActive) {
        const float ox = rival.pos.x - cam.x, oy = rival.pos.y - 10 - cam.y;
        if (ox < 0 || ox >= SCREEN_W || oy < 0 || oy >= viewH) {
            const int ax = (int)std::clamp(ox, 6.0f, SCREEN_W - 7.0f);
            const int ay = (int)std::clamp(oy, 8.0f, viewH - 9.0f);
            const int dir = ox < 0 ? -1 : (ox >= SCREEN_W ? 1 : 0);
            if (((int)(time_ * 4)) & 1) {
                if (dir != 0) {
                    for (int k = 0; k < 4; ++k) DrawLine(ax - dir * k, ay - 3 + k, ax - dir * k, ay + 4 - k, rival.color);
                } else {
                    const int vd = oy < 0 ? -1 : 1;
                    for (int k = 0; k < 4; ++k) DrawLine(ax - 3 + k, ay - vd * k, ax + 4 - k, ay - vd * k, rival.color);
                }
            }
            const int meters = (int)(fabsf(rival.pos.x - me.pos.x) / TILE);
            const char* t = TextFormat("%dM", meters);
            const int tw = MeasureText(t, 10);
            const int tx = dir > 0 ? SCREEN_W - tw - 12 : (dir < 0 ? 12 : ax - tw / 2);
            ShadowText(t, tx, std::clamp(ay - 4, 1, viewH - 11), rival.color);
        }
    }

    ShadowText(TextFormat("YOU: %s", me.name), 4, 3, me.color);
    if (!rivalHere_ && (((int)(time_ * 2)) & 1))
        CenterText("WAITING FOR A RIVAL TO JOIN...", SCREEN_W / 2, 18, RGB12(0xBBB));

    if (bannerTime_ > 0 && !banner_.empty()) {
        const Color c = ((int)(time_ * 8)) & 1 ? pal::Yellow : pal::Orange;
        CenterText(banner_.c_str(), SCREEN_W / 2, viewH / 2 - 40, c);
    }
    EndTextureMode();
}

static void DrawHeart(int x, int y, Color c)
{
    DrawPixel(x + 1, y, c); DrawPixel(x + 3, y, c);
    DrawRectangle(x, y + 1, 5, 1, c);
    DrawRectangle(x + 1, y + 2, 3, 1, c);
    DrawPixel(x + 2, y + 3, c);
}

static void DrawMiniPumpkin(int x, int y, bool lit)
{
    const Color o = lit ? pal::Orange : RGB12(0x423);
    DrawRectangle(x, y + 1, 5, 3, o);
    DrawRectangle(x + 1, y, 3, 5, o);
    DrawPixel(x + 2, y - 1, lit ? pal::Green : RGB12(0x232));
    if (lit) { DrawPixel(x + 1, y + 2, pal::Yellow); DrawPixel(x + 3, y + 2, pal::Yellow); }
}

static void DrawMiniCandy(int x, int y)
{
    DrawPixel(x + 2, y, pal::White);
    DrawRectangle(x + 1, y + 1, 3, 1, pal::Orange);
    DrawRectangle(x, y + 2, 5, 1, pal::Orange);
    DrawRectangle(x, y + 3, 5, 2, pal::Yellow);
}

void Game::DrawHud(int y)
{
    DrawRectangle(0, y, SCREEN_W, HUD_H, pal::Black);
    // copper bars top & bottom
    const Color bars[3] = { RGB12(0x426), RGB12(0xA5D), RGB12(0xF84) };
    for (int i = 0; i < 3; ++i) {
        DrawRectangle(0, y + i, SCREEN_W, 1, bars[2 - i]);
        DrawRectangle(0, y + HUD_H - 1 - i, SCREEN_W, 1, bars[2 - i]);
    }
    for (int p = 0; p < 2; ++p) {
        const Cat& c = cats_[p];
        const bool left = p == 0;
        const bool present = p == myId_ || rivalHere_;
        const int nameW = MeasureText(c.name, 10);
        const int nx = left ? 4 : SCREEN_W - 4 - nameW;
        DrawText(c.name, nx, y + 4, 10, present ? c.color : RGB12(0x555));
        for (int k = 0; k < WINS_NEEDED; ++k) {
            const int px = left ? nx + nameW + 4 + k * 7 : nx - 10 - k * 7;
            DrawMiniPumpkin(px, y + 5, k < wins_[p]);
        }
        if (!present) {
            if (((int)(time_ * 2)) & 1) DrawText("WAITING...", left ? 4 : SCREEN_W - 4 - MeasureText("WAITING...", 10), y + 13, 10, RGB12(0x888));
            continue;
        }
        for (int k = 0; k < 9; ++k) {
            const int hx = left ? 4 + k * 6 : SCREEN_W - 9 - k * 6;
            DrawHeart(hx, y + 15, k < c.hp ? pal::Red : RGB12(0x312));
        }
        const char* cs = TextFormat("%d", c.candy);
        const int cw = MeasureText(cs, 10);
        const int cx = left ? 62 : SCREEN_W - 62 - 7 - cw;
        DrawMiniCandy(cx, y + 14);
        DrawText(cs, cx + 7, y + 13, 10, pal::White);
    }
    DrawRadar(104, y + 3, 112, HUD_H - 6);
}

void Game::DrawRadar(int x, int y, int w, int h)
{
    const bool alarm = nearTime_ > 0 && (((int)(time_ * 6)) & 1);
    DrawRectangle(x, y, w, h, RGB12(0x102));
    // terrain profile
    const auto& g = world_.Ground();
    for (int i = 0; i < w - 2; ++i) {
        int top = World::H;
        const int a = i * World::W / (w - 2), b = (i + 1) * World::W / (w - 2);
        for (int t = a; t < b; ++t) top = std::min(top, g[t]);
        if (top >= World::H) continue;
        const int ry = y + 1 + top * (h - 2) / World::H;
        DrawRectangle(x + 1 + i, ry, 1, y + h - 1 - ry, RGB12(0x324));
        DrawPixel(x + 1 + i, ry, RGB12(0x86A));
    }
    // our view window
    const int vx = x + 1 + (int)(cam_.x / world_.PixelW() * (w - 2));
    const int vw = std::max(2, (int)(SCREEN_W / world_.PixelW() * (w - 2)));
    const Color dim = Lerp12(Me().color, pal::Black, 0.55f);
    DrawRectangle(vx, y + 1, vw, 1, dim);
    DrawRectangle(vx, y + h - 2, vw, 1, dim);
    // the cats
    for (int p = 0; p < 2; ++p) {
        if (p != myId_ && !(rivalHere_ && Rival().netHasState)) continue;
        const Cat& c = cats_[p];
        const int dx = x + 1 + (int)(std::clamp(c.pos.x, 0.0f, world_.PixelW()) / world_.PixelW() * (w - 2));
        const int dy = y + 1 + (int)(std::clamp(c.pos.y, 0.0f, world_.PixelH()) / world_.PixelH() * (h - 2));
        const bool blink = ((int)(time_ * 5) + p) & 1;
        DrawRectangle(dx - 1, dy - 2, 3, 3, blink ? c.color : pal::White);
    }
    DrawRectangleLines(x - 1, y - 1, w + 2, h + 2, alarm ? pal::Red : RGB12(0x649));
    if (nearTime_ > 0 && alarm) {
        const char* t = "FIGHT!";
        DrawText(t, x + w / 2 - MeasureText(t, 10) / 2, y + h / 2 - 4, 10, pal::Red);
    }
}

void Game::DrawTitle()
{
    // starfield
    for (int i = 0; i < 90; ++i) {
        const uint32_t hsh = Hash((uint32_t)i * 2246822519u);
        const float speed = 4.0f + (hsh % 3) * 6.0f;
        const int x = (int)(SCREEN_W - fmodf((hsh % 997) + time_ * speed, (float)SCREEN_W + 1));
        const int y = (int)((hsh >> 12) % SCREEN_H);
        DrawPixel(x, y, (hsh >> 20) % 3 == 0 ? pal::White : RGB12(0x669));
    }
    // copper bars sweeping behind the logo
    const Color barCols[3][2] = { { RGB12(0x203), RGB12(0xC6F) }, { RGB12(0x310), RGB12(0xFC4) }, { RGB12(0x021), RGB12(0x6F8) } };
    for (int b = 0; b < 3; ++b) {
        const int cy = 46 + (int)(sinf(time_ * 1.6f + b * 2.1f) * 36.0f);
        for (int k = -7; k <= 7; ++k) {
            const float t = 1.0f - fabsf((float)k) / 7.0f;
            DrawRectangle(0, cy + k, SCREEN_W, 1, Lerp12(barCols[b][0], barCols[b][1], t));
        }
    }
    // wobbling logo, column slices
    const int lx = (SCREEN_W - logo_.width) / 2;
    for (int x = 0; x < logo_.width; x += 2) {
        const int oy = (int)(sinf(time_ * 3.0f + x * 0.045f) * 4.0f);
        DrawTextureRec(logo_, Rectangle{ (float)x, 0, 2, (float)logo_.height }, Vector2{ (float)(lx + x), (float)(14 + oy) }, WHITE);
    }

    // the two contenders, double size
    for (int p = 0; p < 2; ++p) {
        const bool attack = fmodf(time_ + p * 1.3f, 4.0f) > 3.5f;
        CatFrame f = attack ? CF_ATTACK1 : (CatFrame)(CF_WALK0 + ((int)(time_ * 8) & 3));
        if (fmodf(time_ + p * 0.7f, 6.0f) < 1.0f) f = CF_IDLE0;
        const float bob = (f == CF_WALK1 || f == CF_WALK3) ? -2.0f : 0.0f;
        const float x = p == 0 ? 54.0f : SCREEN_W - 54.0f - CAT_CELL_W * 2;
        const Rectangle src{ (float)(f * CAT_CELL_W), 0, (float)(p == 1 ? -CAT_CELL_W : CAT_CELL_W), (float)CAT_CELL_H };
        DrawTexturePro(sheets_[p].tex, src, Rectangle{ x, 92 + bob, CAT_CELL_W * 2.0f, CAT_CELL_H * 2.0f }, Vector2{ 0, 0 }, 0, WHITE);
        CenterText(cats_[p].name, (int)x + CAT_CELL_W, 152, cats_[p].color);
    }
    const Color vs = ((int)(time_ * 4)) & 1 ? pal::Red : pal::Yellow;
    CenterText("VS", SCREEN_W / 2, 114, vs, 20);
    const bool pad = GamepadConnected();
    if (pad) CenterText("STICK MOVE  A JUMP  X SCRATCH  MENU PAUSE", SCREEN_W / 2, 166, RGB12(0x9C9));
    else CenterText("MOVE A/D  JUMP W  SCRATCH F  F11 FULLSCREEN", SCREEN_W / 2, 166, RGB12(0x99B));

    if (state_ == State::Connecting) {
        if (((int)(time_ * 4)) & 1) CenterText("CONNECTING...", SCREEN_W / 2, 184, pal::White);
        CenterText(serverUrl_.c_str(), SCREEN_W / 2, 196, RGB12(0x888));
    } else {
        if (((int)(time_ * 2.5f)) & 1) CenterText(pad ? "PRESS A TO JOIN THE GAME" : "PRESS FIRE TO JOIN THE GAME", SCREEN_W / 2, 182, pal::White);
        if (!netMessage_.empty()) CenterText(netMessage_.c_str(), SCREEN_W / 2, 196, pal::Red);
        else CenterText(TextFormat("SERVER: %s", serverUrl_.c_str()), SCREEN_W / 2, 196, RGB12(0x888));
    }
    CenterText("FIRST TO 3 KNOCKOUTS IS KING OF HALLOWEEN", SCREEN_W / 2, 210, RGB12(0xB8E));

    // the obligatory sine scroller
    static const char* scroll =
        "                                        "
        "*** THE CATS OF HALLOWEEN ***   A SPOOKY TWO PLAYER ONLINE SCRATCH-'EM-UP...   "
        "MORTIMER THE BLACK CAT VERSUS GINGER THE PIRATE TABBY - ONE WORLD, TWO CATS, NINE LIVES EACH!   "
        "START THE SERVER, THEN JOIN FROM TWO COMPUTERS (OR TWO WINDOWS)...   "
        "PROWL THE GRAVEYARD, THE PUMPKIN PATCH, THE HAUNTED FOREST AND THE CASTLE RUINS...   "
        "WATCH THE RADAR, HUNT DOWN YOUR RIVAL AND SCRATCH HIS WHISKERS OFF!   JUMP ON HIS HEAD FOR EXTRA STYLE POINTS.   "
        "COLLECT 10 CANDY CORNS FOR AN EXTRA LIFE.   XBOX CONTROLLERS WELCOME!   KEYS: F11 = FULLSCREEN   M = MUSIC   F2 = SCANLINES   P = PAUSE   "
        "GREETINGS TO ALL AMIGA FREAKS OUT THERE...   KEEP THE GURU MEDITATING!   ";
    const int len = (int)strlen(scroll);
    const float charW = 8.0f;
    const float off = fmodf(time_ * 60.0f, len * charW);
    const int first = (int)(off / charW);
    for (int i = 0; i < SCREEN_W / (int)charW + 2; ++i) {
        const int ci = (first + i) % len;
        const char ch[2] = { scroll[ci], 0 };
        const float x = i * charW - fmodf(off, charW);
        const float y = 234 + sinf(time_ * 3.0f + x * 0.035f) * 7.0f;
        const Color col = Lerp12(RGB12(0xF80), RGB12(0xF4F), 0.5f + 0.5f * sinf(x * 0.02f + time_));
        DrawText(ch, (int)x + 1, (int)y + 1, 10, pal::Black);
        DrawText(ch, (int)x, (int)y, 10, col);
    }
}

void Game::DrawGuru()
{
    const bool on = ((int)(stateTime_ * 1.2f)) & 1;
    DrawRectangle(0, 0, SCREEN_W, 52, pal::Black);
    if (on) {
        DrawRectangle(4, 4, SCREEN_W - 8, 3, pal::Red);
        DrawRectangle(4, 45, SCREEN_W - 8, 3, pal::Red);
        DrawRectangle(4, 4, 3, 44, pal::Red);
        DrawRectangle(SCREEN_W - 7, 4, 3, 44, pal::Red);
    }
    CenterText("Software Failure.   Press FIRE to continue.", SCREEN_W / 2, 14, pal::Red);
    CenterText("Guru Meditation #0BADCA75.0000BEEF", SCREEN_W / 2, 28, pal::Red);
    CenterText("(THE WORLD KEEPS TURNING - ESC / B TO LEAVE)", SCREEN_W / 2, 60, RGB12(0x999));
}

void Game::DrawMatchOver()
{
    if (matchWinner_ < 0) return;
    const Cat& w = cats_[matchWinner_];
    DrawRectangle(0, 60, SCREEN_W, 116, pal::Black);
    for (int i = 0; i < 4; ++i) {
        const Color c = Lerp12(RGB12(0xF80), RGB12(0x426), i / 3.0f);
        DrawRectangle(0, 60 + i, SCREEN_W, 1, c);
        DrawRectangle(0, 175 - i, SCREEN_W, 1, c);
    }
    const float bob = sinf(time_ * 4) * 3;
    const Rectangle src{ (float)(CF_IDLE0 * CAT_CELL_W), 0, (float)CAT_CELL_W, (float)CAT_CELL_H };
    DrawTexturePro(sheets_[matchWinner_].tex, src, Rectangle{ SCREEN_W / 2.0f - CAT_CELL_W, 70 + bob, CAT_CELL_W * 2.0f, CAT_CELL_H * 2.0f }, Vector2{}, 0, WHITE);
    CenterText(TextFormat("%s IS THE", w.name), SCREEN_W / 2, 130, w.color);
    CenterText("KING OF HALLOWEEN!", SCREEN_W / 2, 142, ((int)(time_ * 6)) & 1 ? pal::Yellow : pal::Orange);
    CenterText(matchWinner_ == myId_ ? "THAT'S YOU! PURRFECT!" : "BETTER LUCK IN YOUR NEXT LIFE...", SCREEN_W / 2, 156, RGB12(0xBBB));
    if (stateTime_ > 1.5f && (((int)(time_ * 2.5f)) & 1)) CenterText("PRESS FIRE FOR A NEW MATCH", SCREEN_W / 2, 186, pal::White);
}
