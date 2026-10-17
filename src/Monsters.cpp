#include "Monsters.h"
#include "Amiga.h"
#include "PixelArt.h"
#include "World.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

using namespace amiga;

static constexpr int CELL_W = 26, CELL_H = 22;
static constexpr float M_GRAVITY = 900.0f;

// ----------------------------------------------------------------------------
// Setup
// ----------------------------------------------------------------------------
void MonsterCrowd::Init(const World& w)
{
    monsters_.clear();
    attackEvents_.clear();
    for (const MonsterSpawn& s : w.MonsterSpawns()) {
        Monster m;
        m.type = (MonsterType)s.type;
        m.home = m.pos = m.netTarget = s.pos;
        m.phase = (float)(monsters_.size() % 9) * 0.7f;
        m.facing = (monsters_.size() & 1) ? 1 : -1;
        monsters_.push_back(m);
    }
}

static PixelCanvas PaintMonster(MonsterType t, int f)
{
    switch (t) {
    case M_GHOST: {
        PixelCanvas c(17, 19);
        c.Ellipse(8, 7, 6, 6, 'w');
        c.Rect(2, 7, 13, 9, 'w');
        for (int x = 2; x <= 14; ++x)                      // wavy tail
            if (((x / 2) + f) % 2 == 0) { c.Set(x, 16, 'w'); c.Set(x, 17, 'W'); }
        c.Line(13, 4, 13, 15, 'W');
        c.Line(14, 7, 14, 15, 'W');
        c.Set(1, 9, 'w'); c.Set(0, 10 - f, 'w');           // little arms waving
        c.Set(15, 9, 'w'); c.Set(16, 9 + f, 'w');
        c.Rect(5, 5, 2, 3, 'x'); c.Rect(10, 5, 2, 3, 'x');
        c.Rect(7, 10, 3, 3, 'x'); c.Set(8, 11, 'm');       // "BOO!" mouth
        return c;
    }
    case M_SKELETON: {
        PixelCanvas c(13, 21);
        c.Ellipse(6, 3, 4, 3, 'k');
        c.Rect(4, 5, 5, 2, 'k');
        c.Set(4, 3, 'x'); c.Set(5, 3, 'x'); c.Set(7, 3, 'x'); c.Set(8, 3, 'x');
        c.Set(6, 5, 'x'); c.Set(5, 6, 'K'); c.Set(7, 6, 'K');
        c.Line(6, 7, 6, 13, 'K');
        c.Line(4, 8, 8, 8, 'k'); c.Line(4, 10, 8, 10, 'k'); c.Line(5, 12, 7, 12, 'k');
        c.Rect(4, 13, 5, 2, 'k');
        c.Line(3, 8, 2 + f, 13, 'k'); c.Line(9, 8, 10 - f, 13, 'k');
        if (f == 0) { c.Line(5, 15, 3, 20, 'k'); c.Line(7, 15, 9, 20, 'k'); c.Set(2, 20, 'K'); c.Set(10, 20, 'K'); }
        else        { c.Line(5, 15, 5, 20, 'k'); c.Line(7, 15, 7, 20, 'k'); c.Set(4, 20, 'K'); c.Set(8, 20, 'K'); }
        return c;
    }
    case M_ZOMBIE: {
        PixelCanvas c(14, 21);
        c.Rect(3, 0, 6, 6, 'g');
        c.Rect(3, 0, 6, 1, 'h'); c.Set(3, 1, 'h'); c.Set(8, 1, 'h');
        c.Set(4, 2, 'R'); c.Set(7, 2, 'R');
        c.Line(4, 4, 7, 4, 'x'); c.Set(5, 5, 'k');
        c.Rect(3, 6, 6, 7, 'b');
        c.Set(4, 11, 'g'); c.Set(7, 9, 'g'); c.Set(3, 12, '.');
        c.Rect(8, 7, 2, 2, 'b'); c.Rect(10, 7, 4, 2, 'g');      // arms out, reaching
        c.Rect(8, 10, 5, 1, 'g'); c.Set(13, 10 - f, 'g');
        c.Rect(3, 13, 6, 4, 'p');
        if (f == 0) { c.Rect(3, 17, 2, 3, 'p'); c.Rect(7, 17, 2, 3, 'p'); c.Rect(2, 20, 3, 1, 'x'); c.Rect(7, 20, 3, 1, 'x'); }
        else        { c.Rect(4, 17, 2, 3, 'p'); c.Rect(6, 17, 2, 3, 'p'); c.Rect(4, 20, 2, 1, 'x'); c.Rect(6, 20, 3, 1, 'x'); }
        return c;
    }
    case M_WITCH: {
        PixelCanvas c(25, 20);
        c.Line(5, 15, 23, 15, 'm'); c.Line(5, 16, 22, 16, 'M');  // broomstick
        c.Rect(0, 13, 6, 6, 'y');                                  // bristles
        for (int y = 13; y < 19; y += 2) c.Line(0, y, 5, y, 'Y');
        if (f) { c.Set(0, 12, 'y'); c.Set(1, 19, 'Y'); }
        c.Rect(9, 8, 6, 7, 'd');                                   // dress
        if (f == 0) c.Rect(5, 9, 4, 4, 'd'); else c.Rect(4, 10, 5, 3, 'd');   // cape fluttering
        c.Rect(12, 4, 4, 4, 'g');                                  // face
        c.Set(16, 6, 'g'); c.Set(17, 7, 'g');                      // long nose
        c.Set(14, 5, 'x');
        c.Rect(10, 5, 2, 5, 'r');                                  // orange hair
        c.Line(9, 4, 18, 4, 'd');                                  // hat brim
        c.Rect(11, 1, 5, 3, 'd');
        c.Set(10, 0, 'd'); c.Set(9, 0, 'd'); c.Set(11, 0, 'd');
        c.Line(11, 3, 15, 3, 'P');                                 // hat band
        c.Line(14, 10, 18, 14, 'd'); c.Set(19, 14, 'g');           // arm holding the broom
        c.Rect(13, 17, 3, 2, 'x');                                 // boots
        return c;
    }
    default: return PixelCanvas(1, 1);
    }
}

