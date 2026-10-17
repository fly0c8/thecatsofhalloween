#include "World.h"
#include "Amiga.h"
#include "PixelArt.h"
#include <algorithm>
#include <cmath>
#include <map>

using namespace amiga;

// ----------------------------------------------------------------------------
// Generation
// ----------------------------------------------------------------------------
static Zone ZoneOfColumn(int tx) { return (Zone)std::clamp(tx * Z_COUNT / World::W, 0, Z_COUNT - 1); }

void World::Generate(uint32_t seed)
{
    Rng rng(seed);
    tiles_.assign(W * H, T_EMPTY);
    ground_.assign(W, H);
    decor_.clear();
    candies_.clear();
    bats_.clear();
    pumpkinHomes_.clear();
    sittingPumpkins_.clear();
    monsterSpawns_.clear();

    // --- 1. ground height map ------------------------------------------------
    int h = 19;
    int x = 0;
    while (x < W) {
        const Zone z = ZoneOfColumn(x);
        const bool safe = x < 12 || x >= W - 12;
        if (safe) { ground_[x++] = h; continue; }

        if (rng.Chance(0.10f) && x < W - 18 && x > 0) {      // a pit
            h = std::max(h, ground_[x - 1]);                  // far side never higher than take-off
            int len = rng.Range(2, 4);
            for (int i = 0; i < len && x < W; ++i) ground_[x++] = H;
            for (int i = 0; i < 2 && x < W; ++i) ground_[x++] = h; // landing zone
            continue;
        }
        const int run = (z == Z_CASTLE) ? rng.Range(2, 6) : rng.Range(3, 9);
        for (int i = 0; i < run && x < W; ++i) ground_[x++] = h;
        const int maxStep = (z == Z_CASTLE) ? 3 : 2;
        h = std::clamp(h + rng.Range(-maxStep, maxStep), 12, 23);
        if (x >= W - 14) h = std::clamp(h, 17, 21);
    }

    // --- 2. fill tiles -----------------------------------------------------------
    for (int tx = 0; tx < W; ++tx) {
        const Tile mat = ZoneOfColumn(tx) == Z_CASTLE ? T_BRICK : T_DIRT;
        for (int ty = ground_[tx]; ty < H; ++ty) tiles_[ty * W + tx] = mat;
    }

    // --- 3. floating one-way platforms ---------------------------------------
    std::vector<std::pair<int, int>> platformSpots;   // (centre x, row)
    for (int tx = 14; tx < W - 14;) {
        if (rng.Chance(0.6f)) {
            const int len = rng.Range(3, 6);
            int minG = H;
            for (int i = 0; i < len; ++i) minG = std::min(minG, std::min(ground_[tx + i], H - 2));
            const int row = minG - 3;
            if (row > 3) {
                for (int i = 0; i < len; ++i) tiles_[row * W + tx + i] = T_PLATFORM;
                platformSpots.push_back({ tx * TILE + len * TILE / 2, row });
                if (rng.Chance(0.35f) && row - 3 > 2) {          // a second storey
                    const int off = rng.Range(-2, 2);
                    const int len2 = rng.Range(2, 4);
                    for (int i = 0; i < len2; ++i) {
                        const int px = std::clamp(tx + off + len / 2 + i, 0, W - 1);
                        if (tiles_[(row - 3) * W + px] == T_EMPTY) tiles_[(row - 3) * W + px] = T_PLATFORM;
                    }
                    platformSpots.push_back({ (tx + off + len / 2) * TILE + len2 * TILE / 2, row - 3 });
                }
            }
            tx += len + rng.Range(4, 10);
        } else {
            tx += rng.Range(3, 7);
        }
    }

    // --- 4. decoration ----------------------------------------------------------
    for (int tx = 3; tx < W - 3; ++tx) {
        const int g = ground_[tx];
        if (g >= H || ground_[tx + 1] != g || ground_[tx - 1] != g) continue;
        const int px = tx * TILE + 8, py = g * TILE;
        const Zone z = ZoneOfColumn(tx);
        const float r = rng.Float();
        DecorType t = D_COUNT;
        switch (z) {
        case Z_GRAVEYARD:
            if (r < 0.20f) t = D_TOMB; else if (r < 0.28f) t = D_CROSS; else if (r < 0.36f) t = D_FENCE;
            else if (r < 0.39f) t = D_SKULL; else if (r < 0.43f) t = D_TREE;
            break;
        case Z_PUMPKINS:
            if (r < 0.30f) t = D_PUMPKIN; else if (r < 0.34f) t = D_SCARECROW; else if (r < 0.37f) t = D_TREE;
            else if (r < 0.40f) t = D_CROSS;
            break;
        case Z_FOREST:
            if (r < 0.20f) t = D_TREE; else if (r < 0.34f) t = D_MUSHROOM; else if (r < 0.39f) t = D_SKULL;
            else if (r < 0.42f) t = D_TOMB; else if (r < 0.45f) t = D_PUMPKIN;
            break;
        case Z_CASTLE:
            if (r < 0.10f) t = D_PILLAR; else if (r < 0.26f) t = D_CANDLE; else if (r < 0.33f) t = D_SKULL;
            else if (r < 0.36f) t = D_TOMB;
            break;
        default: break;
        }
        if (t == D_COUNT) continue;
        if (t == D_FENCE) {
            const int n = rng.Range(2, 4);
            for (int i = 0; i < n && tx + i < W && ground_[tx + i] == g; ++i)
                decor_.push_back({ D_FENCE, (tx + i) * TILE + 8, py });
            tx += n;
            continue;
        }
        const int dx = px + rng.Range(-3, 3);
        if (t == D_PUMPKIN) sittingPumpkins_.push_back(Vector2{ (float)dx, (float)py });   // a real, smashable one
        else decor_.push_back({ t, dx, py });
        tx += (t == D_TREE) ? 3 : 1;
    }
    // trees go behind everything else
    std::stable_sort(decor_.begin(), decor_.end(), [](const Decor& a, const Decor& b) {
        return (a.type == D_TREE) > (b.type == D_TREE);
    });

    // --- 5. candy corn ---------------------------------------------------------
    for (int tx = 8; tx < W - 8; ++tx) {
        if (ground_[tx] >= H || !rng.Chance(0.10f)) continue;
        candies_.push_back({ Vector2{ (float)(tx * TILE + 8), (float)(ground_[tx] * TILE - 14) } });
    }
    for (auto& p : platformSpots)
        if (rng.Chance(0.7f)) candies_.push_back({ Vector2{ (float)p.first, (float)(p.second * TILE - 12) } });

    // --- 6. bats ------------------------------------------------------------------
    for (int i = 0; i < 40; ++i) {
        Bat b;
        b.x = rng.Float() * PixelW();
        const int tx = std::clamp((int)(b.x / TILE), 0, W - 1);
        b.baseY = (float)(std::min(ground_[tx], H - 4) * TILE - rng.Range(48, 110));
        b.y = b.baseY;
        b.speed = (rng.Chance(0.5f) ? 1.0f : -1.0f) * (18.0f + rng.Float() * 30.0f);
        b.phase = rng.Float() * 6.28f;
        bats_.push_back(b);
    }

    // --- 7. homes of the jumping pumpkins -------------------------------------------
    // (generated last, so everything above stays exactly as it was)
    // Most live in the pumpkin patch; none right at the start where the cats appear.
    for (int tx = 24; tx < W - 4; ++tx) {
        const int g = ground_[tx];
        if (g >= H || ground_[tx - 1] != g || ground_[tx + 1] != g) continue;
        if (!rng.Chance(ZoneOfColumn(tx) == Z_PUMPKINS ? 0.18f : 0.06f)) continue;
        pumpkinHomes_.push_back(Vector2{ (float)(tx * TILE + 8), (float)(g * TILE) });
        tx += 3;
    }

    // --- 8. monsters (also last, for the same reason) --------------------------------
    // graveyard: skeletons, zombies, ghosts - pumpkin patch: witches, scarecrow zombies -
    // forest: zombies, witches, ghosts - castle: ghosts, skeletons, witches
    enum { GHOST, SKELETON, ZOMBIE, WITCH };
    static const int byZone[Z_COUNT][4] = {
        { SKELETON, ZOMBIE, GHOST, SKELETON },
        { WITCH, ZOMBIE, WITCH, GHOST },
        { ZOMBIE, WITCH, GHOST, ZOMBIE },
        { GHOST, SKELETON, WITCH, GHOST },
    };
    for (int tx = 30; tx < W - 6; ++tx) {
        const int g = ground_[tx];
        if (g >= H || !rng.Chance(0.09f)) continue;
        const int type = byZone[ZoneOfColumn(tx)][rng.Range(0, 3)];
        const float x = (float)(tx * TILE + 8);
        if (type == GHOST) {
            monsterSpawns_.push_back({ type, Vector2{ x, (float)(g * TILE - rng.Range(24, 48)) } });
        } else if (type == WITCH) {
            int top = g;   // fly well above the highest ground nearby
            for (int k = std::max(0, tx - 12); k <= std::min(W - 1, tx + 12); ++k) top = std::min(top, ground_[k]);
            monsterSpawns_.push_back({ type, Vector2{ x, (float)std::max(28, top * TILE - rng.Range(70, 100)) } });
        } else {
            if (ground_[tx - 1] != g || ground_[tx + 1] != g) continue;
            monsterSpawns_.push_back({ type, Vector2{ x, (float)(g * TILE) } });
        }
        tx += 10;
    }
}

