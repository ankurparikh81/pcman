#include "pathfinder.hpp"
#include <algorithm>
#include <cmath>

namespace pcman {

namespace {

constexpr int TOTAL_GRID_CELLS = GRID_ROWS * GRID_COLS;       // 3,480
constexpr int TOTAL_DIR_STATES = TOTAL_GRID_CELLS * 4;        // 13,920

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
    if (startY == targetY && startX == targetX) {
        result.distance = 0;
        result.firstAction = startDir;
        result.targetGridY = targetY;
        result.targetGridX = targetX;
        return result;
    }

    std::array<int16_t, TOTAL_DIR_STATES> dist;
    dist.fill(INF_DIST);
    std::array<Direction, TOTAL_DIR_STATES> firstAct;
    firstAct.fill(Direction::None);

    std::array<DirectedNode, TOTAL_DIR_STATES> queue{};
    int head = 0;
    int tail = 0;

    auto enqueue = [&](int y, int x, Direction d, int16_t stepDist, Direction initialAction) {
        int idx = DirStateIndex(y, x, d);
        if (dist[idx] <= stepDist) return;
        dist[idx] = stepDist;
        firstAct[idx] = initialAction;
        queue[tail++] = DirectedNode{ static_cast<int8_t>(y), static_cast<int8_t>(x), d };
    };

    // Expand initial step(s) from (startY, startX, startDir)
    if (IsAtGridIntersection(startY, startX)) {
        Direction opp = Opposite(startDir);
        bool anyAdded = false;
        for (int d = 0; d < 4; ++d) {
            Direction cand = static_cast<Direction>(d);
            if (cand != opp && CanMoveInDirection(maze, startY, startX, cand)) {
                int ny = WrapTunnelGridY(startY + DeltaRow(cand));
                int nx = startX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, cand, 1, cand);
                    anyAdded = true;
                }
            }
        }
        if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, startY, startX, opp)) {
            int ny = WrapTunnelGridY(startY + DeltaRow(opp));
            int nx = startX + DeltaCol(opp);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, opp, 1, opp);
            }
        }
    } else {
        // Mid-corridor: must continue in startDir
        int ny = WrapTunnelGridY(startY + DeltaRow(startDir));
        int nx = startX + DeltaCol(startDir);
        if (IsValidGridCoord(maze, ny, nx)) {
            enqueue(ny, nx, startDir, 1, startDir);
        }
    }

    while (head < tail) {
        DirectedNode cur = queue[head++];
        int curIdx = DirStateIndex(cur.y, cur.x, cur.dir);
        int16_t d = dist[curIdx];
        Direction initAct = firstAct[curIdx];

        if (cur.y == targetY && cur.x == targetX) {
            result.distance = d;
            result.firstAction = initAct;
            result.targetGridY = targetY;
            result.targetGridX = targetX;
            return result;
        }

        int16_t nextDist = static_cast<int16_t>(d + 1);
        if (IsAtGridIntersection(cur.y, cur.x)) {
            Direction opp = Opposite(cur.dir);
            bool anyAdded = false;
            for (int dirIdx = 0; dirIdx < 4; ++dirIdx) {
                Direction cand = static_cast<Direction>(dirIdx);
                if (cand != opp && CanMoveInDirection(maze, cur.y, cur.x, cand)) {
                    int ny = WrapTunnelGridY(cur.y + DeltaRow(cand));
                    int nx = cur.x + DeltaCol(cand);
                    if (IsValidGridCoord(maze, ny, nx)) {
                        enqueue(ny, nx, cand, nextDist, initAct);
                        anyAdded = true;
                    }
                }
            }
            if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, cur.y, cur.x, opp)) {
                int ny = WrapTunnelGridY(cur.y + DeltaRow(opp));
                int nx = cur.x + DeltaCol(opp);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, opp, nextDist, initAct);
                }
            }
        } else {
            int ny = WrapTunnelGridY(cur.y + DeltaRow(cur.dir));
            int nx = cur.x + DeltaCol(cur.dir);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, cur.dir, nextDist, initAct);
            }
        }
    }

    return result;
}

DirectedDistanceMap::DirectedDistanceMap() {
    m_minDist.fill(INF_DIST);
}

