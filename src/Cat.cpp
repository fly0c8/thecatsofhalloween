#include "Cat.h"
#include "Amiga.h"
#include "World.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

// Tuned in "pixels per second" for a 320x256 screen.
static constexpr float GRAVITY      = 950.0f;
static constexpr float MAX_FALL     = 420.0f;
static constexpr float RUN_ACCEL    = 1000.0f;
static constexpr float AIR_ACCEL    = 650.0f;
static constexpr float MAX_RUN      = 112.0f;
static constexpr float FRICTION     = 1100.0f;
static constexpr float JUMP_VEL     = 345.0f;
static constexpr float JUMP_CUT     = 0.45f;
static constexpr float COYOTE_TIME  = 0.09f;
static constexpr float JUMP_BUFFER  = 0.12f;

void Cat::Reset(Vector2 at)
{
    pos = at;
    vel = {};
    onGround = false;
    hp = 9;
    ko = false;
    koTimer = 0;
    hurtTimer = 0;
    invuln = 1.5f;
    attackTimer = 0;
    lastSafe = at;
}

Rectangle Cat::AttackBox() const
{
    const float x = facing > 0 ? pos.x + 2 : pos.x - 22;
    return Rectangle{ x, pos.y - 18, 20, 14 };
}

void Cat::Hurt(float fromX, int damage)
{
    if (invuln > 0 || ko) return;
    hp -= damage;
    const float dir = pos.x >= fromX ? 1.0f : -1.0f;
    vel.x = dir * 170.0f;
    vel.y = -190.0f;
    onGround = false;
    hurtTimer = 0.35f;
    invuln = 1.1f;
    flash = 0.12f;
    attackTimer = 0;
    if (hp <= 0) {
        hp = 0;
        ko = true;
        koTimer = 2.8f;
        vel.y = -260.0f;
    }
}

