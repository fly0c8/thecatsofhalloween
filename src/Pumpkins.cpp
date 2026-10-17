#include "Pumpkins.h"
#include "Amiga.h"
#include "PixelArt.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace amiga;

static constexpr float P_GRAVITY  = 820.0f;
static constexpr float P_MAX_FALL = 400.0f;
static constexpr int FRAME_W = 18, FRAME_H = 15;

static float Rand(float lo, float hi) { return lo + (hi - lo) * (GetRandomValue(0, 10000) / 10000.0f); }

// ----------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------
void PumpkinPatch::Init(const World& w)
{
    pumpkins_.clear();
    angerEvents_.clear();
    for (const Vector2& h : w.PumpkinHomes()) {
        Pumpkin p;
        p.home = p.pos = p.netTarget = h;
        p.hopTimer = 0.3f + (float)(pumpkins_.size() % 7) * 0.21f;   // don't all hop in sync
        p.facing = (pumpkins_.size() & 1) ? 1 : -1;
        pumpkins_.push_back(p);
    }
    for (const Vector2& h : w.SittingPumpkins()) {
        Pumpkin p;
        p.home = p.pos = p.netTarget = h;
        p.sitter = true;
        p.facing = (pumpkins_.size() & 1) ? 1 : -1;
        pumpkins_.push_back(p);
    }
}

static PixelCanvas PaintPumpkin(bool angry)
{
    PixelCanvas c(FRAME_W, FRAME_H);
    const char body = angry ? 'R' : 'o', ridge = angry ? 'D' : 'r', face = angry ? 'Y' : 'y';
    // stem and leaf
    c.Rect(8, 1, 2, 3, 'N');
    c.Set(10, 2, 'n'); c.Set(11, 2, 'n'); c.Set(11, 3, 'n');
    if (angry) { c.Set(7, 0, 'N'); c.Set(10, 0, 'N'); }       // ruffled stem
    // body with ridges
    c.Ellipse(9, 9, 7, 5, body);
    for (int y = 5; y <= 13; ++y) { c.Set(5, y, ridge); c.Set(13, y, ridge); }
    c.Line(2, 8, 2, 11, ridge);
    c.Line(16, 8, 16, 11, ridge);
    c.Set(6, 5, 'h'); c.Set(7, 5, 'h'); c.Set(4, 7, 'h');    // shine
    if (!angry) {
        // a friendly, goofy carved face
        c.Set(6, 7, face); c.Set(5, 8, face); c.Set(6, 8, face); c.Set(7, 8, face);
        c.Set(12, 7, face); c.Set(11, 8, face); c.Set(12, 8, face); c.Set(13, 8, face);
        c.Set(9, 9, face);
        for (int x = 5; x <= 13; ++x) c.Set(x, 10, face);
        for (int x = 6; x <= 12; ++x) c.Set(x, 11, face);
        c.Set(7, 10, body); c.Set(11, 10, body);               // two goofy teeth gaps
        c.Set(5, 10, ridge); c.Set(13, 10, ridge);
    } else {
        // slanted, furious eyes and a jagged snarl
        c.Set(4, 6, face); c.Set(5, 7, face); c.Set(6, 7, face); c.Set(6, 8, face); c.Set(7, 8, face);
        c.Set(14, 6, face); c.Set(13, 7, face); c.Set(12, 7, face); c.Set(12, 8, face); c.Set(11, 8, face);
        for (int x = 5; x <= 13; ++x) { c.Set(x, 10, face); c.Set(x, 11, face); }
        for (int x = 6; x <= 12; ++x) c.Set(x, 12, face);
        for (int x = 6; x <= 12; x += 2) c.Set(x, 10, ridge);  // fangs from above
        for (int x = 7; x <= 11; x += 2) c.Set(x, 12, ridge);  // fangs from below
    }
    c.Outline('O');
    return c;
}

