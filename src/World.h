// World.h - the big side-scrolling Halloween world.
// Generated from a seed, so two networked clients build the identical map.
#pragma once
#include "raylib.h"
#include <cstdint>
#include <vector>

constexpr int TILE = 16;

enum Tile : uint8_t { T_EMPTY = 0, T_DIRT, T_BRICK, T_PLATFORM, T_COUNT };

enum Zone { Z_GRAVEYARD, Z_PUMPKINS, Z_FOREST, Z_CASTLE, Z_COUNT };

enum DecorType : uint8_t {
    D_TOMB, D_CROSS, D_PUMPKIN, D_FENCE, D_TREE, D_SKULL, D_CANDLE,
    D_MUSHROOM, D_PILLAR, D_SCARECROW, D_COUNT
};

struct Decor { DecorType type; int x, y; };          // x,y = bottom centre in pixels
struct Candy { Vector2 pos; bool taken = false; float respawn = 0; };
struct Bat   { float x, y, baseY, speed, phase; };
struct MonsterSpawn { int type; Vector2 pos; };   // type: see MonsterType in Monsters.h

class World {
public:
    static constexpr int W = 384;   // tiles  (6144 px)
    static constexpr int H = 28;    // tiles  (448 px)

    void Generate(uint32_t seed);
    void Load();      // build textures (needs an open window)
    void Unload();
    void Update(float dt, float time);

    Tile At(int tx, int ty) const;
    bool Solid(int tx, int ty) const;
    bool Platform(int tx, int ty) const;
    Zone ZoneAt(float px) const;
    int  GroundTop(int tx) const { return ground_[tx < 0 ? 0 : (tx >= W ? W - 1 : tx)]; }

    float PixelW() const { return (float)(W * TILE); }
    float PixelH() const { return (float)(H * TILE); }

    Vector2 SpawnPoint(int player) const;

    // Rendering (camera = top-left of the view in world pixels)
    void DrawTiles(Vector2 cam, int viewW, int viewH, float time) const;
    void DrawDecor(Vector2 cam, int viewW, int viewH, float time) const;
    void DrawCandies(Vector2 cam, int viewW, int viewH, float time) const;
    void DrawBats(Vector2 cam, int viewW, int viewH, float time) const;

    std::vector<Candy>& Candies() { return candies_; }
    const std::vector<Vector2>& PumpkinHomes() const { return pumpkinHomes_; }        // the jumping ones
    const std::vector<Vector2>& SittingPumpkins() const { return sittingPumpkins_; }  // the lazy ones
    const std::vector<MonsterSpawn>& MonsterSpawns() const { return monsterSpawns_; }
    const std::vector<int>& Ground() const { return ground_; }

private:
    std::vector<uint8_t> tiles_;
    std::vector<int> ground_;        // top surface row per column (H = pit)
    std::vector<Decor> decor_;
    std::vector<Candy> candies_;
    std::vector<Bat> bats_;
    std::vector<Vector2> pumpkinHomes_;   // where the jumping pumpkins live (bottom centre)
    std::vector<Vector2> sittingPumpkins_; // pumpkins that just sit in the patch until they get angry
    std::vector<MonsterSpawn> monsterSpawns_; // ghosts, skeletons, zombies and witches
    Texture2D tileset_{};
    Texture2D decorAtlas_{};
    Rectangle decorRects_[D_COUNT]{};
};

void DrawDitherDisc(int cx, int cy, int r, Color c, int phase = 0);