CatEvents Cat::Update(const PlayerInput& rawIn, const World& w, float dt)
{
    CatEvents ev;
    animTime += dt;
    blinkTimer += dt;
    if (invuln > 0) invuln -= dt;
    if (flash > 0) flash -= dt;
    if (attackCooldown > 0) attackCooldown -= dt;
    if (dropThrough > 0) dropThrough -= dt;

    PlayerInput in = rawIn;
    if (ko || hurtTimer > 0) in = PlayerInput{};   // no control while stunned or knocked out
    if (hurtTimer > 0) hurtTimer -= dt;

    // ---- horizontal movement -------------------------------------------------
    const float dir = (in.right ? 1.0f : 0.0f) - (in.left ? 1.0f : 0.0f);
    const float accel = onGround ? RUN_ACCEL : AIR_ACCEL;
    if (dir != 0 && attackTimer <= 0.0f) {
        vel.x += dir * accel * dt;
        facing = dir > 0 ? 1 : -1;
    } else if (onGround) {
        const float f = FRICTION * dt;
        vel.x = fabsf(vel.x) <= f ? 0.0f : vel.x - f * (vel.x > 0 ? 1.0f : -1.0f);
    }
    const float maxRun = (attackTimer > 0 && onGround) ? MAX_RUN * 0.4f : MAX_RUN;
    if (hurtTimer <= 0 && !ko) vel.x = fmaxf(-maxRun, fminf(maxRun, vel.x));

    // ---- jumping (with coyote time + buffering) --------------------------------
    const bool jumpPressed = in.jump && !jumpWasDown;
    jumpWasDown = in.jump;
    if (jumpPressed) jumpBuffer = JUMP_BUFFER; else if (jumpBuffer > 0) jumpBuffer -= dt;
    if (onGround) coyote = COYOTE_TIME; else if (coyote > 0) coyote -= dt;

    if (in.down && onGround && jumpPressed) {
        // down + jump on a platform = drop through it
        const int ty = (int)floorf(pos.y / TILE);
        if (w.Platform((int)floorf(pos.x / TILE), ty) || w.Platform((int)floorf((pos.x - HALF_W) / TILE), ty) ||
            w.Platform((int)floorf((pos.x + HALF_W - 0.01f) / TILE), ty)) {
            dropThrough = 0.25f;
            onGround = false;
            jumpBuffer = 0;
            pos.y += 1;
        }
    }
    if (jumpBuffer > 0 && coyote > 0) {
        vel.y = -JUMP_VEL;
        jumpBuffer = 0;
        coyote = 0;
        onGround = false;
        ev.jumped = true;
    }
    if (!in.jump && vel.y < 0 && !ko && hurtTimer <= 0) vel.y *= powf(JUMP_CUT, dt * 20.0f) ; // short hops

    // ---- attack ------------------------------------------------------------------------
    const bool attackPressed = in.attack && !attackWasDown;
    attackWasDown = in.attack;
    if (attackPressed && attackCooldown <= 0 && !ko) {
        attackTimer = ATTACK_TIME;
        attackCooldown = ATTACK_TIME + 0.12f;
        attackLanded = false;
        ev.attacked = true;
    }
    if (attackTimer > 0) attackTimer -= dt;

    // ---- gravity ---------------------------------------------------------------------
    vel.y = fminf(vel.y + GRAVITY * dt, MAX_FALL);

    // ---- move & collide: X axis -----------------------------------------------------------
    {
        pos.x += vel.x * dt;
        const int top = (int)floorf((pos.y - HEIGHT) / TILE);
        const int bot = (int)floorf((pos.y - 0.01f) / TILE);
        if (vel.x > 0) {
            const int tx = (int)floorf((pos.x + HALF_W) / TILE);
            for (int ty = top; ty <= bot; ++ty)
                if (w.Solid(tx, ty)) { pos.x = tx * TILE - HALF_W - 0.01f; vel.x = 0; break; }
        } else if (vel.x < 0) {
            const int tx = (int)floorf((pos.x - HALF_W) / TILE);
            for (int ty = top; ty <= bot; ++ty)
                if (w.Solid(tx, ty)) { pos.x = (tx + 1) * TILE + HALF_W + 0.01f; vel.x = 0; break; }
        }
    }

    // ---- move & collide: Y axis -----------------------------------------------------------
    const float oldBottom = pos.y;
    pos.y += vel.y * dt;
    const bool wasOnGround = onGround;
    onGround = false;
    {
        const int tx0 = (int)floorf((pos.x - HALF_W) / TILE);
        const int tx1 = (int)floorf((pos.x + HALF_W - 0.01f) / TILE);
        if (vel.y >= 0) {
            const int ty = (int)floorf(pos.y / TILE);
            for (int tx = tx0; tx <= tx1; ++tx) {
                const bool solid = w.Solid(tx, ty) && ty >= 0;
                const bool plat = w.Platform(tx, ty) && dropThrough <= 0 && oldBottom <= ty * TILE + 0.5f;
                if (solid || plat) {
                    pos.y = (float)(ty * TILE);
                    vel.y = 0;
                    onGround = true;
                    break;
                }
            }
        } else {
            const int ty = (int)floorf((pos.y - HEIGHT) / TILE);
            for (int tx = tx0; tx <= tx1; ++tx)
                if (w.Solid(tx, ty) && ty >= 0) { pos.y = (ty + 1) * TILE + HEIGHT; vel.y = 0; break; }
        }
    }
    if (onGround && !wasOnGround) ev.landed = true;

    // remember a safe spot (firm ground under both feet) for pit respawns
    if (onGround) {
        const int ty = (int)floorf(pos.y / TILE);
        if (w.Solid((int)floorf((pos.x - HALF_W - 6) / TILE), ty) && w.Solid((int)floorf((pos.x + HALF_W + 6) / TILE), ty))
            lastSafe = pos;
    }

    // ---- fell off the world ------------------------------------------------------------
    if (!ko && pos.y > w.PixelH() + 40) {
        ev.fellInPit = true;
        pos = lastSafe;
        pos.y -= 1;
        vel = {};
        invuln = 1.2f;
    }

    // ---- knocked out: count down, then respawn -----------------------------------------
    if (ko) {
        if (pos.y > w.PixelH() + 40) { pos.y = w.PixelH() + 40; vel = {}; }
        else if (onGround) vel.x *= 0.85f;
        koTimer -= dt;
        if (koTimer <= 0) Reset(spawn);
    }
    return ev;
}

