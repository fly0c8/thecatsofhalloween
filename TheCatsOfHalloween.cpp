// TheCatsOfHalloween.cpp - entry point.
// A two-player online Amiga-style Halloween platformer built with raylib.
// Start the Go server in server/ first; every client joins the same world over a websocket.
// Desktop (Visual Studio / CMake) and WebAssembly (Emscripten) builds share this file.

#include "raylib.h"
#include "src/Game.h"
#include "src/Music.h"
#include "src/Net.h"
#include <cstring>

#if defined(PLATFORM_WEB)
#include <emscripten/emscripten.h>
#endif

static Game g_game;

static void UpdateDrawFrame()
{
    g_game.Frame();
}

static int RunGame(int argc, char** argv)
{
#if defined(PLATFORM_WEB)
    SetConfigFlags(FLAG_VSYNC_HINT);   // the game sizes the canvas to the page itself
    InitWindow(960, 768, "The Cats of Halloween");
#else
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(960, 768, "The Cats of Halloween");
    SetWindowMinSize(320, 256);
    MaximizeWindow();                  // big screen by default; F11 / Alt+Enter = fullscreen
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--fullscreen") == 0 || std::strcmp(argv[i], "-f") == 0) ToggleBorderlessWindowed();
#endif
    SetExitKey(KEY_NULL);          // ESC is handled by the game (pause / quit from title)
    InitAudioDevice();

    music::Init();
    g_game.Init(DefaultServerUrl(argc, argv));   // e.g. TheCatsOfHalloween.exe ws://192.168.0.10:8080/ws --fullscreen

#if defined(PLATFORM_WEB)
    emscripten_set_main_loop(UpdateDrawFrame, 0, 1);
#else
    SetTargetFPS(60);
    while (!WindowShouldClose() && !g_game.WantsQuit()) UpdateDrawFrame();
#endif

    g_game.Shutdown();
    music::Shutdown();
    CloseAudioDevice();
    CloseWindow();
    return 0;
}

#if defined(PLATFORM_WEB)
// Plain main() for the browser: some Emscripten versions don't pick up main(argc, argv),
// and the web build takes its server address from the page URL anyway.
int main() { return RunGame(0, nullptr); }
#else
int main(int argc, char** argv) { return RunGame(argc, argv); }
#endif