void PumpkinPatch::Load()
{
    static const std::map<char, Color> palette = {
        { 'O', RGB12(0x000) }, { 'o', RGB12(0xF80) }, { 'r', RGB12(0xB40) }, { 'y', RGB12(0xFE5) },
        { 'h', RGB12(0xFC7) }, { 'N', RGB12(0x262) }, { 'n', RGB12(0x4A2) },
        { 'R', RGB12(0xD31) }, { 'D', RGB12(0x710) }, { 'Y', RGB12(0xFF6) },
    };
    Image img = GenImageColor(FRAME_W * 2, FRAME_H, BLANK);
    PaintPumpkin(false).BakeInto(img, 0, 0, palette);
    PixelCanvas angry = PaintPumpkin(true);
    angry.Replace('h', 'o');                                   // angry ones get a hot orange shine
    static std::map<char, Color> angryPal = palette;
    angryPal['o'] = RGB12(0xF63);
    angry.BakeInto(img, FRAME_W, 0, angryPal);
    sheet_ = LoadTextureFromImage(img);
    UnloadImage(img);
}

void PumpkinPatch::Unload()
{
    if (sheet_.id) UnloadTexture(sheet_);
    sheet_ = {};
}

// ----------------------------------------------------------------------------
// Host simulation
// ----------------------------------------------------------------------------
void PumpkinPatch::MoveAndCollide(Pumpkin& p, const World& w, float dt)
{
    p.vel.y = fminf(p.vel.y + P_GRAVITY * dt, P_MAX_FALL);

    // X axis
    p.pos.x += p.vel.x * dt;
    {
        const int top = (int)floorf((p.pos.y - HEIGHT) / TILE);
        const int bot = (int)floorf((p.pos.y - 0.01f) / TILE);
        if (p.vel.x > 0) {
            const int tx = (int)floorf((p.pos.x + HALF_W) / TILE);
            for (int ty = top; ty <= bot; ++ty)
                if (w.Solid(tx, ty)) { p.pos.x = tx * TILE - HALF_W - 0.01f; p.vel.x = 0; break; }
        } else if (p.vel.x < 0) {
            const int tx = (int)floorf((p.pos.x - HALF_W) / TILE);
            for (int ty = top; ty <= bot; ++ty)
                if (w.Solid(tx, ty)) { p.pos.x = (tx + 1) * TILE + HALF_W + 0.01f; p.vel.x = 0; break; }
        }
    }

    // Y axis (lands on ground and on one-way platforms)
    const float oldBottom = p.pos.y;
    const bool wasOnGround = p.onGround;
    p.pos.y += p.vel.y * dt;
    p.onGround = false;
    const int tx0 = (int)floorf((p.pos.x - HALF_W) / TILE);
    const int tx1 = (int)floorf((p.pos.x + HALF_W - 0.01f) / TILE);
    if (p.vel.y >= 0) {
        const int ty = (int)floorf(p.pos.y / TILE);
        for (int tx = tx0; tx <= tx1; ++tx) {
            const bool solid = w.Solid(tx, ty) && ty >= 0;
            const bool plat = w.Platform(tx, ty) && oldBottom <= ty * TILE + 0.5f;
            if (solid || plat) {
                p.pos.y = (float)(ty * TILE);
                p.vel.y = 0;
                p.onGround = true;
                break;
            }
        }
    } else {
        const int ty = (int)floorf((p.pos.y - HEIGHT) / TILE);
        for (int tx = tx0; tx <= tx1; ++tx)
            if (w.Solid(tx, ty) && ty >= 0) { p.pos.y = (ty + 1) * TILE + HEIGHT; p.vel.y = 0; break; }
    }
    if (p.onGround && !wasOnGround) p.squash = 0.14f;

    // fell into a pit: it pops back up at home
    if (p.pos.y > w.PixelH() + 30) {
        p.pos = p.home;
        p.vel = {};
        p.onGround = true;
    }
}

