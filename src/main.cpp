#include "game.hpp"
#include <raylib.h>
#include <memory>
#include <iostream>

#if defined(__EMSCRIPTEN__)
#include <emscripten/emscripten.h>

static std::unique_ptr<pcman::Game> g_game;

static void WebUpdateFrame() {
    g_game->Update();

    BeginDrawing();
    g_game->Render();
    EndDrawing();
}
#endif

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {

    // Initialize 960x720 window (3x 320x240 for crisp 4:3 CRT geometry)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(960, 720, "PC-Man (1982) - Raylib Port");
    SetWindowMinSize(320, 240);

    InitAudioDevice();
    SetTargetFPS(60);

#if defined(__EMSCRIPTEN__)
    g_game = std::make_unique<pcman::Game>();
    g_game->Init();

    std::cout << "[PC-Man] Game initialized (WebAssembly). Press Space to play, Arrow Keys / ESDX / IJKL to move, Ctrl-S to mute.\n";

    // 0 fps uses browser requestAnimationFrame, simulate_infinite_loop=1
    emscripten_set_main_loop(WebUpdateFrame, 0, 1);
#else
    auto game = std::make_unique<pcman::Game>();
    game->Init();

    std::cout << "[PC-Man] Game initialized. Press Space to play, Arrow Keys / ESDX / IJKL to move, Ctrl-S to mute, ESC to quit.\n";

    while (!WindowShouldClose()) {
        game->Update();

        BeginDrawing();
        game->Render();
        EndDrawing();
    }

    game->Close();
    CloseAudioDevice();
    CloseWindow();
#endif

    return 0;
}

