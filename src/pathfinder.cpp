#include "pathfinder.hpp"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace pcman {

namespace {

constexpr int TOTAL_GRID_CELLS = GRID_ROWS * GRID_COLS; // 3,480
constexpr int TOTAL_DIR_STATES = TOTAL_GRID_CELLS * 4;  // 13,920

constexpr int GridIndex(int y, int x) {
    return y * GRID_COLS + x;
}

constexpr int DirStateIndex(int y, int x, Direction d) {
    return (y * GRID_COLS + x) * 4 + static_cast<int>(d);
}

struct DirectedNode {
    int8_t y = 0;
    int8_t x = 0;
    Direction dir = Direction::Up;
};

struct GridNode {
    int8_t y = 0;
    int8_t x = 0;
};

// =========================================================================
// Memoization Cache Structures
// =========================================================================

struct DirectedCacheEntry {
    std::array<int16_t, TOTAL_GRID_CELLS> minDist{};
    std::array<Direction, TOTAL_GRID_CELLS> firstAct{};
};

// Indexed by [includeCornerPause (0..1)][DirStateIndex (0..13919)]
std::array<std::array<std::unique_ptr<DirectedCacheEntry>, TOTAL_DIR_STATES>, 2> s_directedCache{};

struct PacmanCacheEntry {
    uint8_t legalMask = 0;
    std::array<int16_t, TOTAL_GRID_CELLS> dist{};
    std::array<Direction, TOTAL_GRID_CELLS> firstAct{};
    std::vector<int16_t> orderedIntersections{};
};

// Indexed by DirStateIndex(startY, startX, curDir) (0..13919)
std::array<std::unique_ptr<PacmanCacheEntry>, TOTAL_DIR_STATES> s_pacmanCache{};

const DirectedCacheEntry& GetOrComputeDirectedEntry(const Maze& maze,
                                                    int startY, int startX, Direction startDir,
                                                    bool includeCornerPause) {
    int mode = includeCornerPause ? 1 : 0;
    int stateIdx = DirStateIndex(startY, startX, startDir);
    auto& slot = s_directedCache[mode][stateIdx];
    if (slot) {
        return *slot;
    }

    auto entry = std::make_unique<DirectedCacheEntry>();
    entry->minDist.fill(INF_DIST);
    entry->firstAct.fill(Direction::None);

    int startGridIdx = GridIndex(startY, startX);
    entry->minDist[startGridIdx] = 0;
    entry->firstAct[startGridIdx] = startDir;

    std::array<int16_t, TOTAL_DIR_STATES> dist;
    dist.fill(INF_DIST);
    std::array<Direction, TOTAL_DIR_STATES> firstAct;
    firstAct.fill(Direction::None);

    std::array<DirectedNode, TOTAL_DIR_STATES * 2> queue{};
    int head = 0;
    int tail = 0;

    auto enqueue = [&](int y, int x, Direction d, int16_t stepDist, Direction initAct) {
        int idx = DirStateIndex(y, x, d);
        if (dist[idx] <= stepDist) return;
        dist[idx] = stepDist;
        firstAct[idx] = initAct;

        int gIdx = GridIndex(y, x);
        if (stepDist < entry->minDist[gIdx]) {
            entry->minDist[gIdx] = stepDist;
            entry->firstAct[gIdx] = initAct;
        }
        if (tail < static_cast<int>(queue.size())) {
            queue[tail++] = DirectedNode{ static_cast<int8_t>(y), static_cast<int8_t>(x), d };
        }
    };

    if (IsAtGridIntersection(startY, startX)) {
        Direction opp = Opposite(startDir);
        bool anyAdded = false;
        for (int d = 0; d < 4; ++d) {
            Direction cand = static_cast<Direction>(d);
            if (cand != opp && CanMoveInDirection(maze, startY, startX, cand)) {
                int ny = WrapTunnelGridY(startY + DeltaRow(cand));
                int nx = startX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    int16_t cost = (includeCornerPause && cand != startDir) ? 2 : 1;
                    enqueue(ny, nx, cand, cost, cand);
                    anyAdded = true;
                }
            }
        }
        if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, startY, startX, opp)) {
            int ny = WrapTunnelGridY(startY + DeltaRow(opp));
            int nx = startX + DeltaCol(opp);
            if (IsValidGridCoord(maze, ny, nx)) {
                int16_t cost = includeCornerPause ? 2 : 1;
                enqueue(ny, nx, opp, cost, opp);
            }
        }
    } else {
        int ny = WrapTunnelGridY(startY + DeltaRow(startDir));
        int nx = startX + DeltaCol(startDir);
        if (IsValidGridCoord(maze, ny, nx)) {
            enqueue(ny, nx, startDir, 1, startDir);
        }
    }

    while (head < tail) {
        DirectedNode cur = queue[head++];
        int curStateIdx = DirStateIndex(cur.y, cur.x, cur.dir);
        int16_t d = dist[curStateIdx];
        Direction initAct = firstAct[curStateIdx];

        if (IsAtGridIntersection(cur.y, cur.x)) {
            Direction opp = Opposite(cur.dir);
            bool anyAdded = false;
            for (int dirIdx = 0; dirIdx < 4; ++dirIdx) {
                Direction cand = static_cast<Direction>(dirIdx);
                if (cand != opp && CanMoveInDirection(maze, cur.y, cur.x, cand)) {
                    int ny = WrapTunnelGridY(cur.y + DeltaRow(cand));
                    int nx = cur.x + DeltaCol(cand);
                    if (IsValidGridCoord(maze, ny, nx)) {
                        int16_t stepCost = (includeCornerPause && cand != cur.dir) ? 2 : 1;
                        enqueue(ny, nx, cand, static_cast<int16_t>(d + stepCost), initAct);
                        anyAdded = true;
                    }
                }
            }
            if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, cur.y, cur.x, opp)) {
                int ny = WrapTunnelGridY(cur.y + DeltaRow(opp));
                int nx = cur.x + DeltaCol(opp);
                if (IsValidGridCoord(maze, ny, nx)) {
                    int16_t stepCost = includeCornerPause ? 2 : 1;
                    enqueue(ny, nx, opp, static_cast<int16_t>(d + stepCost), initAct);
                }
            }
        } else {
            int ny = WrapTunnelGridY(cur.y + DeltaRow(cur.dir));
            int nx = cur.x + DeltaCol(cur.dir);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, cur.dir, static_cast<int16_t>(d + 1), initAct);
            }
        }
    }

    slot = std::move(entry);
    return *slot;
}

