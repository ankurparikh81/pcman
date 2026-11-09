#include "game.hpp"
#include "benchmark.hpp"
#include <raylib.h>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n\n"
              << "Character Controller Options:\n"
              << "  --pacman <human|random>     Controller for Pac-Man (default: human in GUI, random in --headless)\n"
              << "  --ghosts <original|random>  Controller for all 4 ghosts (default: original)\n"
              << "  --blinky <original|random>  Controller override for Blinky (Red)\n"
              << "  --pinky  <original|random>  Controller override for Pinky (Pink)\n"
              << "  --inky   <original|random>  Controller override for Inky (Cyan)\n"
              << "  --clyde  <original|random>  Controller override for Clyde (Orange)\n\n"
              << "Simulation & Benchmark Options:\n"
              << "  --headless                  Run fast headless batch evaluation without GUI/audio\n"
              << "  --episodes <N>              Number of games to simulate in headless mode (default: 100)\n"
              << "  --max-ticks <T>             Maximum 18.2Hz ticks per episode (default: 10000)\n"
              << "  --seed <S>                  Base 16-bit RNG seed (default: 0)\n"
              << "  --level <L>                 Starting level 1..12 (default: 1)\n"
              << "  -h, --help                  Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    pcman::BenchmarkConfig cfg{};
    bool headless = false;
    bool pacmanExplicitlySet = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        auto requireValue = [&](std::string_view flag) -> std::string_view {
            if (i + 1 >= argc) {
                std::cerr << "Error: Missing value for " << flag << "\n";
                PrintUsage(argv[0]);
                std::exit(1);
            }
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            PrintUsage(argv[0]);
            return 0;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--pacman") {
            cfg.pacmanStrategy = std::string(requireValue(arg));
            pacmanExplicitlySet = true;
        } else if (arg == "--ghosts") {
            std::string g = std::string(requireValue(arg));
            cfg.ghostStrategies = { g, g, g, g };
        } else if (arg == "--blinky") {
            cfg.ghostStrategies[0] = std::string(requireValue(arg));
        } else if (arg == "--pinky") {
            cfg.ghostStrategies[1] = std::string(requireValue(arg));
        } else if (arg == "--inky") {
            cfg.ghostStrategies[2] = std::string(requireValue(arg));
        } else if (arg == "--clyde") {
            cfg.ghostStrategies[3] = std::string(requireValue(arg));
        } else if (arg == "--episodes") {
            cfg.episodes = std::stoi(std::string(requireValue(arg)));
        } else if (arg == "--max-ticks") {
            cfg.maxTicksPerEpisode = std::stoi(std::string(requireValue(arg)));
        } else if (arg == "--seed") {
            cfg.baseSeed = static_cast<uint16_t>(std::stoul(std::string(requireValue(arg)), nullptr, 0));
        } else if (arg == "--level") {
            cfg.startLevel = std::stoi(std::string(requireValue(arg)));
        } else {
            std::cerr << "Error: Unknown option '" << arg << "'\n";
            PrintUsage(argv[0]);
            return 1;
        }
    }

    if (!pacmanExplicitlySet) {
        cfg.pacmanStrategy = headless ? "random" : "human";
    }

    // Validate controller names before launching
    {
        pcman::Game validationGame;
        if (!pcman::ConfigureGameControllers(validationGame, cfg.pacmanStrategy, cfg.ghostStrategies)) {
            std::cerr << "Error: Invalid controller strategy specified.\n"
                      << "  Valid Pac-Man strategies: human, random\n"
                      << "  Valid Ghost strategies  : original, random\n";
            return 1;
        }
    }

    if (headless) {
        auto summary = pcman::RunBenchmark(cfg);
        pcman::PrintBenchmarkSummary(summary);
        return 0;
    }

    // Initialize 960x720 window (3x 320x240 for crisp 4:3 CRT geometry)
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(960, 720, "PC-Man (1982) - Raylib Port");
    SetWindowMinSize(320, 240);

    InitAudioDevice();
    SetTargetFPS(60);

    auto game = std::make_unique<pcman::Game>();
    game->Init();
    pcman::ConfigureGameControllers(*game, cfg.pacmanStrategy, cfg.ghostStrategies);

    // If Pac-Man is controlled by an AI strategy in GUI mode, start the game immediately
    if (cfg.pacmanStrategy != "human") {
        game->StartNewGame(cfg.startLevel, cfg.baseSeed);
    }

    std::cout << "[PC-Man] Game initialized (Pac-Man=" << cfg.pacmanStrategy
              << ", Blinky=" << cfg.ghostStrategies[0]
              << ", Pinky=" << cfg.ghostStrategies[1]
              << ", Inky=" << cfg.ghostStrategies[2]
              << ", Clyde=" << cfg.ghostStrategies[3] << ").\n";

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
