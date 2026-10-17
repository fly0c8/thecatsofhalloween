// Monsters.h - ghosts, skeletons, zombies and witches haunting the world.
//
// They work like the pumpkins: the host client simulates them and streams a compact
// snapshot, the other client smooths and draws them. Touching one costs a life;
// a scratch or a stomp knocks it out for a while. The server referees who knocked out
// which monster (BONK) and brings them back later (MONSTERBACK).
#pragma once
#include "raylib.h"
#include <string>
#include <vector>

class World;

enum MonsterType { M_GHOST, M_SKELETON, M_ZOMBIE, M_WITCH, M_COUNT };

struct Monster {
    MonsterType type = M_GHOST;
    Vector2 home{};          // bottom centre (ghosts and witches: up in the air)
    Vector2 pos{};           // bottom centre
    Vector2 vel{};
    Vector2 netTarget{};
    int facing = 1;
    bool onGround = false;
    bool defeated = false;
    bool faded = false;      // ghost: see-through and harmless for a moment
    bool diving = false;     // witch: swooping down at a cat
    float timer = 0;         // behaviour timer (witch dive, ghost fade)
    float phase = 0;
    float pending = 0;       // we asked the server to knock it out; don't ask again for a moment
};

class MonsterCrowd {
public:
    void Init(const World& w);          // same monsters in the same order on every client
    void Load();                        // sprites (needs an open window)
    void Unload();

    // host: move them, hunt the cats in 'targets'
    void Simulate(const World& w, float dt, float time, const std::vector<Vector2>& targets);
    // everybody else: ease towards the host's snapshot
    void TickRemote(float dt);
    std::string Serialize() const;      // "MONSTERS ..." payload
    bool ApplyRemote(const char* payload);

    void Respawn(int idx);              // back home and active again
    void ResetAll();
    bool Dangerous(const Monster& m) const { return !m.defeated && !m.faded; }

    // monsters that just started an attack (witch dive) - for sound effects
    std::vector<int> TakeAttackEvents();

    std::vector<Monster>& All() { return monsters_; }
    const std::vector<Monster>& All() const { return monsters_; }
    static Rectangle Body(const Monster& m);
    static const char* Name(MonsterType t);

    void Draw(Vector2 cam, int viewW, int viewH, float time) const;

private:
    void Walk(Monster& m, const World& w, float dt, float speed);
    std::vector<Monster> monsters_;
    std::vector<int> attackEvents_;
    Texture2D sheet_{};                 // one row per type, two animation frames each
};