const PacmanCacheEntry& GetOrComputePacmanEntry(const Maze& maze,
                                                const Pacman& pacman,
                                                const LegalActions& legalActions) {
    int startY = WrapTunnelGridY(pacman.GetGridY());
    int startX = pacman.GetGridX();
    Direction curDir = pacman.GetDirection();
    if (curDir == Direction::None) {
        curDir = Direction::Up;
    }

    int stateIdx = DirStateIndex(startY, startX, curDir);
    uint8_t legalMask = 0;
    for (Direction d : legalActions) {
        if (static_cast<uint8_t>(d) < 4) {
            legalMask |= static_cast<uint8_t>(1u << static_cast<uint8_t>(d));
        }
    }

    auto& slot = s_pacmanCache[stateIdx];
    if (slot && slot->legalMask == legalMask) {
        return *slot;
    }

    auto entry = std::make_unique<PacmanCacheEntry>();
    entry->legalMask = legalMask;
    entry->dist.fill(INF_DIST);
    entry->firstAct.fill(Direction::None);
    entry->orderedIntersections.reserve(320);

    int startIdx = GridIndex(startY, startX);
    entry->dist[startIdx] = 0;
    entry->firstAct[startIdx] = legalActions.Empty() ? curDir : legalActions[0];
    if (IsAtGridIntersection(startY, startX)) {
        entry->orderedIntersections.push_back(static_cast<int16_t>(startIdx));
    }

    std::array<GridNode, TOTAL_GRID_CELLS> queue{};
    int head = 0;
    int tail = 0;

    auto enqueue = [&](int y, int x, int16_t stepDist, Direction initialAction) {
        int idx = GridIndex(y, x);
        if (entry->dist[idx] <= stepDist) return;
        entry->dist[idx] = stepDist;
        entry->firstAct[idx] = initialAction;
        queue[tail++] = GridNode{ static_cast<int8_t>(y), static_cast<int8_t>(x) };
    };

    if (IsAtGridIntersection(startY, startX)) {
        for (Direction cand : legalActions) {
            if (CanMoveInDirection(maze, startY, startX, cand)) {
                int ny = WrapTunnelGridY(startY + DeltaRow(cand));
                int nx = startX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, 1, cand);
                }
            }
        }
    } else {
        int interY = WrapTunnelGridY(startY + DeltaRow(curDir));
        int interX = startX + DeltaCol(curDir);
        bool hasCurDir = legalActions.Contains(curDir);

        if (IsValidGridCoord(maze, interY, interX)) {
            int interIdx = GridIndex(interY, interX);
            entry->dist[interIdx] = 1;
            entry->firstAct[interIdx] = hasCurDir ? curDir : legalActions[0];
            if (IsAtGridIntersection(interY, interX)) {
                entry->orderedIntersections.push_back(static_cast<int16_t>(interIdx));
            }
        }

        for (Direction cand : legalActions) {
            if (cand == Opposite(curDir)) {
                int ny = WrapTunnelGridY(startY + DeltaRow(cand));
                int nx = startX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, 1, cand);
                }
            } else if (IsAtGridIntersection(interY, interX) &&
                       CanMoveInDirection(maze, interY, interX, cand)) {
                int ny = WrapTunnelGridY(interY + DeltaRow(cand));
                int nx = interX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, 2, cand);
                }
            }
        }

        if (tail == 0 && IsValidGridCoord(maze, interY, interX)) {
            int interIdx = GridIndex(interY, interX);
            entry->dist[interIdx] = INF_DIST;
            entry->orderedIntersections.clear();
            enqueue(interY, interX, 1, hasCurDir ? curDir : legalActions[0]);
        }
    }

    while (head < tail) {
        GridNode cur = queue[head++];
        int curIdx = GridIndex(cur.y, cur.x);
        int16_t d = entry->dist[curIdx];
        Direction initAct = entry->firstAct[curIdx];

        if (IsAtGridIntersection(cur.y, cur.x)) {
            entry->orderedIntersections.push_back(static_cast<int16_t>(curIdx));
        }

        int16_t nextDist = static_cast<int16_t>(d + 1);
        for (int dirIdx = 0; dirIdx < 4; ++dirIdx) {
            Direction stepDir = static_cast<Direction>(dirIdx);
            if (IsAtGridIntersection(cur.y, cur.x) && !CanMoveInDirection(maze, cur.y, cur.x, stepDir)) {
                continue;
            }
            int ny = WrapTunnelGridY(cur.y + DeltaRow(stepDir));
            int nx = cur.x + DeltaCol(stepDir);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, nextDist, initAct);
            }
        }
    }

    slot = std::move(entry);
    return *slot;
}

} // namespace

