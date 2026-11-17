#include "qlearning.hpp"
#include "pathfinder.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>

namespace pcman {

namespace {

constexpr uint32_t QTABLE_MAGIC   = 0x4C545150u; // "PQTL"
constexpr uint16_t QTABLE_VERSION = 2;
constexpr int FRUIT_GRID_Y        = 25;
constexpr int FRUIT_GRID_X        = 26;
constexpr int MAX_CORRIDOR_TRACE_STEPS = 16;

// Count walkable neighbor directions at an intersection (gridY, gridX).
// A branching junction where Pac-Man has a genuine turn choice has degree >= 3.
int IntersectionDegree(const Maze& maze, int gridY, int gridX) {
    int deg = 0;
    for (int d = 0; d < 4; ++d) {
        if (CanMoveInDirection(maze, gridY, gridX, static_cast<Direction>(d))) {
            deg++;
        }
    }
    return deg;
}

// Given an intersection (gridY, gridX) with degree == 2 (a straight corridor cell or 90-degree L-bend)
// entered via `incomingDir`, return the single continuing forward direction (not reversing).
Direction ContinueCorridorDirection(const Maze& maze, int gridY, int gridX, Direction incomingDir) {
    Direction opp = Opposite(incomingDir);
    if (CanMoveInDirection(maze, gridY, gridX, incomingDir)) {
        return incomingDir;
    }
    for (int d = 0; d < 4; ++d) {
        Direction cand = static_cast<Direction>(d);
        if (cand != opp && CanMoveInDirection(maze, gridY, gridX, cand)) {
            return cand;
        }
    }
    return incomingDir;
}

} // namespace

TabularQPacmanController::TabularQPacmanController(TabularQConfig cfg)
    : m_cfg(cfg) {
    for (auto& row : m_qTable) {
        row.fill(0.0f);
    }
    for (auto& row : m_visits) {
        row.fill(0u);
    }
}

void TabularQPacmanController::OnReset() {
    m_hasPrevStep = false;
    m_traceCount = 0;
    m_prevState = 0;
    m_prevAction = Direction::Up;
    m_prevScore = 0;
    m_prevRemainingDots = 0;
    m_prevDotDist = 0;
    m_prevGhostTicks = INF_DIST;
    m_prevFrightGhostDist = INF_DIST;
}

uint16_t TabularQPacmanController::EncodeState(const Pacman& self,
                                               const GameContext& ctx,
                                               const LegalActions& legalActions,
                                               int* outNearestDotDist,
                                               int* outNearestGhostTicks,
                                               int* outCatchableFrightDist) {
    int pacY = self.GetGridY();
    int pacX = self.GetGridX();
    Direction curDir = self.GetDirection();
    uint8_t curDirIdx = static_cast<uint8_t>(curDir) & 3u;

    // 1. Nearest dot direction (0..3) and shortest-path distance
    PathResult dotPath = FindNearestDotForPacman(ctx.maze, self, legalActions);
    if (outNearestDotDist != nullptr) {
        *outNearestDotDist = dotPath.distance;
    }

    uint8_t dotDir = curDirIdx;
    if (dotPath.firstAction != Direction::None && static_cast<uint8_t>(dotPath.firstAction) < 4) {
        dotDir = static_cast<uint8_t>(dotPath.firstAction);
    } else if (!legalActions.Empty() && static_cast<uint8_t>(legalActions[0]) < 4) {
        dotDir = static_cast<uint8_t>(legalActions[0]);
    }

    // 2. Per-direction 4-level Corridor Status (accounting for Pac-Man dot-eating pause ticks
    //    and Ghost 90-degree corner pause ticks):
    //    0 = Blocked (Wall / Illegal)
    //    1 = Safe Open Corridor (ghost arrival tick margin > 6 all the way to next junction)
    //    2 = Caution Open Corridor (ghost arrival tick margin 3..6)
    //    3 = Danger / Corridor Trap (ghost arrival tick margin <= 2 before reaching next junction)
    std::array<uint8_t, 4> dirStatus{ 0, 0, 0, 0 };
    Direction bestEscapeDir = Direction::None;
    int bestEscapeMargin = -INF_DIST;

    std::array<DirectedDistanceMap, 4> ghostMaps{};
    std::array<bool, 4> ghostActive{ false, false, false, false };
    int nearestGhostToPac = INF_DIST;

    if (ctx.ghosts != nullptr) {
        for (size_t i = 0; i < ctx.ghosts->size(); ++i) {
            const Ghost& g = (*ctx.ghosts)[i];
            if (!g.IsFrightened() && !g.IsEaten() && !g.IsInPen() && !g.IsExitingPen()) {
                ghostActive[i] = true;
                ghostMaps[i].ComputeFrom(ctx.maze, g.GetGridY(), g.GetGridX(), g.GetDirection(),
                                         /*includeCornerPause=*/true);
                nearestGhostToPac = std::min(nearestGhostToPac, ghostMaps[i].GetDistance(pacY, pacX));
            }
        }
    }
    if (outNearestGhostTicks != nullptr) {
        *outNearestGhostTicks = nearestGhostToPac;
    }

    for (int d = 0; d < 4; ++d) {
        Direction cand = static_cast<Direction>(d);
        if (!legalActions.IsLegal(cand)) {
            dirStatus[d] = 0; // Blocked (Wall)
            continue;
        }

        if (ctx.ghosts == nullptr) {
            dirStatus[d] = 1; // Safe
            continue;
        }

        // Trace Pac-Man's trajectory along candidate corridor `cand` until reaching the
        // first branching junction (degree >= 3) where he can choose a new turn.
        // Track `pacTicks` including the 1-tick pause on every 2nd dot eaten along the corridor!
        int traceY = pacY;
        int traceX = pacX;
        Direction moveDir = cand;
        int minMargin = INF_DIST;
        int pacTicks = 0;
        int dotsAlongCorridor = 0;

        for (int step = 1; step <= MAX_CORRIDOR_TRACE_STEPS; ++step) {
            if (step == 1 && !IsAtGridIntersection(pacY, pacX) && cand != Opposite(curDir)) {
                // Mid-corridor forward or pre-buffered turn: step 1 reaches the upcoming intersection
                traceY = WrapTunnelGridY(pacY + DeltaRow(curDir));
                traceX = pacX + DeltaCol(curDir);
                moveDir = cand;
            } else {
                traceY = WrapTunnelGridY(traceY + DeltaRow(moveDir));
                traceX = traceX + DeltaCol(moveDir);
            }

            if (!IsValidGridCoord(ctx.maze, traceY, traceX)) {
                break;
            }

            pacTicks++; // 1 BIOS tick to step 1 grid unit

            // Account for the 1-tick dot-chewing pause on alternate dots along this corridor!
            if (IsAtGridIntersection(traceY, traceX)) {
                uint8_t cell = ctx.maze.GetCell(traceY / 2, traceX / 2);
                if (cell == 2 || cell == 3) {
                    dotsAlongCorridor++;
                    if ((dotsAlongCorridor & 1) == 1) {
                        pacTicks++; // Alternate dot eating pause costs 1 extra tick!
                    }
                }
            }

            for (size_t i = 0; i < ctx.ghosts->size(); ++i) {
                if (!ghostActive[i]) continue;
                const Ghost& g = (*ctx.ghosts)[i];

                // Immediate physical proximity check on early steps
                if (step <= 3) {
                    int mDist = ManhattanDist(g.GetGridY(), g.GetGridX(), traceY, traceX);
                    if (mDist <= 2) {
                        minMargin = std::min(minMargin, 0);
                    }
                }

                // Ghost arrival ticks (including 90° corner pauses) vs Pac-Man's `pacTicks` (including dot pauses)
                int gTicks = ghostMaps[i].GetDistance(traceY, traceX);
                int margin = gTicks - pacTicks;
                minMargin = std::min(minMargin, margin);
            }

            if (IsAtGridIntersection(traceY, traceX)) {
                // If this is the intermediate intersection for a mid-corridor pre-buffered turn,
                // continue at least into step 2 along `cand` before stopping at a junction.
                if (step == 1 && !IsAtGridIntersection(pacY, pacX) && cand != Opposite(curDir)) {
                    continue;
                }

                int deg = IntersectionDegree(ctx.maze, traceY, traceX);
                if (deg >= 3) {
                    // Reached a branching junction with multiple escape corridors!
                    break;
                }
                // Straight corridor or 90-degree L-bend (deg == 2): keep tracing to the junction!
                moveDir = ContinueCorridorDirection(ctx.maze, traceY, traceX, moveDir);
            }
        }

        if (minMargin > bestEscapeMargin) {
            bestEscapeMargin = minMargin;
            bestEscapeDir = cand;
        }

        if (minMargin <= 2) {
            dirStatus[d] = 3; // Danger / Corridor Trap
        } else if (minMargin <= 6) {
            dirStatus[d] = 2; // Caution
        } else {
            dirStatus[d] = 1; // Safe Open Corridor
        }
    }

    // 3. Tactical priority direction (0..3, or 4 = None):
    //    Priority A: Catchable frightened ghost anywhere in the maze (accounting for 50% frightened ghost speed
    //                and ensuring the path is not blocked by a respawned active ghost).
    //    Priority B: Nearby energizer (within 12 steps) when an active ghost is pursuing closely.
    //    Priority C: Active bonus fruit when safe.
    //    Priority D: Best empty-corridor escape direction (`bestEscapeDir`) when pursued closely.
    uint8_t huntDir = 4;
    int catchableFrightDist = INF_DIST;

    if (ctx.ghosts != nullptr && ctx.frightTimer >= 14) {
        // A fleeing frightened ghost moves at 50% speed (0.5 steps/tick) while Pac-Man moves at
        // 80-100% speed (~0.9 steps/tick), closing the gap at ~0.4-0.5 steps/tick.
        // Requiring p.distance <= min(28, (frightTimer - 12) / 2) ensures Pac-Man catches the
        // weakened ghost with a safety margin before frightTimer expires!
        int maxCatchableSteps = std::min(28, (ctx.frightTimer - 12) / 2);
        for (const Ghost& g : *ctx.ghosts) {
            if (g.IsFrightened() && !g.IsEaten() && !g.IsInPen() && !g.IsExitingPen()) {
                PathResult p = FindPacmanShortestPath(ctx.maze, self, legalActions, g.GetGridY(), g.GetGridX());
                uint8_t actIdx = static_cast<uint8_t>(p.firstAction);
                if (p.distance <= maxCatchableSteps && p.distance < catchableFrightDist &&
                    actIdx < 4 && dirStatus[actIdx] < 3) {
                    catchableFrightDist = p.distance;
                    huntDir = actIdx;
                }
            }
        }
    }

    if (outCatchableFrightDist != nullptr) {
        *outCatchableFrightDist = catchableFrightDist;
    }

    // If only 1 dot remains on the entire board AND a frightened ghost is catchable,
    // prioritize hunting the weakened ghost over eating the last dot!
    if (catchableFrightDist < INF_DIST && ctx.maze.GetRemainingDots() == 1) {
        if (dotPath.distance <= 3 && dotPath.firstAction != Direction::None) {
            uint8_t lastDotAct = static_cast<uint8_t>(dotPath.firstAction);
            if (lastDotAct < 4 && lastDotAct != huntDir && dirStatus[lastDotAct] == 1) {
                dirStatus[lastDotAct] = 2; // Mark premature level-ending dot direction as Caution
            }
        }
        dotDir = huntDir;
    }

    // Priority B: Energizer baiting when ghosts are not frightened (or no catchable ghost left)
    // and an active ghost is pursuing within 14 ticks.
    if (huntDir == 4 && nearestGhostToPac <= 14) {
        PathResult ep = FindNearestDotForPacman(ctx.maze, self, legalActions, /*energizersOnly=*/true);
        uint8_t actIdx = static_cast<uint8_t>(ep.firstAction);
        if (ep.distance <= 12 && actIdx < 4 && dirStatus[actIdx] < 3) {
            huntDir = actIdx;
        }
    }

    // Priority C: Active bonus fruit when no immediate danger
    if (huntDir == 4 && ctx.fruitActive && ctx.fruitTimer > 9) {
        PathResult fp = FindPacmanShortestPath(ctx.maze, self, legalActions, FRUIT_GRID_Y, FRUIT_GRID_X);
        uint8_t actIdx = static_cast<uint8_t>(fp.firstAction);
        if (fp.distance <= 14 && actIdx < 4 && dirStatus[actIdx] < 3) {
            huntDir = actIdx;
        }
    }

    // Priority D: Escape corridor radar when pursued closely
    if (huntDir == 4 && nearestGhostToPac <= 10 &&
        bestEscapeDir != Direction::None && static_cast<uint8_t>(bestEscapeDir) < 4) {
        huntDir = static_cast<uint8_t>(bestEscapeDir);
    }

    uint16_t statusCode = static_cast<uint16_t>(
        dirStatus[0] + 4u * (dirStatus[1] + 4u * (dirStatus[2] + 4u * dirStatus[3])));

    uint16_t state = static_cast<uint16_t>(
        ((dotDir * 256u + statusCode) * 5u + huntDir) * 4u + curDirIdx);
    return state;
}

void TabularQPacmanController::PushTraceStep(uint16_t state, Direction action, bool cutTrace) {
    if (cutTrace) {
        // Watkins's Q(lambda): non-greedy exploratory action cuts off earlier eligibility trace
        m_traceCount = 0;
    }

    // Replacing traces: if `state` is already in the active trace buffer, remove the older entry
    // so its eligibility is replaced with 1.0f at the current step rather than accumulating > 1.
    size_t writeIdx = 0;
    for (size_t i = 0; i < m_traceCount; ++i) {
        if (m_trace[i].state != state) {
            m_trace[writeIdx++] = m_trace[i];
        }
    }
    m_traceCount = writeIdx;

    size_t maxCap = std::clamp<size_t>(m_cfg.traceCapacity, 1, MAX_TRACE_CAPACITY);
    size_t cap = maxCap;
    if (m_cfg.dynamicTraceScaling && maxCap > 2) {
        float clampedEps = std::clamp(m_cfg.epsilon, 0.0f, 1.0f);
        float greedyWeight = (1.0f - clampedEps) * (1.0f - clampedEps);
        size_t dynamicCap = static_cast<size_t>(
            std::round(2.0f + static_cast<float>(maxCap - 2) * greedyWeight));
        cap = std::clamp<size_t>(dynamicCap, 2, maxCap);
    }

    if (m_traceCount >= cap) {
        // Drop oldest steps so trace fits within `cap`
        size_t drop = (m_traceCount - cap) + 1;
        for (size_t i = drop; i < m_traceCount; ++i) {
            m_trace[i - drop] = m_trace[i];
        }
        m_traceCount -= drop;
    }

    m_trace[m_traceCount++] = TraceEntry{
        .state = state,
        .action = action,
        .eligibility = 1.0f
    };
}

void TabularQPacmanController::ApplyTdUpdate(float reward,
                                             uint16_t nextState,
                                             const LegalActions* nextLegal,
                                             bool terminal) {
    if (!m_cfg.training || !m_hasPrevStep) {
        return;
    }
    uint8_t actIdx = static_cast<uint8_t>(m_prevAction);
    if (m_prevState >= NUM_STATES || actIdx >= NUM_ACTIONS) {
        return;
    }

    float maxNextQ = 0.0f;
    if (!terminal && nextLegal != nullptr && !nextLegal->Empty() && nextState < NUM_STATES) {
        maxNextQ = -std::numeric_limits<float>::infinity();
        for (size_t a = 0; a < NUM_ACTIONS; ++a) {
            Direction d = static_cast<Direction>(a);
            if (nextLegal->IsLegal(d)) {
                maxNextQ = std::max(maxNextQ, m_qTable[nextState][a]);
            }
        }
        if (!std::isfinite(maxNextQ)) {
            maxNextQ = 0.0f;
        }
    }

    // Increment visit count for the just-executed (m_prevState, m_prevAction)
    m_visits[m_prevState][actIdx]++;

    float currentQ = m_qTable[m_prevState][actIdx];
    float target   = terminal ? reward : (reward + m_cfg.gamma * maxNextQ);
    float tdError  = target - currentQ;

    // Propagate `tdError` backward across all active steps in the eligibility trace buffer!
    // When `dynamicTraceScaling` is active, scale `effectiveLambda` alongside the active trace
    // capacity (low lambda when epsilon is high / trace is short, rising to `m_cfg.lambda` as
    // epsilon decays and trace capacity expands) to avoid cliff-edge truncation at small capacities.
    float effectiveLambda = m_cfg.lambda;
    if (m_cfg.dynamicTraceScaling) {
        float clampedEps = std::clamp(m_cfg.epsilon, 0.0f, 1.0f);
        float greedyWeight = (1.0f - clampedEps) * (1.0f - clampedEps);
        effectiveLambda = m_cfg.lambda * (0.20f + 0.80f * greedyWeight);
    }
    float decayFactor = m_cfg.gamma * effectiveLambda;
    size_t writeIdx = 0;
    for (size_t i = 0; i < m_traceCount; ++i) {
        TraceEntry entry = m_trace[i];
        uint8_t eAct = static_cast<uint8_t>(entry.action);
        if (entry.state < NUM_STATES && eAct < NUM_ACTIONS) {
            uint32_t vis = std::max(1u, m_visits[entry.state][eAct]);
            float effectiveAlpha = std::max(0.01f, m_cfg.alpha / (1.0f + 0.02f * static_cast<float>(vis - 1)));
            m_qTable[entry.state][eAct] += effectiveAlpha * entry.eligibility * tdError;
            m_totalUpdates++;
        }

        entry.eligibility *= decayFactor;
        if (!terminal && entry.eligibility >= 1e-4f) {
            m_trace[writeIdx++] = entry;
        }
    }
    m_traceCount = terminal ? 0 : writeIdx;
}

void TabularQPacmanController::OnPacmanDied(const GameContext& ctx) {
    if (m_cfg.training && m_hasPrevStep) {
        int scoreDelta = ctx.score - m_prevScore;
        float reward = static_cast<float>(scoreDelta) - 500.0f;
        ApplyTdUpdate(reward, 0, nullptr, true);
    }
    m_hasPrevStep = false;
    m_traceCount = 0;
}

void TabularQPacmanController::OnLevelCleared(const GameContext& ctx) {
    if (m_cfg.training && m_hasPrevStep) {
        int scoreDelta = ctx.score - m_prevScore;
        int dotsEaten  = std::max(0, m_prevRemainingDots - ctx.maze.GetRemainingDots());
        // If Pac-Man cleared the final dot while a catchable frightened ghost was still alive,
        // withhold the +500 level-clear bonus so the agent prefers eating the weakened ghost first!
        float clearBonus = (m_prevFrightGhostDist < INF_DIST) ? 0.0f : 500.0f;
        float reward = static_cast<float>(scoreDelta) + 5.0f * static_cast<float>(dotsEaten) + clearBonus;
        ApplyTdUpdate(reward, 0, nullptr, true);
    }
    m_hasPrevStep = false;
    m_traceCount = 0;
}

Direction TabularQPacmanController::ChooseDirection(const Pacman& self,
                                                    const GameContext& ctx,
                                                    const LegalActions& legalActions) {
    if (legalActions.Empty()) {
        return self.GetDirection();
    }

    int curDotDist = INF_DIST;
    int curGhostTicks = INF_DIST;
    int curFrightDist = INF_DIST;
    uint16_t state = EncodeState(self, ctx, legalActions, &curDotDist, &curGhostTicks, &curFrightDist);

    // 1. If training and we have a previous step in this round, perform Bellman TD update
    if (m_cfg.training && m_hasPrevStep) {
        int scoreDelta = ctx.score - m_prevScore;
        int dotsEaten  = std::max(0, m_prevRemainingDots - ctx.maze.GetRemainingDots());
        float reward   = static_cast<float>(scoreDelta) + 5.0f * static_cast<float>(dotsEaten);

        // Amplify high-value ghost/fruit captures (which also drive the 10,000-pt extra life)
        if (scoreDelta >= 200) {
            reward += 0.5f * static_cast<float>(scoreDelta);
        }

        // Potential-based shaping:
        // - When a catchable frightened ghost is active on both steps, shape on closing the distance
        //   to the weakened ghost (+4.0 per step closer), overriding normal dot-distance shaping!
        if (m_prevFrightGhostDist < INF_DIST && curFrightDist < INF_DIST) {
            int deltaFright = std::clamp(m_prevFrightGhostDist - curFrightDist, -4, 4);
            reward += 4.0f * static_cast<float>(deltaFright);
        } else if (dotsEaten == 0 && scoreDelta == 0 && m_prevDotDist < INF_DIST && curDotDist < INF_DIST) {
            int deltaDist = std::clamp(m_prevDotDist - curDotDist, -4, 4);
            bool underGhostPursuit = (m_prevGhostTicks <= 10 || curGhostTicks <= 10);
            if (underGhostPursuit && deltaDist < 0) {
                // When a ghost is pursuing closely, do NOT penalize darting into an empty
                // corridor that temporarily increases distance to the nearest dot.
                deltaDist = 0;
            }
            reward += 2.0f * static_cast<float>(deltaDist);
        }

        ApplyTdUpdate(reward, state, &legalActions, false);
    }

    // 2. Compute greedy action set and select action using epsilon-greedy policy
    float bestQ = -std::numeric_limits<float>::infinity();
    LegalActions bestCandidates;

    for (size_t a = 0; a < NUM_ACTIONS; ++a) {
        Direction cand = static_cast<Direction>(a);
        if (!legalActions.IsLegal(cand)) continue;

        float q = m_qTable[state][a];
        if (q > bestQ + 1e-6f) {
            bestQ = q;
            bestCandidates = LegalActions{};
            bestCandidates.Add(cand);
        } else if (std::abs(q - bestQ) <= 1e-6f) {
            bestCandidates.Add(cand);
        }
    }

    Direction chosen = legalActions[0];
    bool explore = false;
    if (m_cfg.training && m_cfg.epsilon > 0.0f) {
        uint16_t r = ctx.rng.Next();
        float u = static_cast<float>(r) / 65536.0f;
        explore = (u < m_cfg.epsilon);
    }

    if (explore) {
        uint16_t r = ctx.rng.Next();
        size_t idx = static_cast<size_t>((static_cast<uint32_t>(r) * static_cast<uint32_t>(legalActions.Size())) >> 16);
        chosen = legalActions[idx];
    } else if (bestCandidates.Size() == 1) {
        chosen = bestCandidates[0];
    } else if (bestCandidates.Size() > 1) {
        uint16_t r = ctx.rng.Next();
        size_t idx = static_cast<size_t>((static_cast<uint32_t>(r) * static_cast<uint32_t>(bestCandidates.Size())) >> 16);
        chosen = bestCandidates[idx];
    }

    // 3. Push onto Watkins's Q(lambda) eligibility trace buffer and record step state:
    //    If an exploratory move picked a sub-optimal action (`!bestCandidates.IsLegal(chosen)`),
    //    cut the trace so earlier greedy actions are not blamed for a random exploratory choice.
    if (m_cfg.training) {
        bool isNonGreedy = explore && !bestCandidates.IsLegal(chosen);
        PushTraceStep(state, chosen, isNonGreedy);
    }
    m_hasPrevStep = true;
    m_prevState = state;
    m_prevAction = chosen;
    m_prevScore = ctx.score;
    m_prevRemainingDots = ctx.maze.GetRemainingDots();
    m_prevDotDist = curDotDist;
    m_prevGhostTicks = curGhostTicks;
    m_prevFrightGhostDist = curFrightDist;

    return chosen;
}

float TabularQPacmanController::GetQValue(uint16_t state, Direction action) const {
    uint8_t a = static_cast<uint8_t>(action);
    if (state >= NUM_STATES || a >= NUM_ACTIONS) return 0.0f;
    return m_qTable[state][a];
}

void TabularQPacmanController::SetQValue(uint16_t state, Direction action, float value) {
    uint8_t a = static_cast<uint8_t>(action);
    if (state < NUM_STATES && a < NUM_ACTIONS) {
        m_qTable[state][a] = value;
    }
}

uint32_t TabularQPacmanController::GetVisitCount(uint16_t state, Direction action) const {
    uint8_t a = static_cast<uint8_t>(action);
    if (state >= NUM_STATES || a >= NUM_ACTIONS) return 0u;
    return m_visits[state][a];
}

size_t TabularQPacmanController::CountNonZeroEntries() const {
    size_t count = 0;
    for (const auto& row : m_qTable) {
        for (float v : row) {
            if (std::abs(v) > 1e-6f) {
                count++;
            }
        }
    }
    return count;
}

bool TabularQPacmanController::SaveToFile(const std::filesystem::path& path) const {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;

    uint32_t magic = QTABLE_MAGIC;
    uint16_t version = QTABLE_VERSION;
    uint16_t numStates = static_cast<uint16_t>(NUM_STATES);
    ofs.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
    ofs.write(reinterpret_cast<const char*>(&version), sizeof(version));
    ofs.write(reinterpret_cast<const char*>(&numStates), sizeof(numStates));
    ofs.write(reinterpret_cast<const char*>(&m_totalUpdates), sizeof(m_totalUpdates));

    for (size_t s = 0; s < NUM_STATES; ++s) {
        for (size_t a = 0; a < NUM_ACTIONS; ++a) {
            uint32_t bits = std::bit_cast<uint32_t>(m_qTable[s][a]);
            uint32_t vis  = m_visits[s][a];
            ofs.write(reinterpret_cast<const char*>(&bits), sizeof(bits));
            ofs.write(reinterpret_cast<const char*>(&vis), sizeof(vis));
        }
    }
    return ofs.good();
}

bool TabularQPacmanController::LoadFromFile(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return false;

    uint32_t magic = 0;
    uint16_t version = 0;
    uint16_t numStates = 0;
    uint64_t updates = 0;
    if (!ifs.read(reinterpret_cast<char*>(&magic), sizeof(magic)) ||
        !ifs.read(reinterpret_cast<char*>(&version), sizeof(version)) ||
        !ifs.read(reinterpret_cast<char*>(&numStates), sizeof(numStates)) ||
        !ifs.read(reinterpret_cast<char*>(&updates), sizeof(updates))) {
        return false;
    }
    if (magic != QTABLE_MAGIC || version != QTABLE_VERSION || numStates != NUM_STATES) {
        return false;
    }

    std::array<std::array<float, NUM_ACTIONS>, NUM_STATES> loadedQ{};
    std::array<std::array<uint32_t, NUM_ACTIONS>, NUM_STATES> loadedVis{};
    for (size_t s = 0; s < NUM_STATES; ++s) {
        for (size_t a = 0; a < NUM_ACTIONS; ++a) {
            uint32_t bits = 0;
            uint32_t vis  = 0;
            if (!ifs.read(reinterpret_cast<char*>(&bits), sizeof(bits)) ||
                !ifs.read(reinterpret_cast<char*>(&vis), sizeof(vis))) {
                return false;
            }
            float v = std::bit_cast<float>(bits);
            if (!std::isfinite(v)) return false;
            loadedQ[s][a] = v;
            loadedVis[s][a] = vis;
        }
    }

    m_qTable = loadedQ;
    m_visits = loadedVis;
    m_totalUpdates = updates;
    return true;
}

std::shared_ptr<TabularQPacmanController> MakeTabularQPacmanController(TabularQConfig cfg) {
    return std::make_shared<TabularQPacmanController>(cfg);
}

} // namespace pcman