Tile World::At(int tx, int ty) const
{
    if (tx < 0 || tx >= W || ty < 0 || ty >= H) return T_EMPTY;
    return (Tile)tiles_[ty * W + tx];
}

bool World::Solid(int tx, int ty) const
{
    if (tx < 0 || tx >= W) return true;    // invisible walls at the world edges
    const Tile t = At(tx, ty);
    return t == T_DIRT || t == T_BRICK;
}

bool World::Platform(int tx, int ty) const { return At(tx, ty) == T_PLATFORM; }

Zone World::ZoneAt(float px) const { return ZoneOfColumn((int)(px / TILE)); }

Vector2 World::SpawnPoint(int player) const
{
    const int tx = player == 0 ? 5 : 7;   // the two cats start side by side
    return Vector2{ (float)(tx * TILE + 8), (float)(ground_[tx] * TILE) };
}

void World::Update(float dt, float time)
{
    for (auto& b : bats_) {
        b.x += b.speed * dt;
        if (b.x < -20) b.x += PixelW() + 40;
        if (b.x > PixelW() + 20) b.x -= PixelW() + 40;
        b.y = b.baseY + sinf(time * 2.0f + b.phase) * 10.0f;
    }
}

// ----------------------------------------------------------------------------
// Graphics: tileset
// ----------------------------------------------------------------------------
enum TileGfx { G_EMPTY, G_DIRT_A, G_DIRT_B, G_GRASS_A, G_GRASS_B, G_BRICK, G_BRICK_MOSS, G_PLATFORM,
               G_DIRT_DEEP, G_DIRT_DEEPER, G_BRICK_DEEP, G_BRICK_DEEPER, G_BONE, G_COUNT };

