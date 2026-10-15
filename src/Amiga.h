// Amiga.h - "hardware" constants and the OCS-style 12-bit palette.
// Everything is rendered into a 320x256 PAL low-res buffer and scaled up.
#pragma once
#include "raylib.h"
#include <cstdint>

namespace amiga {

constexpr int SCREEN_W = 320;
constexpr int SCREEN_H = 256;

// 12-bit colour, exactly like the Amiga OCS colour registers ($RGB, 4 bits each)
constexpr Color RGB12(int hex)
{
    return Color{ (unsigned char)(((hex >> 8) & 15) * 17),
                  (unsigned char)(((hex >> 4) & 15) * 17),
                  (unsigned char)((hex & 15) * 17), 255 };
}

// Linear blend between two colours, snapped back to 4 bits per channel so
// gradients show the typical copper-bar banding.
inline Color Lerp12(Color a, Color b, float t)
{
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    auto ch = [t](unsigned char x, unsigned char y) {
        int v = (int)(x + (y - x) * t + 0.5f);
        int n = (v + 8) / 17;          // quantise to 0..15
        if (n > 15) n = 15;
        return (unsigned char)(n * 17);
    };
    return Color{ ch(a.r, b.r), ch(a.g, b.g), ch(a.b, b.b), 255 };
}

// Small deterministic hash / RNG (identical on every machine -> network safe)
inline uint32_t Hash(uint32_t x)
{
    x ^= x >> 16; x *= 0x7feb352dU;
    x ^= x >> 15; x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
inline uint32_t Hash2(int x, int y, uint32_t seed = 0)
{
    return Hash((uint32_t)x * 73856093U ^ (uint32_t)y * 19349663U ^ seed * 83492791U);
}

struct Rng {
    uint32_t s;
    explicit Rng(uint32_t seed) : s(seed ? seed : 0x1234567u) {}
    uint32_t Next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    int Range(int lo, int hi) { return lo + (int)(Next() % (uint32_t)(hi - lo + 1)); } // inclusive
    float Float() { return (Next() & 0xFFFFFF) / 16777216.0f; }
    bool Chance(float p) { return Float() < p; }
};

// --- Palette -------------------------------------------------------------
namespace pal {
    constexpr Color Black      = RGB12(0x000);
    constexpr Color Night      = RGB12(0x102);
    constexpr Color DeepPurple = RGB12(0x213);
    constexpr Color Purple     = RGB12(0x426);
    constexpr Color Violet     = RGB12(0x649);
    constexpr Color Lilac      = RGB12(0x96C);
    constexpr Color Orange     = RGB12(0xF80);
    constexpr Color Pumpkin    = RGB12(0xE60);
    constexpr Color DarkOrange = RGB12(0xA30);
    constexpr Color Yellow     = RGB12(0xFE4);
    constexpr Color Moon       = RGB12(0xFFC);
    constexpr Color MoonShade  = RGB12(0xDDA);
    constexpr Color Green      = RGB12(0x5C3);
    constexpr Color Lime       = RGB12(0xCF3);
    constexpr Color White      = RGB12(0xFFF);
    constexpr Color Grey       = RGB12(0x888);
    constexpr Color DarkGrey   = RGB12(0x445);
    constexpr Color Red        = RGB12(0xE22);
    constexpr Color Pink       = RGB12(0xF8A);
    constexpr Color Blue       = RGB12(0x48F);
}

} // namespace amiga
