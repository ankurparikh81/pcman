#pragma once
#include "controllers.hpp"
#include "entities.hpp"
#include "maze.hpp"
#include <array>
#include <cstdint>

namespace pcman {

inline constexpr int GRID_ROWS = TUNNEL_GRID_ROWS;           // 58 (0..57)
inline constexpr int GRID_COLS = Maze::COLLISION_STRIDE * 2; // 60 (0..59)
inline constexpr int INF_DIST  = 0x3FFF;

struct PathResult {
    int distance = INF_DIST;                  // Grid steps to target (INF_DIST if unreachable)
    Direction firstAction = Direction::None;  // Initial legal key press to take
    int targetGridY = -1;
    int targetGridX = -1;
};

// Check whether a fine grid coordinate (gridY, gridX) lies on a valid maze corridor or intersection
bool IsValidGridCoord(const Maze& maze, int gridY, int gridX);

// =========================================================================
// 1. DIRECTED (NO-180-REVERSAL) SHORTEST PATH (Ghost Movement Physics)
// =========================================================================

// Compute exact shortest path (in grid steps) for an entity starting at (startY, startX)
// with heading `startDir` that is NOT allowed to make 180-degree reversals.
PathResult FindDirectedShortestPath(const Maze& maze,
                                    int startY, int startX, Direction startDir,
                                    int targetY, int targetX);

// Distance Map from a single directed source (startY, startX, startDir) to all grid coordinates.
// Backed by a lazily populated static memoization cache for O(1) zero-copy lookup.
class DirectedDistanceMap {
public:
    using DistGrid = std::array<int16_t, GRID_ROWS * GRID_COLS>;

    DirectedDistanceMap() = default;
    void ComputeFrom(const Maze& maze, int startY, int startX, Direction startDir,
                     bool includeCornerPause = false);

    // Minimum steps/ticks across all arrival directions to reach (targetY, targetX)
    [[nodiscard]] int GetDistance(int targetY, int targetX) const;

private:
    const DistGrid* m_distPtr = nullptr;
};

// Helper for Ghost AI or Pac-Man threat assessment:
// Compute the effective number of grid steps for `ghost` to reach (targetY, targetX),
// accounting for its current direction (no 180-degree reversal) and pen exit delay if in/exiting pen.
int EstimateGhostStepsToTarget(const Maze& maze, const Ghost& ghost, int targetY, int targetX);

// =========================================================================
// 2. REVERSIBLE SHORTEST PATH (Pac-Man Movement Physics)
// =========================================================================

// Compute shortest path for Pac-Man from his current state to (targetY, targetX),
// respecting Pac-Man's `legalActions` for the first action (including pre-buffered
// corner turns when 1 step before an intersection).
PathResult FindPacmanShortestPath(const Maze& maze,
                                  const Pacman& pacman,
                                  const LegalActions& legalActions,
                                  int targetY, int targetX);

// Find the nearest remaining dot (`cell == 2`) or energizer (`cell == 3`) from Pac-Man's
// current state, returning the distance, the target dot's grid coordinates, and the
// legal key press (`firstAction` in `legalActions`) that leads to it.
PathResult FindNearestDotForPacman(const Maze& maze,
                                   const Pacman& pacman,
                                   const LegalActions& legalActions,
                                   bool energizersOnly = false);

} // namespace pcman