void DirectedDistanceMap::ComputeFrom(const Maze& maze, int startY, int startX, Direction startDir) {
    m_minDist.fill(INF_DIST);
    startY = WrapTunnelGridY(startY);
    if (!IsValidGridCoord(maze, startY, startX)) {
        return;
    }
    if (startDir == Direction::None) {
        startDir = Direction::Up;
    }

    m_minDist[GridIndex(startY, startX)] = 0;

    std::array<int16_t, TOTAL_DIR_STATES> dist;
    dist.fill(INF_DIST);
    std::array<DirectedNode, TOTAL_DIR_STATES> queue{};
    int head = 0;
    int tail = 0;

    auto enqueue = [&](int y, int x, Direction d, int16_t stepDist) {
        int idx = DirStateIndex(y, x, d);
        if (dist[idx] <= stepDist) return;
        dist[idx] = stepDist;
        int gIdx = GridIndex(y, x);
        if (stepDist < m_minDist[gIdx]) {
            m_minDist[gIdx] = stepDist;
        }
        queue[tail++] = DirectedNode{ static_cast<int8_t>(y), static_cast<int8_t>(x), d };
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
                    enqueue(ny, nx, cand, 1);
                    anyAdded = true;
                }
            }
        }
        if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, startY, startX, opp)) {
            int ny = WrapTunnelGridY(startY + DeltaRow(opp));
            int nx = startX + DeltaCol(opp);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, opp, 1);
            }
        }
    } else {
        int ny = WrapTunnelGridY(startY + DeltaRow(startDir));
        int nx = startX + DeltaCol(startDir);
        if (IsValidGridCoord(maze, ny, nx)) {
            enqueue(ny, nx, startDir, 1);
        }
    }

    while (head < tail) {
        DirectedNode cur = queue[head++];
        int16_t d = dist[DirStateIndex(cur.y, cur.x, cur.dir)];
        int16_t nextDist = static_cast<int16_t>(d + 1);

        if (IsAtGridIntersection(cur.y, cur.x)) {
            Direction opp = Opposite(cur.dir);
            bool anyAdded = false;
            for (int dirIdx = 0; dirIdx < 4; ++dirIdx) {
                Direction cand = static_cast<Direction>(dirIdx);
                if (cand != opp && CanMoveInDirection(maze, cur.y, cur.x, cand)) {
                    int ny = WrapTunnelGridY(cur.y + DeltaRow(cand));
                    int nx = cur.x + DeltaCol(cand);
                    if (IsValidGridCoord(maze, ny, nx)) {
                        enqueue(ny, nx, cand, nextDist);
                        anyAdded = true;
                    }
                }
            }
            if (!anyAdded && opp != Direction::None && CanMoveInDirection(maze, cur.y, cur.x, opp)) {
                int ny = WrapTunnelGridY(cur.y + DeltaRow(opp));
                int nx = cur.x + DeltaCol(opp);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, opp, nextDist);
                }
            }
        } else {
            int ny = WrapTunnelGridY(cur.y + DeltaRow(cur.dir));
            int nx = cur.x + DeltaCol(cur.dir);
            if (IsValidGridCoord(maze, ny, nx)) {
                enqueue(ny, nx, cur.dir, nextDist);
            }
        }
    }
}