void MonsterCrowd::Load()
{
    static const std::map<char, Color> palette = {
        { 'O', RGB12(0x000) }, { 'x', RGB12(0x000) }, { 'w', RGB12(0xEEF) }, { 'W', RGB12(0xAAC) },
        { 'm', RGB12(0x852) }, { 'M', RGB12(0x531) }, { 'k', RGB12(0xEED) }, { 'K', RGB12(0xAA9) },
        { 'g', RGB12(0x7B5) }, { 'h', RGB12(0x243) }, { 'R', RGB12(0xF22) }, { 'b', RGB12(0x668) },
        { 'p', RGB12(0x534) }, { 'y', RGB12(0xFD5) }, { 'Y', RGB12(0xC93) }, { 'd', RGB12(0x314) },
        { 'P', RGB12(0xA4E) }, { 'r', RGB12(0xF60) },
    };
    Image img = GenImageColor(CELL_W * 2, CELL_H * M_COUNT, BLANK);
    for (int t = 0; t < M_COUNT; ++t)
        for (int f = 0; f < 2; ++f) {
            PixelCanvas c = PaintMonster((MonsterType)t, f);
            c.Outline('O');
            c.BakeInto(img, f * CELL_W + (CELL_W - c.W()) / 2, t * CELL_H + CELL_H - c.H(), palette);
        }
    sheet_ = LoadTextureFromImage(img);
    UnloadImage(img);
}

void MonsterCrowd::Unload()
{
    if (sheet_.id) UnloadTexture(sheet_);
    sheet_ = {};
}