bool IsValidGridCoord(const Maze& maze, int gridY, int gridX) {
    gridY = WrapTunnelGridY(gridY);
    if (gridY < 0 || gridY >= GRID_ROWS || gridX < 0 || gridX >= GRID_COLS) {
        return false;
    }

    bool evenY = ((gridY & 1) == 0);
    bool evenX = ((gridX & 1) == 0);

    if (evenY && evenX) {
        return maze.IsWalkable(gridY / 2, gridX / 2);
    }
    if (!evenY && evenX) {
        int r1 = (gridY - 1) / 2;
        int r2 = WrapTunnelGridY(gridY + 1) / 2;
        int c  = gridX / 2;
        return maze.IsWalkable(r1, c) && maze.IsWalkable(r2, c);
    }
    if (evenY && !evenX) {
        int r  = gridY / 2;
        int c1 = (gridX - 1) / 2;
        int c2 = (gridX + 1) / 2;
        return maze.IsWalkable(r, c1) && maze.IsWalkable(r, c2);
    }
    return false;
}

PathResult FindDirectedShortestPath(const Maze& maze,
                                    int startY, int startX, Direction startDir,
                                    int targetY, int targetX) {
    PathResult result{};
    startY  = WrapTunnelGridY(startY);
    targetY = WrapTunnelGridY(targetY);

    if (!IsValidGridCoord(maze, startY, startX) || !IsValidGridCoord(maze, targetY, targetX)) {
        return result;
    }
    if (startDir == Direction::None) {
        startDir = Direction::Up;
    }

    const auto& entry = GetOrComputeDirectedEntry(maze, startY, startX, startDir, /*includeCornerPause=*/false);
    int targetIdx = GridIndex(targetY, targetX);
    int16_t d = entry.minDist[targetIdx];
    if (d < INF_DIST) {
        result.distance = d;
        result.firstAction = entry.firstAct[targetIdx];
        result.targetGridY = targetY;
        result.targetGridX = targetX;
    }
    return result;
}

