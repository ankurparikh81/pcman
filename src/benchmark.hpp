#pragma once
#include "controllers.hpp"
#include "qlearning.hpp"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace pcman {

enum class EpsScheduleMode : uint8_t {
    FloorClamp = 0, // Existing: max(epsMin, epsStart * 0.001^p)
    ExpCutoff  = 1  // Decay epsStart -> epsMin exponentially at epsCutoff, then 0.000
};

struct EpsilonSchedule {
    EpsScheduleMode mode = EpsScheduleMode::FloorClamp;
    float epsStart = 1.000f;
    float epsMin = 0.010f;
    float epsCutoff = 1.000f; // Fraction [0.01..1.0] where eps reaches epsMin; remaining episodes run at 0.000
};

// Compute the training epsilon for episode `ep` in `[0, totalEpisodes)`.
float ComputeTrainingEpsilon(const EpsilonSchedule& sched, int ep, int totalEpisodes);

enum class GammaScheduleMode : uint8_t {
    Fixed      = 0, // Constant gamma
    LinearRamp = 1, // Linearly ramp gammaStart -> gammaEnd over first 80% of episodes, then hold gammaEnd
    EpsCoupled = 2  // Scale gammaStart -> gammaEnd with (1 - eps) as exploration decays and table fills
};

struct GammaSchedule {
    GammaScheduleMode mode = GammaScheduleMode::Fixed;
    float gammaStart = 0.95f;
    float gammaEnd   = 0.95f;
};

// Compute the training discount factor gamma for episode `ep` in `[0, totalEpisodes)`.
float ComputeTrainingGamma(const GammaSchedule& sched, float currentEps, int ep, int totalEpisodes);

struct BenchmarkConfig {
    std::string pacmanStrategy = "random";
    std::array<std::string, 4> ghostStrategies = {
        "original", "original", "original", "original"
    };
    int episodes = 100;
    int maxTicksPerEpisode = 10000;
    uint16_t baseSeed = 0;
    int startLevel = 1;
    int trainEpisodes = 0;
    size_t traceCapacity = 24;
    float gamma = 0.95f;
    GammaSchedule gammaSchedule{};
    float lambda = 0.85f;
    EpsilonSchedule epsSchedule{};
    std::string qtableFile{};
    bool verboseTraining = true;
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

// Train a TabularQPacmanController across `trainEpisodes` episodes against `ghostStrategies`,
// decaying epsilon according to `epsSchedule` and optionally scaling gamma via `gammaSchedule`,
// then switching to greedy evaluation mode (epsilon = 0).
void TrainTabularQAgent(TabularQPacmanController& agent,
                        int trainEpisodes,
                        int maxTicksPerEpisode,
                        uint16_t baseSeed,
                        int startLevel,
                        const std::array<std::string, 4>& ghostStrategies,
                        bool verbose = true,
                        const EpsilonSchedule& epsSchedule = {},
                        const GammaSchedule& gammaSchedule = {});

BenchmarkSummary RunBenchmark(const BenchmarkConfig& config);
void PrintBenchmarkSummary(const BenchmarkSummary& summary);

} // namespace pcman