Rectangle MonsterCrowd::Body(const Monster& m)
{
    switch (m.type) {
    case M_GHOST: return Rectangle{ m.pos.x - 6, m.pos.y - 17, 12, 15 };
    case M_WITCH: return Rectangle{ m.pos.x - 9, m.pos.y - 16, 18, 13 };
    default:      return Rectangle{ m.pos.x - 4, m.pos.y - 19, 8, 19 };
    }
}

const char* MonsterCrowd::Name(MonsterType t)
{
    switch (t) {
    case M_GHOST: return "GHOST";
    case M_SKELETON: return "SKELETON";
    case M_ZOMBIE: return "ZOMBIE";
    default: return "WITCH";
    }
}

// ----------------------------------------------------------------------------
// Host simulation
// ----------------------------------------------------------------------------
void MonsterCrowd::Walk(Monster& m, const World& w, float dt, float speed)
{
    static constexpr float HW = 5.0f, HT = 18.0f;
    m.vel.x = m.facing * speed;
    m.vel.y = fminf(m.vel.y + M_GRAVITY * dt, 400.0f);

    // turn around at walls and at edges (they don't jump into pits)
    m.pos.x += m.vel.x * dt;
    {
        const int top = (int)floorf((m.pos.y - HT) / TILE), bot = (int)floorf((m.pos.y - 0.01f) / TILE);
        const int tx = (int)floorf((m.pos.x + m.facing * HW) / TILE);
        for (int ty = top; ty <= bot; ++ty)
            if (w.Solid(tx, ty)) {
                m.pos.x = m.facing > 0 ? tx * TILE - HW - 0.01f : (tx + 1) * TILE + HW + 0.01f;
                m.facing = -m.facing;
                break;
            }
    }
    if (m.onGround) {
        const int tx = (int)floorf((m.pos.x + m.facing * (HW + 2)) / TILE);
        const int ty = (int)floorf(m.pos.y / TILE);
        if (!w.Solid(tx, ty) && !w.Platform(tx, ty)) m.facing = -m.facing;
    }

    const float oldBottom = m.pos.y;
    m.pos.y += m.vel.y * dt;
    m.onGround = false;
    if (m.vel.y >= 0) {
        const int ty = (int)floorf(m.pos.y / TILE);
        for (int tx = (int)floorf((m.pos.x - HW) / TILE); tx <= (int)floorf((m.pos.x + HW - 0.01f) / TILE); ++tx)
            if ((w.Solid(tx, ty) && ty >= 0) || (w.Platform(tx, ty) && oldBottom <= ty * TILE + 0.5f)) {
                m.pos.y = (float)(ty * TILE);
                m.vel.y = 0;
                m.onGround = true;
                break;
            }
    }
    if (m.pos.y > w.PixelH() + 30) { m.pos = m.home; m.vel = {}; }
}