static void PaintDirt(Image& img, int ox, uint32_t seed)
{
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            const uint32_t h = Hash2(x, y, seed);
            Color c = RGB12(0x432);
            if (h % 9 == 0) c = RGB12(0x321);
            else if (h % 17 == 0) c = RGB12(0x543);
            else if (((x + y) & 1) && (h % 5 == 0)) c = RGB12(0x322);
            ImageDrawPixel(&img, ox + x, y, c);
        }
    // a couple of pebbles and a buried bone
    for (int i = 0; i < 2; ++i) {
        const uint32_t h = Hash2(i, 99, seed);
        const int px = 2 + h % 11, py = 3 + (h >> 8) % 10;
        ImageDrawRectangle(&img, ox + px, py, 2, 2, RGB12(0x776));
        ImageDrawPixel(&img, ox + px, py, RGB12(0x998));
        ImageDrawPixel(&img, ox + px + 1, py + 1, RGB12(0x554));
    }
}

// Darken an already painted tile with an ordered dither: the deeper, the darker
static void Darken(Image& img, int ox, int srcOx, float amount)
{
    static const int bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            Color c = GetImageColor(img, srcOx + x, y);
            const bool extra = bayer[y & 3][x & 3] / 16.0f < amount * 0.5f;
            c = Lerp12(c, RGB12(0x101), amount + (extra ? 0.18f : 0.0f));
            ImageDrawPixel(&img, ox + x, y, c);
        }
}

