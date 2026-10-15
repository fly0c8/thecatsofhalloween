// Cat.h - a player-controlled cat: platform physics, animation and combat state.
#pragma once
#include "raylib.h"
#include "CatSprites.h"
#include "Input.h"
#include <string>

class World;

struct CatEvents {          // things that happened this step (for sound / particles)
    bool jumped = false;
    bool landed = false;
    bool attacked = false;
    bool fellInPit = false;
};

struct Cat {
    int id = 0;
    const char* name = "CAT";
    Color color = WHITE;            // HUD / radar colour

    Vector2 pos{};                  // bottom-centre in world pixels
    Vector2 vel{};
    bool onGround = false;
    int facing = 1;                 // +1 right, -1 left

    float coyote = 0, jumpBuffer = 0;
    bool jumpWasDown = false, attackWasDown = false;
    float attackTimer = 0, attackCooldown = 0;
    bool attackLanded = false;
    float hurtTimer = 0, invuln = 0, flash = 0;
    float dropThrough = 0;
    float animTime = 0, blinkTimer = 0;

    int hp = 9;                     // nine lives, naturally
    int candy = 0;
    int wins = 0;
    bool ko = false;
    float koTimer = 0;

    Vector2 spawn{};
    Vector2 lastSafe{};

    static constexpr float HALF_W = 5.0f;
    static constexpr float HEIGHT = 20.0f;
    static constexpr float ATTACK_TIME = 0.30f;

    void Reset(Vector2 at);

    // --- networking -----------------------------------------------------------
    std::string Serialize() const;                    // our state -> "STATE ..." payload
    bool ApplyRemote(const char* payload);            // rival's state from the server
    void TickRemote(float dt);                        // smooth + animate the rival between packets
    Vector2 netTarget{};
    bool netHasState = false;
    CatEvents Update(const PlayerInput& in, const World& w, float dt);

    Rectangle Body() const { return Rectangle{ pos.x - HALF_W, pos.y - HEIGHT, HALF_W * 2, HEIGHT }; }
    bool AttackActive() const { return attackTimer > 0 && attackTimer < ATTACK_TIME - 0.06f && attackTimer > 0.10f; }
    Rectangle AttackBox() const;
    void Hurt(float fromX, int damage);

    CatFrame Frame() const;
    void Draw(const CatSheet& sheet, Vector2 cam, float time) const;
};