int DirectedDistanceMap::GetDistance(int targetY, int targetX) const {
    targetY = WrapTunnelGridY(targetY);
    if (targetY < 0 || targetY >= GRID_ROWS || targetX < 0 || targetX >= GRID_COLS) {
        return INF_DIST;
    }
    return m_minDist[GridIndex(targetY, targetX)];
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

namespace {

// Internal helper for Pac-Man reversible BFS seeded from `legalActions`
template <typename GoalPredicate>
PathResult RunPacmanBfs(const Maze& maze,
                        const Pacman& pacman,
                        const LegalActions& legalActions,
                        GoalPredicate&& isGoal) {
    PathResult result{};
    int startY = WrapTunnelGridY(pacman.GetGridY());
    int startX = pacman.GetGridX();
    Direction curDir = pacman.GetDirection();

    if (!IsValidGridCoord(maze, startY, startX) || legalActions.Empty()) {
        return result;
    }

    if (isGoal(startY, startX)) {
        result.distance = 0;
        result.firstAction = legalActions[0];
        result.targetGridY = startY;
        result.targetGridX = startX;
        return result;
    }

    std::array<int16_t, TOTAL_GRID_CELLS> dist;
    dist.fill(INF_DIST);
    std::array<Direction, TOTAL_GRID_CELLS> firstAct;
    firstAct.fill(Direction::None);

    std::array<GridNode, TOTAL_GRID_CELLS> queue{};
    int head = 0;
    int tail = 0;

    dist[GridIndex(startY, startX)] = 0;

    auto enqueue = [&](int y, int x, int16_t stepDist, Direction initialAction) {
        int idx = GridIndex(y, x);
        if (dist[idx] <= stepDist) return;
        dist[idx] = stepDist;
        firstAct[idx] = initialAction;
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
        // Mid-corridor:
        // - cand == Opposite(curDir): immediate 1-step reversal
        // - cand == curDir: step 1 to upcoming intersection, then continue straight on step 2
        //   (unless the upcoming intersection itself is the goal!)
        // - cand is a perpendicular pre-buffered turn: step 1 to upcoming intersection, then turn `cand` on step 2
        int interY = WrapTunnelGridY(startY + DeltaRow(curDir));
        int interX = startX + DeltaCol(curDir);
        bool hasCurDir = legalActions.Contains(curDir);

        // Check if the upcoming intersection 1 step ahead already satisfies the goal
        if (IsValidGridCoord(maze, interY, interX) && isGoal(interY, interX)) {
            Direction act = hasCurDir ? curDir : legalActions[0];
            result.distance = 1;
            result.firstAction = act;
            result.targetGridY = interY;
            result.targetGridX = interX;
            return result;
        }

        // Mark the upcoming intersection as visited at dist 1 so 2-step committed branches don't backtrack through it
        if (IsValidGridCoord(maze, interY, interX)) {
            dist[GridIndex(interY, interX)] = 1;
            firstAct[GridIndex(interY, interX)] = hasCurDir ? curDir : legalActions[0];
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
                // Both `curDir` (continuing straight through the intersection) and perpendicular
                // pre-buffered turns reach their respective corridors at step 2.
                int ny = WrapTunnelGridY(interY + DeltaRow(cand));
                int nx = interX + DeltaCol(cand);
                if (IsValidGridCoord(maze, ny, nx)) {
                    enqueue(ny, nx, 2, cand);
                }
            }
        }

        // If continuing `curDir` hits a wall at `(interY, interX)`, still enqueue `(interY, interX)` at dist 1
        if (tail == 0 && IsValidGridCoord(maze, interY, interX)) {
            dist[GridIndex(interY, interX)] = INF_DIST;
            enqueue(interY, interX, 1, hasCurDir ? curDir : legalActions[0]);
        }
    }

    while (head < tail) {
        GridNode cur = queue[head++];
        int curIdx = GridIndex(cur.y, cur.x);
        int16_t d = dist[curIdx];
        Direction initAct = firstAct[curIdx];

        if (isGoal(cur.y, cur.x)) {
            result.distance = d;
            result.firstAction = initAct;
            result.targetGridY = cur.y;
            result.targetGridX = cur.x;
            return result;
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

    return result;
}

} // namespace

PathResult FindPacmanShortestPath(const Maze& maze,
                                  const Pacman& pacman,
                                  const LegalActions& legalActions,
                                  int targetY, int targetX) {
    targetY = WrapTunnelGridY(targetY);
    if (!IsValidGridCoord(maze, targetY, targetX)) {
        return PathResult{};
    }
    return RunPacmanBfs(maze, pacman, legalActions, [&](int y, int x) {
        return y == targetY && x == targetX;
    });
}

PathResult FindNearestDotForPacman(const Maze& maze,
                                   const Pacman& pacman,
                                   const LegalActions& legalActions,
                                   bool energizersOnly) {
    return RunPacmanBfs(maze, pacman, legalActions, [&](int y, int x) {
        if (!IsAtGridIntersection(y, x)) return false;
        uint8_t cell = maze.GetCell(y / 2, x / 2);
        if (energizersOnly) {
            return cell == 3;
        }
        return cell == 2 || cell == 3;
    });
}

} // namespace pcman