static void PaintBone(Image& img, int ox)
{
    PaintDirt(img, ox, 9);
    ImageDrawRectangle(&img, ox + 4, 9, 7, 1, RGB12(0xDDC));
    ImageDrawRectangle(&img, ox + 4, 10, 7, 1, RGB12(0xAA9));
    for (int dy : {8, 11}) { ImageDrawPixel(&img, ox + 3, dy, RGB12(0xDDC)); ImageDrawPixel(&img, ox + 11, dy, RGB12(0xDDC)); }
}

static void PaintGrass(Image& img, int ox, uint32_t seed)
{
    PaintDirt(img, ox, seed);
    for (int x = 0; x < TILE; ++x) {
        const uint32_t h = Hash2(x, 7, seed);
        const int depth = 3 + (int)(h % 3);
        for (int y = 0; y < depth; ++y) {
            Color c = y == 0 ? RGB12(0x5B6) : (y == 1 ? RGB12(0x395) : RGB12(0x274));
            ImageDrawPixel(&img, ox + x, y, c);
        }
        if ((h >> 4) % 4 == 0) {   // hanging roots
            ImageDrawPixel(&img, ox + x, depth, RGB12(0x163));
            ImageDrawPixel(&img, ox + x, depth + 1, RGB12(0x152));
        }
    }
}

static void PaintBrick(Image& img, int ox, bool moss)
{
    for (int y = 0; y < TILE; ++y)
        for (int x = 0; x < TILE; ++x) {
            const int row = y / 4;
            const int jx = (x + (row & 1) * 4) % 8;
            Color c = RGB12(0x667);
            if (y % 4 == 3 || jx == 7) c = RGB12(0x223);
            else if (y % 4 == 0) c = RGB12(0x889);
            else if (y % 4 == 2 || jx == 6) c = RGB12(0x556);
            if (jx == 0 && y % 4 != 3) c = RGB12(0x778);
            const uint32_t h = Hash2(x, y, 5);
            if (h % 23 == 0 && y % 4 != 3) c = RGB12(0x445);
            ImageDrawPixel(&img, ox + x, y, c);
        }
    if (moss) {
        for (int x = 0; x < TILE; ++x) {
            const uint32_t h = Hash2(x, 3, 11);
            const int d = 1 + (int)(h % 3);
            for (int y = 0; y < d; ++y) ImageDrawPixel(&img, ox + x, y, y == 0 ? RGB12(0x5A5) : RGB12(0x374));
            if (h % 5 == 0) ImageDrawPixel(&img, ox + x, d + 1, RGB12(0x263));
        }
    }
}

static void PaintPlatform(Image& img, int ox)
{
    for (int x = 0; x < TILE; ++x) {
        ImageDrawPixel(&img, ox + x, 0, RGB12(0xB85));
        for (int y = 1; y < 5; ++y) {
            Color c = RGB12(0x852);
            if (Hash2(x, y, 3) % 6 == 0) c = RGB12(0x642);
            ImageDrawPixel(&img, ox + x, y, c);
        }
        ImageDrawPixel(&img, ox + x, 5, RGB12(0x420));
    }
    ImageDrawPixel(&img, ox + 0, 2, RGB12(0x420));
    ImageDrawPixel(&img, ox + 15, 3, RGB12(0x420));
    ImageDrawPixel(&img, ox + 3, 2, RGB12(0xBBB));
    ImageDrawPixel(&img, ox + 12, 2, RGB12(0xBBB));
    // little support struts
    for (int y = 6; y < 9; ++y) { ImageDrawPixel(&img, ox + 7, y, RGB12(0x531)); ImageDrawPixel(&img, ox + 8, y, RGB12(0x420)); }
}

// ----------------------------------------------------------------------------
// Graphics: decoration atlas
// ----------------------------------------------------------------------------
static const std::map<char, Color>& DecorPalette()
{
    static const std::map<char, Color> p = {
        {'O', RGB12(0x000)}, {'g', RGB12(0x889)}, {'G', RGB12(0x556)}, {'L', RGB12(0xBBC)},
        {'o', RGB12(0xF80)}, {'r', RGB12(0xB40)}, {'y', RGB12(0xFE5)}, {'n', RGB12(0x4A2)},
        {'N', RGB12(0x262)}, {'w', RGB12(0x852)}, {'W', RGB12(0x531)}, {'d', RGB12(0x532)},
        {'t', RGB12(0x323)}, {'T', RGB12(0x545)}, {'k', RGB12(0xEED)}, {'K', RGB12(0xAA9)},
        {'i', RGB12(0x334)}, {'I', RGB12(0x778)}, {'p', RGB12(0x92C)}, {'P', RGB12(0xD6F)},
        {'c', RGB12(0xEEC)}, {'x', RGB12(0x000)}, {'R', RGB12(0xC22)},
    };
    return p;
}