CatFrame Cat::Frame() const
{
    if (ko) return CF_KO;
    if (hurtTimer > 0) return CF_HURT;
    if (attackTimer > 0) return attackTimer > ATTACK_TIME - 0.08f ? CF_ATTACK0 : CF_ATTACK1;
    if (!onGround) return vel.y < 0 ? CF_JUMP : CF_FALL;
    if (fabsf(vel.x) > 12.0f) return (CatFrame)(CF_WALK0 + ((int)(animTime * 12.0f) & 3));
    return fmodf(blinkTimer, 3.2f) > 3.05f ? CF_IDLE1 : CF_IDLE0;
}

void Cat::Draw(const CatSheet& sheet, Vector2 cam, float time) const
{
    if (!ko && invuln > 0 && flash <= 0 && ((int)(time * 20.0f) & 1)) return;   // blink while invulnerable
    const int x = (int)floorf(pos.x - CAT_CELL_W / 2.0f - cam.x);
    const int y = (int)floorf(pos.y - CAT_CELL_H - cam.y) + 1;
    DrawCatFrame(sheet, Frame(), x, y, facing < 0, flash > 0);

    // claw slash arc during the active part of the swipe
    if (AttackActive()) {
        const int sx = facing > 0 ? x + 20 : x + 3;
        const int sy = y + 12;
        const int f = facing;
        const Color c1 = amiga::pal::White, c2 = amiga::RGB12(0xAEF);
        for (int k = 0; k < 3; ++k) {
            const int oy = k * 3 - 3;
            DrawLine(sx + f * 2, sy + oy - 2, sx + f * 9, sy + oy + 3, k == 1 ? c1 : c2);
        }
    }
}

// ----------------------------------------------------------------------------
// Networking: each client owns its cat and streams this compact state.
// ----------------------------------------------------------------------------
std::string Cat::Serialize() const
{
    char buf[200];
    std::snprintf(buf, sizeof buf, "%.1f %.1f %.0f %.0f %d %d %.3f %.3f %.2f %.2f %d %d %d",
                  pos.x, pos.y, vel.x, vel.y, facing, onGround ? 1 : 0, attackTimer, hurtTimer,
                  invuln, flash, ko ? 1 : 0, hp, candy);
    return buf;
}

bool Cat::ApplyRemote(const char* p)
{
    // 13 numbers: x y vx vy facing onGround attack hurt invuln flash ko hp candy
    float v[13];
    const char* cur = p;
    for (float& n : v) {
        char* end = nullptr;
        n = std::strtof(cur, &end);
        if (end == cur) return false;
        cur = end;
    }
    const float x = v[0], y = v[1], vx = v[2], vy = v[3], at = v[6], ht = v[7], inv = v[8], fl = v[9];
    const int f = (int)v[4], g = (int)v[5], k = (int)v[10], h = (int)v[11], c = (int)v[12];
    const bool teleport = !netHasState || fabsf(x - pos.x) > 64.0f || fabsf(y - pos.y) > 64.0f;
    netTarget = Vector2{ x, y };
    if (teleport) pos = netTarget;
    vel = Vector2{ vx, vy };
    facing = f < 0 ? -1 : 1;
    onGround = g != 0;
    attackTimer = at;
    hurtTimer = ht;
    invuln = inv;
    flash = fl;
    ko = k != 0;
    hp = h;
    candy = c;
    netHasState = true;
    return true;
}

void Cat::TickRemote(float dt)
{
    animTime += dt;
    blinkTimer += dt;
    // dead reckoning between packets, then ease towards the reported position
    netTarget.x += vel.x * dt;
    if (!onGround) netTarget.y += vel.y * dt;
    pos.x += (netTarget.x - pos.x) * 0.35f;
    pos.y += (netTarget.y - pos.y) * 0.35f;
    if (attackTimer > 0) attackTimer -= dt;
    if (hurtTimer > 0) hurtTimer -= dt;
    if (invuln > 0) invuln -= dt;
    if (flash > 0) flash -= dt;
}
