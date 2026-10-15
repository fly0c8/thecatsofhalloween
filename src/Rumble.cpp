#include "Rumble.h"

#include <algorithm>

namespace {
float g_low = 0, g_high = 0, g_left = 0;   // current strength + time left
}

// ============================================================================
// Windows: XInput, loaded from the system DLL on first use
// ============================================================================
#if defined(_WIN32) && !defined(__EMSCRIPTEN__)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace {
struct XVibration { WORD left, right; };                  // XINPUT_VIBRATION
using SetStateFn = DWORD(WINAPI*)(DWORD, XVibration*);    // XInputSetState

SetStateFn LoadXInput()
{
    static bool tried = false;
    static SetStateFn fn = nullptr;
    if (tried) return fn;
    tried = true;
    const wchar_t* dlls[] = { L"xinput1_4.dll", L"xinput1_3.dll", L"xinput9_1_0.dll" };
    for (const wchar_t* name : dlls) {
        if (HMODULE m = LoadLibraryW(name)) {
            fn = reinterpret_cast<SetStateFn>(reinterpret_cast<void*>(GetProcAddress(m, "XInputSetState")));
            if (fn) break;
        }
    }
    return fn;
}

void Apply(float low, float high)
{
    SetStateFn set = LoadXInput();
    if (!set) return;
    XVibration v{ (WORD)(std::clamp(low, 0.0f, 1.0f) * 65535.0f), (WORD)(std::clamp(high, 0.0f, 1.0f) * 65535.0f) };
    for (DWORD pad = 0; pad < 4; ++pad) set(pad, &v);     // unplugged slots just return an error
}
}

// ============================================================================
// Web: raylib passes it on to the browser's Gamepad API
// ============================================================================
#elif defined(__EMSCRIPTEN__)
#include "raylib.h"

namespace {
void Apply(float low, float high)
{
    if (low <= 0 && high <= 0) return;   // the browser stops by itself after the duration
    for (int pad = 0; pad < 4; ++pad)
        if (IsGamepadAvailable(pad)) SetGamepadVibration(pad, low, high, g_left);
}
}

#else
namespace {
void Apply(float, float) {}
}
#endif

namespace rumble {

void Pulse(float low, float high, float seconds)
{
    // a stronger or longer shake wins over a weaker one that is still running
    if (g_left > 0 && low + high < g_low + g_high && seconds < g_left) return;
    g_low = low;
    g_high = high;
    g_left = seconds;
    Apply(low, high);
}

void Update(float dt)
{
    if (g_left <= 0) return;
    g_left -= dt;
    if (g_left <= 0) {
        g_left = g_low = g_high = 0;
        Apply(0, 0);
    }
}

void Stop()
{
    g_left = g_low = g_high = 0;
    Apply(0, 0);
}

} // namespace rumble
