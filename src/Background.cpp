#include "Background.h"
#include "Amiga.h"
#include <cmath>

using namespace amiga;

void Background::Load()
{
    // Big dithered moon with craters and a halo, painted pixel by pixel
    const int R = 22, S = 2 * R + 14;
    Image img = GenImageColor(S, S, BLANK);
    const int c = S / 2;
    for (int y = 0; y < S; ++y)
        for (int x = 0; x < S; ++x) {
            const float dx = (float)(x - c), dy = (float)(y - c);
            const float d = sqrtf(dx * dx + dy * dy);
            if (d <= R) {
                Color col = pal::Moon;
                // shading towards the lower-left, with ordered dither at the boundary
                const float shade = (dx * -0.6f + dy * 0.8f) / R;   // -1..1
                const int bayer[4][4] = { {0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5} };
                const float thr = bayer[y & 3][x & 3] / 16.0f;
                if (shade > 0.25f + thr * 0.5f) col = pal::MoonShade;
                if (shade > 0.70f + thr * 0.3f) col = RGB12(0xBB8);
                ImageDrawPixel(&img, x, y, col);
            } else if (d <= R + 6 && ((x + y) & 1) == 0 && d <= R + 3 + ((x * 7 + y * 3) % 4)) {
                ImageDrawPixel(&img, x, y, RGB12(0x535));
            }
        }
    struct Crater { int x, y, r; } craters[] = { {-8, -6, 4}, {6, -10, 3}, {7, 5, 5}, {-6, 9, 3}, {-12, 3, 2}, {12, -2, 2} };
    for (auto& cr : craters) {
        ImageDrawCircle(&img, c + cr.x, c + cr.y, cr.r, pal::MoonShade);
        ImageDrawCircle(&img, c + cr.x + 1, c + cr.y + 1, cr.r - 1, RGB12(0xCC9));
    }
    moon_ = LoadTextureFromImage(img);
    UnloadImage(img);
}

void Background::Unload()
{
    if (moon_.id) UnloadTexture(moon_);
    moon_ = {};
}

static float HillFar(float x)
{
    return 18.0f * sinf(x * 0.0090f) + 9.0f * sinf(x * 0.023f + 1.3f) + 4.0f * sinf(x * 0.061f + 0.5f);
}
static float HillNear(float x)
{
    return 12.0f * sinf(x * 0.013f + 2.0f) + 6.0f * sinf(x * 0.037f) + 3.0f * sinf(x * 0.11f + 1.0f);
}

