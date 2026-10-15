// Game.h - states, the shared networked world, HUD + radar, title and pause screens.
#pragma once
#include "raylib.h"
#include "Background.h"
#include "Cat.h"
#include "CatSprites.h"
#include "Net.h"
#include "World.h"
#include <string>
#include <vector>

class Game {
public:
    void Init(const std::string& serverUrl);
    void Shutdown();
    void Frame();                 // one rendered frame (runs fixed 60 Hz logic steps inside)
    bool WantsQuit() const { return quit_; }

private:
    enum class State { Title, Connecting, Play, MatchOver };

    struct Particle { Vector2 p, v; float life, maxLife; Color c; bool gravity; };
    struct Popup { Vector2 p; std::string text; float life; Color c; };

    // networking
    void StartConnect();
    void PumpNetwork();
    void HandleMessage(const std::string& msg);
    void LeaveGame(const std::string& why);

    void StartRound();
    void Step(float dt);
    void StepPlay(float dt);
    void UpdateCamera(float dt, bool snap);

    void Burst(Vector2 at, int n, Color c, float speed, bool gravity, float life = 0.5f);
    void Pop(Vector2 at, const std::string& text, Color c);
    void Banner(const std::string& text, float seconds);

    void Draw();
    void DrawView();
    void DrawHud(int y);
    void DrawRadar(int x, int y, int w, int h);
    void DrawTitle();
    void DrawGuru();
    void DrawMatchOver();
    void BlitToWindow();
    void BuildLogo();

    Cat& Me() { return cats_[myId_]; }
    Cat& Rival() { return cats_[1 - myId_]; }

    State state_ = State::Title;
    bool quit_ = false;
    bool paused_ = false;        // Guru screen; the shared world keeps running
    bool scanlines_ = true;
    float time_ = 0, acc_ = 0, stateTime_ = 0;

    RenderTexture2D screen_{};
    RenderTexture2D upscaled_{};   // screen_ blown up by a whole number, then smoothly fitted to the window
    int upscale_ = 0;
    RenderTexture2D view_{};
    Texture2D logo_{};

    World world_;
    Background bg_;
    CatSheet sheets_[2];
    Cat cats_[2];
    Vector2 cam_{};
    float shake_ = 0;

    // network state
    NetClient net_;
    std::string serverUrl_;
    std::string netMessage_;     // shown on the title screen (errors etc.)
    int myId_ = 0;
    bool rivalHere_ = false;
    int wins_[2] = { 0, 0 };
    float sendTimer_ = 0;
    std::vector<float> candyPending_;   // per candy: time until we may ask the server again

    std::vector<Particle> parts_;
    std::vector<Popup> popups_;
    std::string banner_;
    float bannerTime_ = 0;
    int matchWinner_ = -1;
    float nearTime_ = 0;

    static constexpr int WINS_NEEDED = 3;
    static constexpr int VIEW_H = 232;   // 320x232 game view + 24 px HUD
};