static PixelCanvas PaintDecor(DecorType t)
{
    switch (t) {
    case D_TOMB: {
        PixelCanvas c(16, 19);
        c.Ellipse(8, 7, 5, 5, 'g');
        c.Rect(3, 7, 11, 10, 'g');
        c.Line(4, 5, 4, 15, 'L');
        c.Line(5, 3, 7, 2, 'L');
        c.Line(13, 7, 13, 16, 'G');
        c.Line(8, 5, 8, 11, 'G');
        c.Line(6, 7, 10, 7, 'G');
        c.Line(10, 12, 12, 15, 'G');
        c.Rect(2, 16, 13, 2, 'G');
        c.Set(3, 15, 'N'); c.Set(5, 15, 'N'); c.Set(11, 16, 'n');
        return c;
    }
    case D_CROSS: {
        PixelCanvas c(14, 20);
        c.Ellipse(7, 18, 6, 2, 'd');
        c.Rect(6, 1, 3, 17, 'w');
        c.Rect(2, 5, 11, 3, 'w');
        c.Line(8, 1, 8, 17, 'W');
        c.Line(2, 7, 12, 7, 'W');
        return c;
    }
    case D_PUMPKIN: {
        PixelCanvas c(18, 15);
        c.Rect(8, 1, 2, 3, 'N');
        c.Set(10, 2, 'n'); c.Set(11, 2, 'n'); c.Set(11, 3, 'n');
        c.Ellipse(9, 9, 7, 5, 'o');
        for (int y = 5; y <= 13; ++y) { c.Set(5, y, 'r'); c.Set(13, y, 'r'); }
        c.Line(2, 8, 2, 11, 'r');
        c.Set(5, 7, 'y'); c.Set(6, 7, 'y'); c.Set(6, 6, 'y');
        c.Set(12, 6, 'y'); c.Set(12, 7, 'y'); c.Set(13, 7, 'o');
        c.Set(11, 7, 'y'); c.Set(9, 9, 'y');
        for (int x = 5; x <= 13; ++x) { if (x % 2 == 0) c.Set(x, 10, 'y'); c.Set(x, 11, 'y'); }
        c.Set(5, 12, 'r'); c.Set(13, 12, 'r');
        return c;
    }
    case D_FENCE: {
        PixelCanvas c(16, 18);
        for (int x : {2, 6, 10, 14}) {
            c.Rect(x, 4, 1, 14, 'i');
            c.Set(x, 2, 'I'); c.Set(x, 3, 'I'); c.Set(x - 1, 4, 'i'); c.Set(x + 1, 4, 'i');
        }
        c.Rect(0, 6, 16, 1, 'I');
        c.Rect(0, 14, 16, 1, 'i');
        return c;
    }
    case D_TREE: {
        PixelCanvas c(46, 62);
        c.Rect(19, 40, 8, 21, 't');
        c.Line(21, 40, 22, 24, 't', 4);
        c.Line(19, 59, 13, 61, 't', 2);
        c.Line(26, 59, 33, 61, 't', 2);
        c.Line(21, 30, 9, 18, 't', 3);
        c.Line(9, 18, 3, 15, 't', 2);
        c.Line(9, 19, 8, 9, 't', 2);
        c.Line(8, 9, 5, 5, 't');
        c.Line(24, 27, 35, 16, 't', 3);
        c.Line(35, 16, 42, 13, 't', 2);
        c.Line(35, 17, 34, 7, 't', 2);
        c.Line(34, 7, 37, 3, 't');
        c.Line(22, 25, 22, 8, 't', 2);
        c.Line(22, 8, 18, 2, 't');
        c.Line(22, 12, 28, 5, 't');
        c.Line(3, 15, 1, 17, 't');
        c.Line(42, 13, 44, 15, 't');
        c.Line(20, 40, 20, 58, 'T');
        c.Line(21, 26, 21, 39, 'T');
        // spooky face in the bark
        c.Rect(20, 44, 2, 2, 'x'); c.Rect(24, 44, 2, 2, 'x');
        c.Ellipse(23, 51, 2, 2, 'x');
        // a hanging rope swing... no, a hanging lantern
        c.Line(9, 19, 9, 24, 'i');
        c.Rect(8, 25, 3, 4, 'y');
        c.Set(8, 25, 'i'); c.Set(10, 25, 'i');
        return c;
    }
    case D_SKULL: {
        PixelCanvas c(11, 10);
        c.Ellipse(5, 4, 4, 3, 'k');
        c.Rect(3, 6, 5, 2, 'k');
        c.Rect(3, 3, 2, 2, 'x'); c.Rect(6, 3, 2, 2, 'x');
        c.Set(5, 6, 'x');
        c.Set(4, 8, 'K'); c.Set(6, 8, 'K');
        c.Line(8, 2, 8, 5, 'K');
        return c;
    }
    case D_CANDLE: {
        PixelCanvas c(9, 16);
        c.Rect(3, 6, 3, 8, 'c');
        c.Set(2, 8, 'c'); c.Set(2, 9, 'c'); c.Set(6, 10, 'c');
        c.Line(5, 6, 5, 13, 'K');
        c.Set(4, 5, 'x');
        c.Rect(1, 14, 7, 1, 'I');
        c.Rect(2, 13, 5, 1, 'i');
        return c;
    }
    case D_MUSHROOM: {
        PixelCanvas c(12, 10);
        c.Ellipse(6, 4, 4, 2, 'p');
        c.Rect(2, 4, 9, 1, 'p');
        c.Set(4, 3, 'P'); c.Set(7, 2, 'P'); c.Set(8, 4, 'P');
        c.Rect(5, 5, 2, 4, 'c');
        c.Set(6, 6, 'K');
        return c;
    }
    case D_PILLAR: {
        PixelCanvas c(18, 46);
        c.Rect(4, 7, 10, 35, 'g');
        for (int x : {6, 9, 12}) c.Line(x, 8, x, 40, 'G');
        c.Line(5, 8, 5, 40, 'L');
        c.Rect(2, 5, 14, 3, 'g');
        c.Line(2, 5, 15, 5, 'L');
        c.Rect(2, 41, 14, 4, 'G');
        c.Line(2, 41, 15, 41, 'g');
        // broken top
        c.Set(4, 5, '.'); c.Set(5, 5, '.'); c.Set(4, 6, '.');
        c.Rect(9, 2, 4, 3, 'g'); c.Set(12, 2, '.'); c.Set(10, 1, 'g');
        c.Line(9, 18, 11, 24, 'G');
        // ivy
        c.Line(13, 10, 11, 20, 'N'); c.Set(12, 13, 'n'); c.Set(10, 18, 'n'); c.Set(13, 9, 'n');
        return c;
    }
    case D_SCARECROW: {
        PixelCanvas c(22, 36);
        c.Rect(10, 10, 2, 26, 'w');
        c.Rect(2, 14, 18, 2, 'w');
        c.Rect(6, 13, 10, 10, 'r');
        c.Line(6, 17, 15, 17, 'o');
        c.Set(8, 15, 'y'); c.Set(13, 19, 'y');
        c.Rect(1, 15, 2, 2, 'y'); c.Rect(19, 15, 2, 2, 'y');
        c.Set(0, 16, 'y'); c.Set(21, 14, 'y');
        c.Ellipse(11, 7, 5, 4, 'o');
        c.Set(9, 6, 'x'); c.Set(13, 6, 'x');
        c.Line(9, 9, 13, 9, 'x');
        c.Rect(6, 2, 10, 2, 't');
        c.Rect(8, 0, 6, 2, 't');
        return c;
    }
    default: return PixelCanvas(1, 1);
    }
}

