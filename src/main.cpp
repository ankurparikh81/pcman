#include "game.hpp"
#include <raylib.h>
#include <memory>
#include <iostream>

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {

    // Initialize 960x720 window (3x 320x240 for crisp 4:3 CRT geometry)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(960, 720, "PC-Man (1982) - Raylib Port");
    SetWindowMinSize(320, 240);

    InitAudioDevice();
    SetTargetFPS(60);

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

    return 0;
}