void MonsterCrowd::Simulate(const World& w, float dt, float time, const std::vector<Vector2>& targets)
{
    for (int i = 0; i < (int)monsters_.size(); ++i) {
        Monster& m = monsters_[i];
        if (m.pending > 0) m.pending -= dt;
        if (m.defeated) continue;

        // the nearest cat
        const Vector2* near = nullptr;
        float best = 1e9f;
        for (const Vector2& t : targets) {
            const float d = fabsf(t.x - m.pos.x) + fabsf(t.y - m.pos.y) * 0.5f;
            if (d < best) { best = d; near = &t; }
        }

        switch (m.type) {
        case M_SKELETON: {
            // patrols back and forth, rattling
            if (m.pos.x > m.home.x + 130) m.facing = -1;
            if (m.pos.x < m.home.x - 130) m.facing = 1;
            Walk(m, w, dt, 28.0f);
            break;
        }
        case M_ZOMBIE: {
            // shuffles around, but smells cats from afar and slowly comes for them
            const bool hunting = near && fabsf(near->x - m.pos.x) < 220.0f && fabsf(near->y - m.pos.y) < 60.0f;
            if (hunting) {
                if (fabsf(near->x - m.pos.x) > 4) m.facing = near->x < m.pos.x ? -1 : 1;
            } else {
                if (m.pos.x > m.home.x + 90) m.facing = -1;
                if (m.pos.x < m.home.x - 90) m.facing = 1;
            }
            Walk(m, w, dt, hunting ? 30.0f : 14.0f);
            break;
        }
        case M_GHOST: {
            // drifts through anything; fades out for a moment every few seconds
            m.faded = fmodf(time + m.phase * 7.0f, 7.0f) > 5.4f;
            Vector2 goal{ m.home.x + sinf(time * 0.4f + m.phase) * 70.0f, m.home.y + sinf(time * 0.9f + m.phase) * 14.0f };
            float speed = 22.0f;
            if (near && fabsf(near->x - m.pos.x) < 200.0f && fabsf(near->y - m.pos.y) < 120.0f) {
                goal = Vector2{ near->x, near->y + 2 };
                speed = 30.0f;
            }
            const float dx = goal.x - m.pos.x, dy = goal.y - m.pos.y;
            const float len = sqrtf(dx * dx + dy * dy);
            m.vel = len > 1 ? Vector2{ dx / len * speed, dy / len * speed } : Vector2{};
            m.pos.x += m.vel.x * dt;
            m.pos.y += m.vel.y * dt;
            if (fabsf(m.vel.x) > 2) m.facing = m.vel.x < 0 ? -1 : 1;
            break;
        }
        case M_WITCH: {
            if (m.timer > 0) m.timer -= dt;
            if (m.diving) {
                m.pos.x += m.vel.x * dt;
                m.pos.y += m.vel.y * dt;
                const bool hitGround = w.Solid((int)floorf(m.pos.x / TILE), (int)floorf((m.pos.y - 2) / TILE));
                if (m.timer <= 0 || hitGround) { m.diving = false; m.timer = 2.5f; }
            } else {
                // cruise back and forth high up, climb back after a dive
                if (m.pos.x > m.home.x + 170) m.facing = -1;
                if (m.pos.x < m.home.x - 170) m.facing = 1;
                m.vel.x = m.facing * 55.0f;
                m.pos.x += m.vel.x * dt;
                const float wantY = m.home.y + sinf(time * 2.0f + m.phase) * 6.0f;
                m.pos.y += std::clamp(wantY - m.pos.y, -60.0f * dt, 60.0f * dt);
                // a cat right below? swoop!
                if (m.timer <= 0 && near && fabsf(near->x - m.pos.x) < 50.0f && near->y > m.pos.y + 20 && near->y - m.pos.y < 200) {
                    const float dx = near->x - m.pos.x, dy = near->y - 4 - m.pos.y;
                    const float len = sqrtf(dx * dx + dy * dy);
                    m.vel = Vector2{ dx / len * 140.0f, dy / len * 140.0f };
                    m.facing = dx < 0 ? -1 : 1;
                    m.diving = true;
                    m.timer = 1.2f;
                    attackEvents_.push_back(i);
                }
            }
            break;
        }
        default: break;
        }
    }
}

// ----------------------------------------------------------------------------
// Networking
// ----------------------------------------------------------------------------
std::string MonsterCrowd::Serialize() const
{
    // count, then per monster: x y flags (1 = faded, 2 = on ground, 4 = facing left, 8 = diving)
    std::string s = std::to_string(monsters_.size());
    char buf[40];
    for (const Monster& m : monsters_) {
        const int flags = (m.faded ? 1 : 0) | (m.onGround ? 2 : 0) | (m.facing < 0 ? 4 : 0) | (m.diving ? 8 : 0);
        std::snprintf(buf, sizeof buf, " %d %d %d", (int)lrintf(m.pos.x), (int)lrintf(m.pos.y), flags);
        s += buf;
    }
    return s;
}

