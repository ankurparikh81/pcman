#pragma once
#include "controllers.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pcman {

struct BenchmarkConfig {
    std::string pacmanStrategy = "random";
    std::array<std::string, 4> ghostStrategies = {
        "original", "original", "original", "original"
    };
    int episodes = 100;
    int maxTicksPerEpisode = 10000;
    uint16_t baseSeed = 0;
    int startLevel = 1;
};

struct EpisodeResult {
    int score = 0;
    int dotsEaten = 0;
    int ticksSurvived = 0;
    int levelsCleared = 0;
    bool clearedFirstLevel = false;
    bool timedOut = false;
};

struct BenchmarkSummary {
    BenchmarkConfig config{};
    std::vector<EpisodeResult> episodes{};
    double meanScore = 0.0;
    double stdDevScore = 0.0;
    int minScore = 0;
    int maxScore = 0;
    double meanDotsEaten = 0.0;
    double meanTicks = 0.0;
    int totalLevelsCleared = 0;
    double levelClearRate = 0.0;
    double elapsedSeconds = 0.0;
    double ticksPerSecond = 0.0;
};

// Configure a Game instance's 5 character controllers from strategy names.
// Returns false if any strategy name is unknown.
bool ConfigureGameControllers(class Game& game,
                              std::string_view pacmanStrategy,
                              const std::array<std::string, 4>& ghostStrategies);

BenchmarkSummary RunBenchmark(const BenchmarkConfig& config);
void PrintBenchmarkSummary(const BenchmarkSummary& summary);

} // namespace pcman