void PumpkinPatch::Simulate(const World& w, float dt, const std::vector<Vector2>& targets)
{
    for (Pumpkin& p : pumpkins_) {
        if (p.pending > 0) p.pending -= dt;
        if (p.squash > 0) p.squash -= dt;
        if (p.smashed) continue;

        if (p.onGround) {
            // stick to the ground between hops
            const float f = 900.0f * dt;
            p.vel.x = fabsf(p.vel.x) <= f ? 0.0f : p.vel.x - f * (p.vel.x > 0 ? 1.0f : -1.0f);
            p.hopTimer -= dt;
            // a calm sitter just sits at home (after a chase it hops back first)
            const bool lazy = p.sitter && !p.angry && fabsf(p.pos.x - p.home.x) < 20.0f;
            if (lazy) p.hopTimer = fmaxf(p.hopTimer, 0.5f);
            if (p.hopTimer <= 0) {
                if (p.angry) {
                    // chase the nearest cat
                    float best = 1e9f;
                    int dir = p.facing;
                    for (const Vector2& t : targets) {
                        const float d = fabsf(t.x - p.pos.x) + fabsf(t.y - p.pos.y) * 0.5f;
                        if (d < best) { best = d; dir = t.x < p.pos.x ? -1 : 1; }
                    }
                    if (targets.empty()) dir = GetRandomValue(0, 1) ? 1 : -1;
                    p.facing = dir;
                    p.vel.x = dir * Rand(95.0f, 135.0f);
                    p.vel.y = -Rand(230.0f, 300.0f);
                    // a wall right ahead? jump higher
                    const int tx = (int)floorf((p.pos.x + dir * (HALF_W + 10)) / TILE);
                    const int ty = (int)floorf((p.pos.y - 1) / TILE);
                    if (w.Solid(tx, ty) || w.Solid(tx, ty - 1)) p.vel.y = -360.0f;
                    p.hopTimer = Rand(0.10f, 0.30f);
                } else {
                    // wander around home with little hops
                    int dir = GetRandomValue(0, 1) ? 1 : -1;
                    const float roam = p.sitter ? 0.0f : 40.0f;
                    if (p.pos.x > p.home.x + roam) dir = -1;
                    if (p.pos.x < p.home.x - roam) dir = 1;
                    p.facing = dir;
                    p.vel.x = dir * Rand(25.0f, 60.0f);
                    p.vel.y = -Rand(150.0f, 270.0f);
                    if (p.sitter) p.vel.x = dir * std::clamp(fabsf(p.home.x - p.pos.x) * 1.6f, 20.0f, 60.0f);   // aim for home
                    // don't hop into a pit on purpose
                    const int tx = (int)floorf((p.pos.x + dir * 24) / TILE);
                    if (w.GroundTop(tx) >= World::H) { p.vel.x = -p.vel.x; p.facing = -dir; }
                    p.hopTimer = Rand(0.5f, 1.8f);
                }
                p.onGround = false;
            }
        }
        MoveAndCollide(p, w, dt);
    }
}

// ----------------------------------------------------------------------------
// Networking
// ----------------------------------------------------------------------------
std::string PumpkinPatch::Serialize() const
{
    // count, then per pumpkin: x y flags (1 = angry, 2 = on ground, 4 = facing left)
    std::string s = std::to_string(pumpkins_.size());
    char buf[40];
    for (const Pumpkin& p : pumpkins_) {
        const int flags = (p.angry ? 1 : 0) | (p.onGround ? 2 : 0) | (p.facing < 0 ? 4 : 0);
        std::snprintf(buf, sizeof buf, " %d %d %d", (int)lrintf(p.pos.x), (int)lrintf(p.pos.y), flags);
        s += buf;
    }
    return s;
}

bool PumpkinPatch::ApplyRemote(const char* payload)
{
    char* end = nullptr;
    const long n = std::strtol(payload, &end, 10);
    if (end == payload || n != (long)pumpkins_.size()) return false;   // different world?
    const char* cur = end;
    for (Pumpkin& p : pumpkins_) {
        long v[3];
        for (long& x : v) {
            x = std::strtol(cur, &end, 10);
            if (end == cur) return false;
            cur = end;
        }
        const Vector2 t{ (float)v[0], (float)v[1] };
        const bool angry = (v[2] & 1) != 0, ground = (v[2] & 2) != 0;
        if (angry && !p.angry && !p.smashed) angerEvents_.push_back((int)(&p - pumpkins_.data()));
        if (ground && !p.onGround) p.squash = 0.14f;
        if (fabsf(t.x - p.pos.x) > 64.0f || fabsf(t.y - p.pos.y) > 64.0f) p.pos = t;   // respawned / teleported
        p.netTarget = t;
        p.angry = angry;
        p.onGround = ground;
        p.facing = (v[2] & 4) ? -1 : 1;
    }
    return true;
}

void PumpkinPatch::TickRemote(float dt)
{
    for (Pumpkin& p : pumpkins_) {
        if (p.pending > 0) p.pending -= dt;
        if (p.squash > 0) p.squash -= dt;
        p.pos.x += (p.netTarget.x - p.pos.x) * 0.35f;
        p.pos.y += (p.netTarget.y - p.pos.y) * 0.35f;
    }
}