void DirectedDistanceMap::ComputeFrom(const Maze& maze, int startY, int startX, Direction startDir,
                                      bool includeCornerPause) {
    startY = WrapTunnelGridY(startY);
    if (!IsValidGridCoord(maze, startY, startX)) {
        m_distPtr = nullptr;
        return;
    }
    if (startDir == Direction::None) {
        startDir = Direction::Up;
    }
    const auto& entry = GetOrComputeDirectedEntry(maze, startY, startX, startDir, includeCornerPause);
    m_distPtr = &entry.minDist;
}

int DirectedDistanceMap::GetDistance(int targetY, int targetX) const {
    if (m_distPtr == nullptr) {
        return INF_DIST;
    }
    targetY = WrapTunnelGridY(targetY);
    if (targetY < 0 || targetY >= GRID_ROWS || targetX < 0 || targetX >= GRID_COLS) {
        return INF_DIST;
    }
    return (*m_distPtr)[GridIndex(targetY, targetX)];
}

int EstimateGhostStepsToTarget(const Maze& maze, const Ghost& ghost, int targetY, int targetX) {
    if (ghost.IsEaten()) {
        return INF_DIST;
    }
    if (ghost.IsInPen() || ghost.IsExitingPen()) {
        EntityPosition pos = ghost.GetPos();
        int exitPixelDist = std::abs(pos.x - PEN_DOOR_PIXEL_X) + std::abs(pos.y - PEN_DOOR_PIXEL_Y);
        int exitSteps = (exitPixelDist + 3) / 4;
        if (ghost.IsInPen()) {
            exitSteps += 4;
        }
        int dUp   = FindDirectedShortestPath(maze, 25, PEN_DOOR_GRID_X, Direction::Up, targetY, targetX).distance;
        int dDown = FindDirectedShortestPath(maze, 25, PEN_DOOR_GRID_X, Direction::Down, targetY, targetX).distance;
        int best  = std::min(dUp, dDown);
        return (best >= INF_DIST) ? INF_DIST : (exitSteps + best);
    }

    return FindDirectedShortestPath(maze, ghost.GetGridY(), ghost.GetGridX(),
                                    ghost.GetDirection(), targetY, targetX).distance;
}

PathResult FindPacmanShortestPath(const Maze& maze,
                                  const Pacman& pacman,
                                  const LegalActions& legalActions,
                                  int targetY, int targetX) {
    PathResult result{};
    int startY = WrapTunnelGridY(pacman.GetGridY());
    int startX = pacman.GetGridX();
    targetY = WrapTunnelGridY(targetY);

    if (!IsValidGridCoord(maze, startY, startX) ||
        !IsValidGridCoord(maze, targetY, targetX) ||
        legalActions.Empty()) {
        return result;
    }

    const auto& entry = GetOrComputePacmanEntry(maze, pacman, legalActions);
    int targetIdx = GridIndex(targetY, targetX);
    int16_t d = entry.dist[targetIdx];
    if (d < INF_DIST) {
        result.distance = d;
        result.firstAction = entry.firstAct[targetIdx];
        result.targetGridY = targetY;
        result.targetGridX = targetX;
    }
    return result;
}

PathResult FindNearestDotForPacman(const Maze& maze,
                                   const Pacman& pacman,
                                   const LegalActions& legalActions,
                                   bool energizersOnly) {
    PathResult result{};
    int startY = WrapTunnelGridY(pacman.GetGridY());
    int startX = pacman.GetGridX();
    if (!IsValidGridCoord(maze, startY, startX) || legalActions.Empty()) {
        return result;
    }

    const auto& entry = GetOrComputePacmanEntry(maze, pacman, legalActions);
    for (int16_t gridIdx : entry.orderedIntersections) {
        int y = gridIdx / GRID_COLS;
        int x = gridIdx % GRID_COLS;
        uint8_t cell = maze.GetCell(y / 2, x / 2);
        bool match = energizersOnly ? (cell == 3) : (cell == 2 || cell == 3);
        if (match) {
            result.distance = entry.dist[gridIdx];
            result.firstAction = entry.firstAct[gridIdx];
            result.targetGridY = y;
            result.targetGridX = x;
            return result;
        }
    }
    return result;
}

} // namespace pcman