void World::Load()
{
    // tileset
    Image img = GenImageColor(G_COUNT * TILE, TILE, BLANK);
    PaintDirt(img, G_DIRT_A * TILE, 1);
    PaintDirt(img, G_DIRT_B * TILE, 2);
    PaintGrass(img, G_GRASS_A * TILE, 3);
    PaintGrass(img, G_GRASS_B * TILE, 4);
    PaintBrick(img, G_BRICK * TILE, false);
    PaintBrick(img, G_BRICK_MOSS * TILE, true);
    PaintPlatform(img, G_PLATFORM * TILE);
    Darken(img, G_DIRT_DEEP * TILE, G_DIRT_A * TILE, 0.30f);
    Darken(img, G_DIRT_DEEPER * TILE, G_DIRT_B * TILE, 0.55f);
    Darken(img, G_BRICK_DEEP * TILE, G_BRICK * TILE, 0.30f);
    Darken(img, G_BRICK_DEEPER * TILE, G_BRICK * TILE, 0.55f);
    PaintBone(img, G_BONE * TILE);
    tileset_ = LoadTextureFromImage(img);
    UnloadImage(img);

    // decoration atlas
    std::vector<PixelCanvas> pics;
    int totalW = 0, maxH = 0;
    for (int t = 0; t < D_COUNT; ++t) {
        pics.push_back(PaintDecor((DecorType)t));
        pics.back().Outline('O');
        totalW += pics.back().W() + 1;
        maxH = std::max(maxH, pics.back().H());
    }
    Image atlas = GenImageColor(totalW, maxH, BLANK);
    int ax = 0;
    for (int t = 0; t < D_COUNT; ++t) {
        pics[t].BakeInto(atlas, ax, 0, DecorPalette());
        decorRects_[t] = Rectangle{ (float)ax, 0, (float)pics[t].W(), (float)pics[t].H() };
        ax += pics[t].W() + 1;
    }
    decorAtlas_ = LoadTextureFromImage(atlas);
    UnloadImage(atlas);
}

