#include "benchmark.hpp"
#include "game.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
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

float ComputeTrainingEpsilon(const EpsilonSchedule& sched, int ep, int totalEpisodes) {
    if (totalEpisodes <= 1) {
        return std::clamp(sched.epsStart, 0.0f, 1.0f);
    }
    float progress = static_cast<float>(ep) / static_cast<float>(totalEpisodes - 1);
    float cutoff = std::clamp(sched.epsCutoff, 0.01f, 1.0f);
    if (progress > cutoff + 1e-6f) {
        return 0.0f;
    }

    float t = std::clamp(progress / cutoff, 0.0f, 1.0f);
    if (sched.mode == EpsScheduleMode::FloorClamp) {
        float minEps = std::clamp(sched.epsMin, 0.0f, 1.0f);
        float startEps = std::clamp(sched.epsStart, minEps, 1.0f);
        return std::max(minEps, startEps * std::pow(0.001f, t));
    }

    float startEps = std::clamp(sched.epsStart, 0.0f, 1.0f);
    float minEps = std::clamp(sched.epsMin, 0.0f, startEps);
    if (startEps <= 0.0f) {
        return 0.0f;
    }
    float ratio = std::max(1e-6f, minEps / startEps);
    return std::clamp(startEps * std::pow(ratio, t), minEps, startEps);
}

float ComputeTrainingGamma(const GammaSchedule& sched, float currentEps, int ep, int totalEpisodes) {
    float gEnd = std::clamp(sched.gammaEnd, 0.0f, 0.9999f);
    if (sched.mode == GammaScheduleMode::Fixed || totalEpisodes <= 1) {
        return gEnd;
    }
    float gStart = std::clamp(sched.gammaStart, 0.0f, 0.9999f);
    if (sched.mode == GammaScheduleMode::EpsCoupled) {
        float w = std::clamp(1.0f - currentEps, 0.0f, 1.0f);
        return std::clamp(gStart + (gEnd - gStart) * w, 0.0f, 0.9999f);
    }
    // LinearRamp: smoothly ramp gammaStart -> gammaEnd over the first 80% of training episodes,
    // then hold gammaEnd for the final 20% so Q-values stabilize at the target scale 1/(1 - gammaEnd).
    float progress = static_cast<float>(ep) / static_cast<float>(totalEpisodes - 1);
    float t = std::clamp(progress / 0.80f, 0.0f, 1.0f);
    return std::clamp(gStart + (gEnd - gStart) * t, 0.0f, 0.9999f);
}