void Background::Draw(Vector2 cam, int viewW, int viewH, float worldW, float worldH, float time) const
{
    (void)worldW;

    // --- copper sky: one colour per scanline, 12-bit banding on purpose -----
    const Color top = RGB12(0x102), mid = RGB12(0x426), low = RGB12(0xA35), horizon = RGB12(0xF84);
    for (int y = 0; y < viewH; ++y) {
        const float t = (y + cam.y * 0.35f) / (viewH + worldH * 0.35f) * 1.25f + 0.05f;
        Color col;
        if (t < 0.45f) col = Lerp12(top, mid, t / 0.45f);
        else if (t < 0.8f) col = Lerp12(mid, low, (t - 0.45f) / 0.35f);
        else col = Lerp12(low, horizon, (t - 0.8f) / 0.2f);
        DrawRectangle(0, y, viewW, 1, col);
    }

    // --- stars ---------------------------------------------------------------------
    const int starShiftX = (int)(cam.x * 0.03f);
    const int starShiftY = (int)(cam.y * 0.05f);
    for (int i = 0; i < 70; ++i) {
        const uint32_t h = Hash((uint32_t)i * 2654435761u);
        int x = (int)(h % 1024) - starShiftX;
        x = ((x % 1024) + 1024) % 1024;
        if (x >= viewW) continue;
        const int y = (int)((h >> 10) % 200) - starShiftY;
        if (y < 0 || y >= viewH * 0.7f) continue;
        const bool twinkle = ((int)(time * 3.0f) + i) % 11 == 0;
        DrawPixel(x, y, twinkle ? pal::White : ((h >> 20) & 1 ? RGB12(0x99B) : RGB12(0x667)));
        if (twinkle) { DrawPixel(x - 1, y, RGB12(0x99B)); DrawPixel(x + 1, y, RGB12(0x99B)); }
    }

    // --- moon (hardly moves: it's very far away) ----------------------------------
    const int mx = viewW - 90 - (int)(cam.x * 0.01f);
    const int my = 8 - (int)(cam.y * 0.04f);
    DrawTexture(moon_, mx, my, WHITE);

    // A witch on a broom crosses the moon now and then
    const float wt = fmodf(time, 24.0f);
    if (wt < 6.0f) {
        const int wx = mx - 30 + (int)(wt * 22.0f);
        const int wy = my + 30 - (int)(wt * 4.0f) + (int)(sinf(time * 3) * 2);
        const Color s = RGB12(0x102);
        DrawRectangle(wx - 8, wy + 2, 18, 1, s);            // broom stick
        DrawLine(wx - 12, wy, wx - 8, wy + 2, s); DrawLine(wx - 12, wy + 5, wx - 8, wy + 3, s);
        DrawLine(wx - 11, wy + 2, wx - 8, wy + 2, s);
        DrawRectangle(wx - 1, wy - 3, 4, 5, s);              // body
        DrawRectangle(wx, wy - 6, 3, 3, s);                  // head
        DrawTriangle(Vector2{ (float)wx - 1, (float)wy - 5 }, Vector2{ (float)wx + 4, (float)wy - 5 },
                     Vector2{ (float)wx + 4, (float)wy - 12 }, s);  // hat
        DrawRectangle(wx - 2, wy - 5, 7, 1, s);
        DrawLine(wx - 1, wy - 2, wx - 4, wy + 1, s);         // cape
    }

    // --- far hills + castle silhouette ---------------------------------------------
    const float baseFar = viewH * 0.62f - (cam.y - (worldH - viewH) * 0.6f) * 0.18f;
    const Color farCol = RGB12(0x213), farTop = RGB12(0x324);
    const float px1 = cam.x * 0.22f;
    for (int x = 0; x < viewW; ++x) {
        const float wx = x + px1;
        int y = (int)(baseFar - HillFar(wx));
        // a ruined castle on the far hills (repeats every 900 parallax pixels)
        const float cxm = fmodf(wx + 4000.0f, 900.0f);
        if (cxm > 600 && cxm < 660) {
            const float lx = cxm - 600;
            int towerH = 22;
            if (lx < 8 || lx > 52) towerH = 40;          // two towers
            else if (lx > 24 && lx < 34) towerH = 52;    // keep
            if ((int)lx % 4 < 2 && towerH < 30) towerH += 3;  // battlements
            y = (int)(baseFar - HillFar(4000 + 630.0f) - towerH);
        }
        if (y < viewH) {
            DrawRectangle(x, y, 1, viewH - y, farCol);
            DrawPixel(x, y, farTop);
        }
        // lit castle windows
        if (cxm > 600 && cxm < 660) {
            const int lx = (int)(cxm - 600);
            if ((lx == 3 || lx == 4 || lx == 56 || lx == 57) && ((int)(time * 0.7f) % 3 != 0)) {
                const int wy = (int)(baseFar - HillFar(4000 + 630.0f) - 32);
                DrawPixel(x, wy, pal::Yellow); DrawPixel(x, wy + 1, pal::Orange);
            }
            if (lx == 29 || lx == 30) {
                const int wy = (int)(baseFar - HillFar(4000 + 630.0f) - 44);
                DrawPixel(x, wy, pal::Yellow); DrawPixel(x, wy + 1, pal::Yellow);
            }
        }
    }

    // --- near hills with dead trees and grave crosses ---------------------------------
    const float baseNear = viewH * 0.78f - (cam.y - (worldH - viewH) * 0.6f) * 0.35f;
    const Color nearCol = RGB12(0x102);
    const float px2 = cam.x * 0.45f;
    for (int x = 0; x < viewW; ++x) {
        const float wx = x + px2;
        const int y = (int)(baseNear - HillNear(wx));
        if (y < viewH) DrawRectangle(x, y, 1, viewH - y, nearCol);
    }
    // silhouettes placed on a 70px grid in parallax space
    const int first = (int)floorf(px2 / 70.0f) - 1;
    for (int i = first; i < first + viewW / 70 + 3; ++i) {
        const uint32_t h = Hash((uint32_t)(i + 100000) * 747796405u);
        const float wx = i * 70.0f + (float)(h % 40);
        const int sx = (int)(wx - px2);
        const int gy = (int)(baseNear - HillNear(wx)) + 2;
        if ((h >> 8) % 3 == 0) {          // dead tree
            const int th = 18 + (int)((h >> 12) % 14);
            DrawRectangle(sx - 1, gy - th, 2, th, nearCol);
            DrawLine(sx, gy - th + 6, sx - 7, gy - th - 1, nearCol);
            DrawLine(sx, gy - th + 9, sx + 8, gy - th + 1, nearCol);
            DrawLine(sx, gy - th + 3, sx + 3, gy - th - 4, nearCol);
            DrawLine(sx - 7, gy - th - 1, sx - 9, gy - th - 4, nearCol);
        } else if ((h >> 8) % 3 == 1) {   // grave cross
            DrawRectangle(sx, gy - 10, 2, 10, nearCol);
            DrawRectangle(sx - 2, gy - 8, 6, 2, nearCol);
        }
    }
}
