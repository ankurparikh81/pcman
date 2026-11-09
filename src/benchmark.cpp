#include "benchmark.hpp"
#include "game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>

namespace pcman {

bool ConfigureGameControllers(Game& game,
                              std::string_view pacmanStrategy,
                              const std::array<std::string, 4>& ghostStrategies) {
    auto pacCtrl = CreatePacmanControllerByName(pacmanStrategy);
    if (!pacCtrl) {
        return false;
    }
    game.SetPacmanController(std::move(pacCtrl));

    for (size_t i = 0; i < ghostStrategies.size(); ++i) {
        auto ghostCtrl = CreateGhostControllerByName(ghostStrategies[i]);
        if (!ghostCtrl) {
            return false;
        }
        game.SetGhostController(i, std::move(ghostCtrl));
    }
    return true;
}

BenchmarkSummary RunBenchmark(const BenchmarkConfig& config) {
    BenchmarkSummary summary{};
    summary.config = config;

    int numEpisodes = std::max(1, config.episodes);
    int maxTicks = std::max(1, config.maxTicksPerEpisode);
    summary.episodes.reserve(static_cast<size_t>(numEpisodes));

    Game game;
    ConfigureGameControllers(game, config.pacmanStrategy, config.ghostStrategies);

    auto startTime = std::chrono::steady_clock::now();
    int64_t totalTicksSimulated = 0;

    for (int ep = 0; ep < numEpisodes; ++ep) {
        uint16_t episodeSeed = static_cast<uint16_t>(config.baseSeed + static_cast<uint16_t>(ep));
        game.StartNewGame(config.startLevel, episodeSeed);
        game.SetState(GameState::Playing);

        EpisodeResult res{};
        while (res.ticksSurvived < maxTicks && game.GetState() != GameState::GameOver) {
            GameState st = game.GetState();
            if (st == GameState::Playing) {
                game.Tick18Hz();
                res.ticksSurvived++;
                totalTicksSimulated++;
            } else if (st == GameState::Ready) {
                game.SetState(GameState::Playing);
            } else if (st == GameState::PacmanDying) {
                // Fast-forward death animation to next life or GameOver
                game.Update(2.0f, InputState{});
            } else if (st == GameState::LevelClear) {
                res.levelsCleared++;
                // Fast-forward level clear flash to next level's Ready state
                game.Update(3.0f, InputState{});
            }
        }

        int dotsOnCurrentBoard = Maze::GetTotalDots() - game.GetMaze().GetRemainingDots();
        res.dotsEaten = res.levelsCleared * Maze::GetTotalDots() + dotsOnCurrentBoard;
        res.score = game.GetScore();
        res.clearedFirstLevel = (res.levelsCleared >= 1);
        res.timedOut = (res.ticksSurvived >= maxTicks && game.GetState() != GameState::GameOver);

        summary.episodes.push_back(res);
    }

    auto endTime = std::chrono::steady_clock::now();
    summary.elapsedSeconds = std::chrono::duration<double>(endTime - startTime).count();

    double sumScore = 0.0;
    double sumDots = 0.0;
    double sumTicks = 0.0;
    int firstLevelClears = 0;
    summary.minScore = summary.episodes.front().score;
    summary.maxScore = summary.episodes.front().score;

    for (const auto& ep : summary.episodes) {
        sumScore += ep.score;
        sumDots += ep.dotsEaten;
        sumTicks += ep.ticksSurvived;
        summary.minScore = std::min(summary.minScore, ep.score);
        summary.maxScore = std::max(summary.maxScore, ep.score);
        summary.totalLevelsCleared += ep.levelsCleared;
        if (ep.clearedFirstLevel) {
            firstLevelClears++;
        }
    }

    double n = static_cast<double>(summary.episodes.size());
    summary.meanScore = sumScore / n;
    summary.meanDotsEaten = sumDots / n;
    summary.meanTicks = sumTicks / n;
    summary.levelClearRate = (static_cast<double>(firstLevelClears) / n) * 100.0;

    double varianceSum = 0.0;
    for (const auto& ep : summary.episodes) {
        double diff = ep.score - summary.meanScore;
        varianceSum += diff * diff;
    }
    summary.stdDevScore = std::sqrt(varianceSum / n);
    summary.ticksPerSecond = (summary.elapsedSeconds > 0.0)
        ? (static_cast<double>(totalTicksSimulated) / summary.elapsedSeconds)
        : 0.0;

    return summary;
}

void PrintBenchmarkSummary(const BenchmarkSummary& s) {
    std::printf("=== PC-Man Headless AI Benchmark Summary ===\n");
    std::printf("Controllers:\n");
    std::printf("  Pac-Man : %s\n", s.config.pacmanStrategy.c_str());
    std::printf("  Blinky  : %s\n", s.config.ghostStrategies[0].c_str());
    std::printf("  Pinky   : %s\n", s.config.ghostStrategies[1].c_str());
    std::printf("  Inky    : %s\n", s.config.ghostStrategies[2].c_str());
    std::printf("  Clyde   : %s\n", s.config.ghostStrategies[3].c_str());
    std::printf("Configuration:\n");
    std::printf("  Episodes: %zu | Max Ticks/Ep: %d | Start Level: %d | Base Seed: 0x%04X\n",
                s.episodes.size(), s.config.maxTicksPerEpisode, s.config.startLevel, s.config.baseSeed);
    std::printf("Results:\n");
    std::printf("  Score       : Mean = %.1f (StdDev = %.1f, Min = %d, Max = %d)\n",
                s.meanScore, s.stdDevScore, s.minScore, s.maxScore);
    std::printf("  Dots Eaten  : Mean = %.1f / %d\n", s.meanDotsEaten, Maze::GetTotalDots());
    std::printf("  Survival    : Mean = %.1f ticks (%.1f sec @ 18.2Hz)\n",
                s.meanTicks, s.meanTicks / 18.2065);
    std::printf("  Level Clear : %.1f%% (%d total levels cleared)\n",
                s.levelClearRate, s.totalLevelsCleared);
    std::printf("Performance:\n");
    std::printf("  Elapsed     : %.4f s (%.0f ticks/sec)\n",
                s.elapsedSeconds, s.ticksPerSecond);
    std::printf("============================================\n");
}

} // namespace pcman