// ----------------------------------------------------------------------------
// Moods
// ----------------------------------------------------------------------------
void PumpkinPatch::MakeAngry(int idx)
{
    if (idx < 0 || idx >= (int)pumpkins_.size()) return;
    Pumpkin& p = pumpkins_[idx];
    if (p.angry || p.smashed) return;
    p.angry = true;
    p.hopTimer = 0.4f;
    angerEvents_.push_back(idx);
}

void PumpkinPatch::CalmAll()
{
    for (Pumpkin& p : pumpkins_) {
        if (!p.angry) continue;
        p.angry = false;
        p.hopTimer = Rand(0.8f, 1.6f);   // a moment of confusion, then they hop back home
    }
}

void PumpkinPatch::Respawn(int idx)
{
    if (idx < 0 || idx >= (int)pumpkins_.size()) return;
    Pumpkin& p = pumpkins_[idx];
    p.smashed = false;
    p.angry = false;
    p.pos = p.netTarget = p.home;
    p.vel = {};
    p.onGround = true;
    p.hopTimer = Rand(0.5f, 1.5f);
    p.pending = 0;
}

void PumpkinPatch::ResetAll()
{
    for (int i = 0; i < (int)pumpkins_.size(); ++i) Respawn(i);
    angerEvents_.clear();
}

int PumpkinPatch::AngryCount() const
{
    int n = 0;
    for (const Pumpkin& p : pumpkins_) n += (p.angry && !p.smashed) ? 1 : 0;
    return n;
}

std::vector<int> PumpkinPatch::TakeAngerEvents()
{
    std::vector<int> out;
    out.swap(angerEvents_);
    return out;
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
void PumpkinPatch::Draw(Vector2 cam, int viewW, int viewH, float time) const
{
    const int cx = (int)cam.x, cy = (int)cam.y;
    for (const Pumpkin& p : pumpkins_) {
        if (p.smashed) continue;
        const int bx = (int)floorf(p.pos.x) - cx, by = (int)floorf(p.pos.y) - cy;
        if (bx < -20 || bx > viewW + 20 || by < -20 || by > viewH + 24) continue;
        const int idx = (int)(&p - pumpkins_.data());

        // glow behind the carved face
        const int flick = (int)(Hash2(idx, (int)(time * 12)) % 3);
        DrawDitherDisc(bx, by - 7, 10 + (flick == 0), p.angry ? RGB12(0x501) : RGB12(0x420), idx);

        // squash on landing, stretch while rising
        float sx = 1.0f, sy = 1.0f;
        if (p.squash > 0) { sx = 1.25f; sy = 0.75f; }
        else if (!p.onGround && p.vel.y < -120.0f) { sx = 0.9f; sy = 1.12f; }
        // angry ones tremble with rage
        const int shakeX = p.angry && p.onGround ? (((int)(time * 30) + idx) % 3) - 1 : 0;
        const float w = FRAME_W * sx, h = FRAME_H * sy;
        const Rectangle src{ (float)(p.angry ? FRAME_W : 0), 0, (float)(p.facing < 0 ? -FRAME_W : FRAME_W), (float)FRAME_H };
        const Rectangle dst{ floorf(bx - w / 2) + shakeX, floorf(by - h) + 1, floorf(w), floorf(h) };
        DrawTexturePro(sheet_, src, dst, Vector2{ 0, 0 }, 0, WHITE);

        if (p.angry) {
            // steam puffing out of the stem
            for (int k = 0; k < 2; ++k) {
                const float t = fmodf(time * 1.8f + k * 0.5f + idx * 0.37f, 1.0f);
                const int px = (int)dst.x + 9 + (k ? 3 : -3) + (int)(sinf(t * 9.0f) * 1.5f);
                const int py = (int)dst.y - 1 - (int)(t * 9.0f);
                DrawPixel(px, py, t < 0.5f ? RGB12(0xCCC) : RGB12(0x777));
            }
        } else if (flick == 0) {
            DrawPixel((int)dst.x + 9, (int)dst.y + 9, RGB12(0xFFC));   // the candle inside flickers
        }
    }
}