void World::Unload()
{
    if (tileset_.id) UnloadTexture(tileset_);
    if (decorAtlas_.id) UnloadTexture(decorAtlas_);
    tileset_ = {};
    decorAtlas_ = {};
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
void DrawDitherDisc(int cx, int cy, int r, Color c, int phase)
{
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r * r && ((x + y + phase) & 1) == 0)
                DrawPixel(cx + x, cy + y, c);
}

void World::DrawTiles(Vector2 cam, int viewW, int viewH, float) const
{
    const int tx0 = std::max(0, (int)floorf(cam.x / TILE));
    const int ty0 = std::max(0, (int)floorf(cam.y / TILE));
    const int tx1 = std::min(W - 1, (int)((cam.x + viewW) / TILE) + 1);
    const int ty1 = std::min(H - 1, (int)((cam.y + viewH) / TILE) + 1);
    const int cx = (int)cam.x, cy = (int)cam.y;
    for (int ty = ty0; ty <= ty1; ++ty)
        for (int tx = tx0; tx <= tx1; ++tx) {
            const Tile t = At(tx, ty);
            if (t == T_EMPTY) continue;
            const bool openAbove = !Solid(tx, ty - 1);
            const uint32_t h = Hash2(tx, ty, 77);
            const int depth = ty - GroundTop(tx);
            int g = G_DIRT_A;
            if (t == T_DIRT) {
                if (openAbove) g = (h & 1) ? G_GRASS_A : G_GRASS_B;
                else if (depth >= 5) g = G_DIRT_DEEPER;
                else if (depth >= 3) g = G_DIRT_DEEP;
                else g = (h % 23 == 0) ? G_BONE : ((h & 2) ? G_DIRT_A : G_DIRT_B);
            } else if (t == T_BRICK) {
                g = openAbove ? G_BRICK_MOSS : (depth >= 5 ? G_BRICK_DEEPER : (depth >= 3 ? G_BRICK_DEEP : G_BRICK));
            }
            else if (t == T_PLATFORM) g = G_PLATFORM;
            const int sx = tx * TILE - cx, sy = ty * TILE - cy;
            DrawTextureRec(tileset_, Rectangle{ (float)(g * TILE), 0, TILE, TILE }, Vector2{ (float)sx, (float)sy }, WHITE);
            if (t != T_PLATFORM) {   // dark edges where the ground meets air -> chunky readable shapes
                if (!Solid(tx - 1, ty) && tx > 0) DrawRectangle(sx, sy, 1, TILE, amiga::pal::Black);
                if (!Solid(tx + 1, ty) && tx < W - 1) DrawRectangle(sx + TILE - 1, sy, 1, TILE, amiga::pal::Black);
            }
        }
}