bool MonsterCrowd::ApplyRemote(const char* payload)
{
    char* end = nullptr;
    const long n = std::strtol(payload, &end, 10);
    if (end == payload || n != (long)monsters_.size()) return false;
    const char* cur = end;
    for (Monster& m : monsters_) {
        long v[3];
        for (long& x : v) {
            x = std::strtol(cur, &end, 10);
            if (end == cur) return false;
            cur = end;
        }
        const Vector2 t{ (float)v[0], (float)v[1] };
        const bool diving = (v[2] & 8) != 0;
        if (diving && !m.diving && !m.defeated) attackEvents_.push_back((int)(&m - monsters_.data()));
        if (fabsf(t.x - m.pos.x) > 64.0f || fabsf(t.y - m.pos.y) > 64.0f) m.pos = t;
        m.netTarget = t;
        m.faded = (v[2] & 1) != 0;
        m.onGround = (v[2] & 2) != 0;
        m.facing = (v[2] & 4) ? -1 : 1;
        m.diving = diving;
    }
    return true;
}

void MonsterCrowd::TickRemote(float dt)
{
    for (Monster& m : monsters_) {
        if (m.pending > 0) m.pending -= dt;
        m.pos.x += (m.netTarget.x - m.pos.x) * 0.35f;
        m.pos.y += (m.netTarget.y - m.pos.y) * 0.35f;
    }
}

void MonsterCrowd::Respawn(int idx)
{
    if (idx < 0 || idx >= (int)monsters_.size()) return;
    Monster& m = monsters_[idx];
    m.defeated = false;
    m.diving = false;
    m.faded = false;
    m.pos = m.netTarget = m.home;
    m.vel = {};
    m.timer = 2.0f;
    m.pending = 0;
}

void MonsterCrowd::ResetAll()
{
    for (int i = 0; i < (int)monsters_.size(); ++i) Respawn(i);
    attackEvents_.clear();
}

std::vector<int> MonsterCrowd::TakeAttackEvents()
{
    std::vector<int> out;
    out.swap(attackEvents_);
    return out;
}

// ----------------------------------------------------------------------------
// Drawing
// ----------------------------------------------------------------------------
void MonsterCrowd::Draw(Vector2 cam, int viewW, int viewH, float time) const
{
    const int cx = (int)cam.x, cy = (int)cam.y;
    for (const Monster& m : monsters_) {
        if (m.defeated) continue;
        const int bx = (int)floorf(m.pos.x) - cx, by = (int)floorf(m.pos.y) - cy;
        if (bx < -30 || bx > viewW + 30 || by < -30 || by > viewH + 30) continue;
        const int idx = (int)(&m - monsters_.data());

        int frame = 0;
        Color tint = WHITE;
        switch (m.type) {
        case M_GHOST:
            frame = ((int)(time * 4) + idx) & 1;
            tint = m.faded ? Color{ 255, 255, 255, (unsigned char)(((int)(time * 20) & 1) ? 50 : 0) } : Color{ 255, 255, 255, 220 };
            if (!m.faded) DrawDitherDisc(bx, by - 9, 11, RGB12(0x214), idx);   // eerie glow
            break;
        case M_WITCH:
            frame = ((int)(time * 6) + idx) & 1;
            if (m.diving)   // magic sparkles trailing behind the broom
                for (int k = 0; k < 4; ++k)
                    DrawPixel(bx - m.facing * (12 + k * 4) + GetRandomValue(-1, 1), by - 6 + GetRandomValue(-3, 3),
                              k & 1 ? RGB12(0xC8F) : pal::Yellow);
            break;
        default:
            frame = ((int)(fabsf(m.pos.x) / 6.0f)) & 1;
            break;
        }
        const Rectangle src{ (float)(frame * CELL_W), (float)(m.type * CELL_H), (float)(m.facing < 0 ? -CELL_W : CELL_W), (float)CELL_H };
        DrawTextureRec(sheet_, src, Vector2{ (float)(bx - CELL_W / 2), (float)(by - CELL_H + 1) }, tint);
    }
}
