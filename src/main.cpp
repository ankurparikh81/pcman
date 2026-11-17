#include "game.hpp"
#include "benchmark.hpp"
#include <raylib.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>

namespace {

void PrintUsage(const char* progName) {
    std::cout << "Usage: " << progName << " [options]\n\n"
              << "Character Controller Options:\n"
              << "  --pacman <human|random|qtable>  Controller for Pac-Man (default: human in GUI, random in --headless)\n"
              << "  --ghosts <original|random>      Controller for all 4 ghosts (default: original)\n"
              << "  --blinky <original|random>      Controller override for Blinky (Red)\n"
              << "  --pinky  <original|random>      Controller override for Pinky (Pink)\n"
              << "  --inky   <original|random>      Controller override for Inky (Cyan)\n"
              << "  --clyde  <original|random>      Controller override for Clyde (Orange)\n\n"
              << "Reinforcement Learning Options (--pacman qtable):\n"
              << "  --train-episodes <N>            Train Tabular Q-Learning agent for N episodes before play/eval\n"
              << "  --trace-capacity <N>            Max eligibility trace length 1..64 (default: 24)\n"
              << "  --gamma <G>                     Discount factor gamma in [0, 1) (default: 0.95)\n"
              << "  --gamma-schedule <fixed|linear|eps>  Dynamic gamma mode (default: fixed)\n"
              << "  --gamma-start <G0>              Starting gamma for linear/eps schedules (default: 0.80)\n"
              << "  --lambda <L>                    Eligibility trace decay factor 0..1 (default: 0.85)\n"
              << "  --eps-schedule <floor|cutoff>   Epsilon decay mode: floor (legacy) or cutoff (0.000 tail)\n"
              << "  --eps-start <E>                 Starting exploration rate epsilon (default: 1.000)\n"
              << "  --eps-min <E>                   Minimum epsilon reached before greedy tail (default: 0.010)\n"
              << "  --eps-cutoff <F>                Fraction of episodes (0..1] to reach eps-min, then eps=0.000 (default: 1.000)\n"
              << "  --qtable-file <path>            Load existing Q-table and/or save trained Q-table to <path>\n\n"
              << "Simulation & Benchmark Options:\n"
              << "  --headless                      Run fast headless batch evaluation without GUI/audio\n"
              << "  --episodes <N>                  Number of games to simulate in headless mode (default: 100)\n"
              << "  --max-ticks <T>                 Maximum 18.2Hz ticks per episode (default: 10000)\n"
              << "  --seed <S>                      Base 16-bit RNG seed (default: 0)\n"
              << "  --level <L>                     Starting level 1..12 (default: 1)\n"
              << "  -h, --help                      Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    pcman::BenchmarkConfig cfg{};
    cfg.gammaSchedule.gammaStart = 0.80f;
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
        } else if (arg == "--train-episodes") {
            cfg.trainEpisodes = std::stoi(std::string(requireValue(arg)));
        } else if (arg == "--trace-capacity") {
            cfg.traceCapacity = static_cast<size_t>(std::stoul(std::string(requireValue(arg))));
        } else if (arg == "--gamma") {
            cfg.gamma = std::stof(std::string(requireValue(arg)));
        } else if (arg == "--gamma-schedule") {
            std::string_view m = requireValue(arg);
            if (m == "fixed") {
                cfg.gammaSchedule.mode = pcman::GammaScheduleMode::Fixed;
            } else if (m == "linear") {
                cfg.gammaSchedule.mode = pcman::GammaScheduleMode::LinearRamp;
            } else if (m == "eps") {
                cfg.gammaSchedule.mode = pcman::GammaScheduleMode::EpsCoupled;
            } else {
                std::cerr << "Error: Invalid --gamma-schedule '" << m << "' (expected 'fixed', 'linear', or 'eps')\n";
                return 1;
            }
        } else if (arg == "--gamma-start") {
            cfg.gammaSchedule.gammaStart = std::stof(std::string(requireValue(arg)));
            if (cfg.gammaSchedule.mode == pcman::GammaScheduleMode::Fixed) {
                cfg.gammaSchedule.mode = pcman::GammaScheduleMode::LinearRamp;
            }
        } else if (arg == "--lambda") {
            cfg.lambda = std::stof(std::string(requireValue(arg)));
        } else if (arg == "--eps-schedule") {
            std::string_view m = requireValue(arg);
            if (m == "floor") {
                cfg.epsSchedule.mode = pcman::EpsScheduleMode::FloorClamp;
            } else if (m == "cutoff") {
                cfg.epsSchedule.mode = pcman::EpsScheduleMode::ExpCutoff;
            } else {
                std::cerr << "Error: Invalid --eps-schedule '" << m << "' (expected 'floor' or 'cutoff')\n";
                return 1;
            }
        } else if (arg == "--eps-start") {
            cfg.epsSchedule.epsStart = std::stof(std::string(requireValue(arg)));
        } else if (arg == "--eps-min") {
            cfg.epsSchedule.epsMin = std::stof(std::string(requireValue(arg)));
        } else if (arg == "--eps-cutoff") {
            cfg.epsSchedule.epsCutoff = std::stof(std::string(requireValue(arg)));
        } else if (arg == "--qtable-file") {
            cfg.qtableFile = std::string(requireValue(arg));
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
        if (cfg.trainEpisodes > 0 || !cfg.qtableFile.empty()) {
            cfg.pacmanStrategy = "qtable";
        } else {
            cfg.pacmanStrategy = headless ? "random" : "human";
        }
    }

    // Validate controller names before launching
    {
        pcman::Game validationGame;
        if (!pcman::ConfigureGameControllers(validationGame, cfg.pacmanStrategy, cfg.ghostStrategies)) {
            std::cerr << "Error: Invalid controller strategy specified.\n"
                      << "  Valid Pac-Man strategies: human, random, qtable\n"
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

    if (auto qAgent = std::dynamic_pointer_cast<pcman::TabularQPacmanController>(game->GetPacmanController())) {
        if (!cfg.qtableFile.empty() && std::filesystem::exists(cfg.qtableFile)) {
            if (qAgent->LoadFromFile(cfg.qtableFile)) {
                std::cout << "[PC-Man] Loaded Q-table from " << cfg.qtableFile
                          << " (" << qAgent->CountNonZeroEntries() << " non-zero entries).\n";
            }
        }
        if (cfg.trainEpisodes > 0) {
            qAgent->SetTraceCapacity(cfg.traceCapacity);
            qAgent->SetGamma(std::clamp(cfg.gamma, 0.0f, 0.9999f));
            qAgent->SetLambda(cfg.lambda);
            pcman::GammaSchedule gSched = cfg.gammaSchedule;
            gSched.gammaEnd = std::clamp(cfg.gamma, 0.0f, 0.9999f);
            pcman::TrainTabularQAgent(*qAgent, cfg.trainEpisodes, cfg.maxTicksPerEpisode,
                                      cfg.baseSeed, cfg.startLevel, cfg.ghostStrategies,
                                      true, cfg.epsSchedule, gSched);
            if (!cfg.qtableFile.empty()) {
                qAgent->SaveToFile(cfg.qtableFile);
                std::cout << "[PC-Man] Saved trained Q-table to " << cfg.qtableFile << ".\n";
            }
        }
        qAgent->SetTraining(false);
        qAgent->SetEpsilon(0.0f);
    }

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