void World::DrawDecor(Vector2 cam, int viewW, int viewH, float time) const
{
    const int cx = (int)cam.x, cy = (int)cam.y;
    for (const Decor& d : decor_) {
        const Rectangle r = decorRects_[d.type];
        const int sx = d.x - (int)(r.width / 2) - cx;
        const int sy = d.y - (int)r.height - cy + 1;
        if (sx > viewW || sx + r.width < 0 || sy > viewH || sy + r.height < 0) continue;

        const int flick = (int)(Hash2(d.x, (int)(time * 12)) % 3);
        if (d.type == D_PUMPKIN) DrawDitherDisc(sx + 9, sy + 8, 10 + (flick == 0), RGB12(0x520), d.x);
        if (d.type == D_MUSHROOM) DrawDitherDisc(sx + 6, sy + 4, 7, RGB12(0x314), d.x);
        if (d.type == D_CANDLE) DrawDitherDisc(sx + 4, sy + 3, 8 + (flick == 0), RGB12(0x431), d.x);

        DrawTextureRec(decorAtlas_, r, Vector2{ (float)sx, (float)sy }, WHITE);

        if (d.type == D_CANDLE) {   // flickering flame
            DrawPixel(sx + 4, sy + 4, RGB12(0xF80));
            DrawPixel(sx + 4, sy + 3, RGB12(0xFE4));
            DrawPixel(sx + 4 + (flick == 1 ? 1 : 0), sy + 2 - (flick == 2), RGB12(0xFFC));
        }
        if (d.type == D_PUMPKIN && flick == 0) {   // inner glow pulses
            DrawPixel(sx + 9, sy + 9, RGB12(0xFFC));
        }
    }
}

void World::DrawCandies(Vector2 cam, int viewW, int viewH, float time) const
{
    const int cx = (int)cam.x, cy = (int)cam.y;
    for (const Candy& c : candies_) {
        if (c.taken) continue;
        const int sx = (int)c.pos.x - cx;
        const int sy = (int)(c.pos.y + sinf(time * 4.0f + c.pos.x * 0.1f) * 2.0f) - cy;
        if (sx < -8 || sx > viewW + 8 || sy < -8 || sy > viewH + 8) continue;
        // candy corn: white tip, orange, yellow base - with black outline
        DrawRectangle(sx - 2, sy - 4, 3, 2, pal::Black);
        DrawRectangle(sx - 3, sy - 2, 5, 3, pal::Black);
        DrawRectangle(sx - 4, sy + 1, 7, 4, pal::Black);
        DrawPixel(sx - 1, sy - 3, pal::White);
        DrawRectangle(sx - 2, sy - 2, 3, 1, pal::White);
        DrawRectangle(sx - 2, sy - 1, 3, 1, pal::Orange);
        DrawRectangle(sx - 3, sy, 5, 2, pal::Orange);
        DrawRectangle(sx - 3, sy + 2, 5, 2, pal::Yellow);
        if (((int)(time * 6) + (int)c.pos.x) % 9 == 0) {   // sparkle
            DrawPixel(sx + 3, sy - 4, pal::White);
            DrawPixel(sx + 2, sy - 4, pal::Yellow);
            DrawPixel(sx + 4, sy - 4, pal::Yellow);
            DrawPixel(sx + 3, sy - 5, pal::Yellow);
            DrawPixel(sx + 3, sy - 3, pal::Yellow);
        }
    }
}

void World::DrawBats(Vector2 cam, int viewW, int viewH, float time) const
{
    const int cx = (int)cam.x, cy = (int)cam.y;
    const Color body = RGB12(0x101);
    for (const Bat& b : bats_) {
        const int x = (int)b.x - cx, y = (int)b.y - cy;
        if (x < -6 || x > viewW + 6 || y < -6 || y > viewH + 6) continue;
        const bool up = ((int)(time * 10 + b.phase * 3)) & 1;
        DrawRectangle(x - 1, y - 1, 3, 3, body);
        DrawPixel(x - 1, y - 2, body); DrawPixel(x + 1, y - 2, body);   // ears
        DrawPixel(b.speed > 0 ? x + 1 : x - 1, y, pal::Red);           // evil little eye
        if (up) {
            DrawLine(x - 2, y - 1, x - 5, y - 4, body); DrawLine(x + 2, y - 1, x + 5, y - 4, body);
            DrawPixel(x - 4, y - 2, body); DrawPixel(x + 4, y - 2, body);
        } else {
            DrawLine(x - 2, y, x - 5, y + 2, body); DrawLine(x + 2, y, x + 5, y + 2, body);
            DrawPixel(x - 3, y, body); DrawPixel(x + 3, y, body);
        }
    }
}
