#pragma once
#include "controllers.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace pcman {

struct TabularQConfig {
    float alpha = 0.25f;               // Initial learning rate (decays per state-action visit count)
    float gamma = 0.95f;               // Discount factor
    float lambda = 0.85f;              // Eligibility trace decay factor for Watkins's Q(lambda)
    float epsilon = 0.20f;             // Exploration probability (0.0 when evaluating)
    size_t traceCapacity = 24;         // Maximum steps retained in the eligibility trace buffer (1..MAX_TRACE_CAPACITY)
    bool dynamicTraceScaling = true;   // Scale active trace capacity from 2..traceCapacity as epsilon decays
    bool training = false;             // Enable online Bellman TD(lambda) updates
};

class TabularQPacmanController : public IPacmanController {
public:
    // 4 (dotDir) * 256 (4^4 directional status: Blocked/Safe/Caution/Danger) * 5 (huntDir) * 4 (curDir) = 20,480 states
    static constexpr size_t NUM_STATES         = 4 * 256 * 5 * 4;
    static constexpr size_t NUM_ACTIONS        = 4;
    static constexpr size_t MAX_TRACE_CAPACITY = 64;

    explicit TabularQPacmanController(TabularQConfig cfg = {});

    Direction ChooseDirection(const Pacman& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override;
    void OnReset() override;
    void OnPacmanDied(const GameContext& ctx) override;
    void OnLevelCleared(const GameContext& ctx) override;

    // Discretized Tactical Radar State Encoder (returns state index in 0..20479)
    static uint16_t EncodeState(const Pacman& self,
                                const GameContext& ctx,
                                const LegalActions& legalActions,
                                int* outNearestDotDist = nullptr,
                                int* outNearestGhostTicks = nullptr,
                                int* outCatchableFrightDist = nullptr);

    // Hyperparameter & mode control
    void SetTraining(bool enabled) { m_cfg.training = enabled; }
    [[nodiscard]] bool IsTraining() const { return m_cfg.training; }
    void SetEpsilon(float eps) { m_cfg.epsilon = eps; }
    [[nodiscard]] float GetEpsilon() const { return m_cfg.epsilon; }
    void SetAlpha(float alpha) { m_cfg.alpha = alpha; }
    [[nodiscard]] float GetAlpha() const { return m_cfg.alpha; }
    void SetGamma(float gamma) { m_cfg.gamma = gamma; }
    [[nodiscard]] float GetGamma() const { return m_cfg.gamma; }
    void SetLambda(float lambda) { m_cfg.lambda = lambda; }
    [[nodiscard]] float GetLambda() const { return m_cfg.lambda; }
    void SetTraceCapacity(size_t cap) {
        m_cfg.traceCapacity = (cap < 1) ? 1 : ((cap > MAX_TRACE_CAPACITY) ? MAX_TRACE_CAPACITY : cap);
    }
    [[nodiscard]] size_t GetTraceCapacity() const { return m_cfg.traceCapacity; }
    [[nodiscard]] size_t GetTraceLength() const { return m_traceCount; }

    // Q-table inspection & binary file persistence
    [[nodiscard]] float GetQValue(uint16_t state, Direction action) const;
    void SetQValue(uint16_t state, Direction action, float value);
    [[nodiscard]] uint32_t GetVisitCount(uint16_t state, Direction action) const;
    [[nodiscard]] size_t CountNonZeroEntries() const;
    [[nodiscard]] uint64_t GetTotalUpdates() const { return m_totalUpdates; }

    bool SaveToFile(const std::filesystem::path& path) const;
    bool LoadFromFile(const std::filesystem::path& path);

private:
    struct TraceEntry {
        uint16_t state = 0;
        Direction action = Direction::Up;
        float eligibility = 0.0f;
    };

    TabularQConfig m_cfg{};
    std::array<std::array<float, NUM_ACTIONS>, NUM_STATES> m_qTable{};
    std::array<std::array<uint32_t, NUM_ACTIONS>, NUM_STATES> m_visits{};
    std::array<TraceEntry, MAX_TRACE_CAPACITY> m_trace{};
    size_t m_traceCount = 0;

    bool m_hasPrevStep = false;
    uint16_t m_prevState = 0;
    Direction m_prevAction = Direction::Up;
    int m_prevScore = 0;
    int m_prevRemainingDots = 0;
    int m_prevDotDist = 0;
    int m_prevGhostTicks = 0x3FFF;
    int m_prevFrightGhostDist = 0x3FFF;
    uint64_t m_totalUpdates = 0;

    void PushTraceStep(uint16_t state, Direction action, bool cutTrace);
    void ApplyTdUpdate(float reward, uint16_t nextState, const LegalActions* nextLegal, bool terminal);
};

std::shared_ptr<TabularQPacmanController> MakeTabularQPacmanController(TabularQConfig cfg = {});

} // namespace pcman