void TrainTabularQAgent(TabularQPacmanController& agent,
                        int trainEpisodes,
                        int maxTicksPerEpisode,
                        uint16_t baseSeed,
                        int startLevel,
                        const std::array<std::string, 4>& ghostStrategies,
                        bool verbose,
                        const EpsilonSchedule& epsSchedule,
                        const GammaSchedule& gammaSchedule) {
    if (trainEpisodes <= 0) {
        agent.SetTraining(false);
        agent.SetEpsilon(0.0f);
        return;
    }

    int maxTicks = std::max(1, maxTicksPerEpisode);
    auto agentPtr = std::shared_ptr<TabularQPacmanController>(&agent, [](TabularQPacmanController*) {});

    Game game;
    ConfigureGameControllers(game, "random", ghostStrategies);
    game.SetPacmanController(agentPtr);

    agent.SetTraining(true);
    agent.SetAlpha(0.25f);

    if (verbose) {
        std::printf("=== Training Tabular Q-Learning Pac-Man (%d episodes) ===\n", trainEpisodes);
    }

    int reportInterval = std::max(1, trainEpisodes / 5);
    double windowScoreSum = 0.0;
    double windowDotsSum = 0.0;
    int windowCount = 0;

    GammaSchedule activeGamma = gammaSchedule;
    if (activeGamma.mode == GammaScheduleMode::Fixed) {
        activeGamma.gammaEnd = agent.GetGamma();
    }

    for (int ep = 0; ep < trainEpisodes; ++ep) {
        float eps = ComputeTrainingEpsilon(epsSchedule, ep, trainEpisodes);
        agent.SetEpsilon(eps);
        float curGamma = ComputeTrainingGamma(activeGamma, eps, ep, trainEpisodes);
        agent.SetGamma(curGamma);

        // Offset training seeds by 0x8000 so training seeds do not overlap evaluation seeds
        uint16_t trainSeed = static_cast<uint16_t>(baseSeed + 0x8000u + static_cast<uint16_t>(ep));
        game.StartNewGame(startLevel, trainSeed);
        game.SetState(GameState::Playing);

        int ticks = 0;
        int levelsCleared = 0;
        while (ticks < maxTicks && game.GetState() != GameState::GameOver) {
            GameState st = game.GetState();
            if (st == GameState::Playing) {
                game.Tick18Hz();
                ticks++;
            } else if (st == GameState::Ready) {
                game.SetState(GameState::Playing);
            } else if (st == GameState::PacmanDying) {
                game.Update(2.0f, InputState{});
            } else if (st == GameState::LevelClear) {
                levelsCleared++;
                game.Update(3.0f, InputState{});
            }
        }

        int dotsEaten = levelsCleared * Maze::GetTotalDots() +
                        (Maze::GetTotalDots() - game.GetMaze().GetRemainingDots());
        windowScoreSum += game.GetScore();
        windowDotsSum += dotsEaten;
        windowCount++;

        if (verbose && ((ep + 1) % reportInterval == 0 || ep + 1 == trainEpisodes)) {
            std::printf("  [Ep %4d/%4d] eps=%.3f gamma=%.3f | Avg Score=%.1f | Avg Dots=%.1f | Learned Q-Entries=%zu (Updates=%llu)\n",
                        ep + 1, trainEpisodes, eps, curGamma,
                        windowScoreSum / windowCount,
                        windowDotsSum / windowCount,
                        agent.CountNonZeroEntries(),
                        static_cast<unsigned long long>(agent.GetTotalUpdates()));
            windowScoreSum = 0.0;
            windowDotsSum = 0.0;
            windowCount = 0;
        }
    }

    // Switch to greedy evaluation policy
    agent.SetTraining(false);
    agent.SetEpsilon(0.0f);
}

BenchmarkSummary RunBenchmark(const BenchmarkConfig& config) {
    BenchmarkSummary summary{};
    summary.config = config;

    int numEpisodes = std::max(1, config.episodes);
    int maxTicks = std::max(1, config.maxTicksPerEpisode);
    summary.episodes.reserve(static_cast<size_t>(numEpisodes));

    Game game;
    ConfigureGameControllers(game, config.pacmanStrategy, config.ghostStrategies);

    // If using Tabular Q-Learning, optionally load an existing Q-table and/or run training episodes
    if (auto qAgent = std::dynamic_pointer_cast<TabularQPacmanController>(game.GetPacmanController())) {
        if (!config.qtableFile.empty() && std::filesystem::exists(config.qtableFile)) {
            qAgent->LoadFromFile(config.qtableFile);
        }
        if (config.trainEpisodes > 0) {
            qAgent->SetTraceCapacity(config.traceCapacity);
            qAgent->SetGamma(std::clamp(config.gamma, 0.0f, 0.9999f));
            qAgent->SetLambda(config.lambda);
            GammaSchedule gSched = config.gammaSchedule;
            gSched.gammaEnd = std::clamp(config.gamma, 0.0f, 0.9999f);
            TrainTabularQAgent(*qAgent, config.trainEpisodes, maxTicks,
                               config.baseSeed, config.startLevel,
                               config.ghostStrategies, config.verboseTraining,
                               config.epsSchedule, gSched);
            if (!config.qtableFile.empty()) {
                qAgent->SaveToFile(config.qtableFile);
            }
        }
        qAgent->SetTraining(false);
        qAgent->SetEpsilon(0.0f);
    }

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
