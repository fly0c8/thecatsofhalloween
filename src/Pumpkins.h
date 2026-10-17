// Pumpkins.h - the jumping pumpkins the two cats hunt together.
//
// One client (the "host", chosen by the server) simulates every pumpkin and streams
// a compact snapshot; the other client only smooths and draws them. Whether a pumpkin
// is smashed is refereed by the server (SMASH messages; smashed ones stay gone), so that flag is
// never part of the snapshot.
#pragma once
#include "raylib.h"
#include <string>
#include <vector>

class World;

struct Pumpkin {
    Vector2 home{};          // where it lives and respawns (bottom centre, world pixels)
    Vector2 pos{};           // bottom centre
    Vector2 vel{};
    Vector2 netTarget{};     // last reported position (non-host)
    bool onGround = true;
    bool angry = false;
    bool smashed = false;
    bool sitter = false;     // sits still at home (until it gets angry)
    int facing = 1;
    float hopTimer = 0;
    float squash = 0;        // > 0 right after landing
    float pending = 0;       // we asked the server to smash it; don't ask again for a moment
};

class PumpkinPatch {
public:
    static constexpr float HALF_W = 7.0f;
    static constexpr float HEIGHT = 12.0f;

    void Init(const World& w);          // jumping + sitting pumpkins of the world (same order on every client)
    void Load();                        // sprites (needs an open window)
    void Unload();

    // host: hop around, angry ones chase the nearest cat in 'targets'
    void Simulate(const World& w, float dt, const std::vector<Vector2>& targets);
    // everybody else: ease towards the host's snapshot
    void TickRemote(float dt);
    std::string Serialize() const;      // "PUMPKINS ..." payload
    bool ApplyRemote(const char* payload);

    void MakeAngry(int idx);
    void CalmAll();
    void Respawn(int idx);              // back home, calm, not smashed (new round)
    void ResetAll();
    int AngryCount() const;

    // pumpkins that just turned angry (consumed by the game for sound + popups)
    std::vector<int> TakeAngerEvents();

    std::vector<Pumpkin>& All() { return pumpkins_; }
    const std::vector<Pumpkin>& All() const { return pumpkins_; }
    static Rectangle Body(const Pumpkin& p) { return Rectangle{ p.pos.x - HALF_W, p.pos.y - HEIGHT, HALF_W * 2, HEIGHT }; }

    void Draw(Vector2 cam, int viewW, int viewH, float time) const;

private:
    void MoveAndCollide(Pumpkin& p, const World& w, float dt);

    std::vector<Pumpkin> pumpkins_;
    std::vector<int> angerEvents_;
    Texture2D sheet_{};                 // frame 0: calm, frame 1: angry
};
