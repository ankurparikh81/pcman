#include "controllers.hpp"
#include <array>

namespace pcman {

LegalActions GetPacmanLegalActions(const Maze& maze, const Pacman& pacman) {
    LegalActions legal;
    int gy = pacman.GetGridY();
    int gx = pacman.GetGridX();

    if (IsAtGridIntersection(gy, gx)) {
        for (int d = 0; d < 4; ++d) {
            Direction cand = static_cast<Direction>(d);
            if (CanMoveInDirection(maze, gy, gx, cand)) {
                legal.Add(cand);
            }
        }
    } else {
        // Midway through a corridor: Pac-Man can continue forward, reverse 180 degrees,
        // or pre-buffer a perpendicular turn available at the intersection 1 step ahead.
        Direction curDir = pacman.GetDirection();
        if (curDir != Direction::None) {
            legal.Add(curDir);
            legal.Add(Opposite(curDir));

            int nextY = WrapTunnelGridY(gy + DeltaRow(curDir));
            int nextX = gx + DeltaCol(curDir);
            if (IsAtGridIntersection(nextY, nextX)) {
                for (int d = 0; d < 4; ++d) {
                    Direction cand = static_cast<Direction>(d);
                    if (cand != curDir && cand != Opposite(curDir) &&
                        CanMoveInDirection(maze, nextY, nextX, cand)) {
                        legal.Add(cand);
                    }
                }
            }
        }
    }

    if (legal.Empty() && pacman.GetDirection() != Direction::None) {
        legal.Add(pacman.GetDirection());
    }
    return legal;
}

LegalActions GetGhostLegalActions(const Maze& maze, const Ghost& ghost) {
    LegalActions legal;
    Direction curDir = ghost.GetDirection();

    if (ghost.IsInPen() || ghost.IsExitingPen() || !IsAtGridIntersection(ghost.GetGridY(), ghost.GetGridX())) {
        if (curDir != Direction::None) {
            legal.Add(curDir);
        }
        return legal;
    }

    int gy = ghost.GetGridY();
    int gx = ghost.GetGridX();
    Direction oppDir = Opposite(curDir);

    // Collect all walkable non-reverse directions (Up, Down, Left, Right)
    for (int d = 0; d < 4; ++d) {
        Direction cand = static_cast<Direction>(d);
        if (cand != oppDir && CanMoveInDirection(maze, gy, gx, cand)) {
            legal.Add(cand);
        }
    }

    // Dead end fallback: allow reversal if it is the only walkable exit
    if (legal.Empty() && oppDir != Direction::None && CanMoveInDirection(maze, gy, gx, oppDir)) {
        legal.Add(oppDir);
    }

    if (legal.Empty() && curDir != Direction::None) {
        legal.Add(curDir);
    }
    return legal;
}

Direction SanitizeGhostDirection(const Maze& maze, int gridY, int gridX,
                                 Direction currentDir, Direction proposedDir) {
    Direction oppDir = Opposite(currentDir);

    // 1. Accept proposedDir if it is walkable and not a 180-degree reversal
    if (proposedDir != Direction::None && proposedDir != oppDir &&
        CanMoveInDirection(maze, gridY, gridX, proposedDir)) {
        return proposedDir;
    }

    // 2. Otherwise, prefer continuing straight if walkable
    if (currentDir != Direction::None && CanMoveInDirection(maze, gridY, gridX, currentDir)) {
        return currentDir;
    }

    // 3. Otherwise, pick the first walkable non-reverse neighbor (Up, Down, Left, Right)
    for (int d = 0; d < 4; ++d) {
        Direction candidate = static_cast<Direction>(d);
        if (candidate != oppDir && CanMoveInDirection(maze, gridY, gridX, candidate)) {
            return candidate;
        }
    }

    // 4. Dead end: allow 180-degree reversal if walkable
    if (oppDir != Direction::None && CanMoveInDirection(maze, gridY, gridX, oppDir)) {
        return oppDir;
    }

    return currentDir;
}

Direction HumanPacmanController::ChooseDirection(const Pacman& /*self*/,
                                                 const GameContext& ctx,
                                                 const LegalActions& /*legalActions*/) {
    return ctx.input.desiredDir;
}

Direction RandomPacmanController::ChooseDirection(const Pacman& self,
                                                  const GameContext& ctx,
                                                  const LegalActions& legalActions) {
    if (legalActions.Empty()) {
        return self.GetDirection();
    }
    if (legalActions.Size() == 1) {
        return legalActions[0];
    }

    uint16_t rnd = ctx.rng.Next();
    size_t idx = static_cast<size_t>((static_cast<uint32_t>(rnd) * static_cast<uint32_t>(legalActions.Size())) >> 16);
    return legalActions[idx];
}

// Exact translation of the 8086 Ghost AI decision function at 0x095A..0x0AFF in Pcman v1.com
Direction OriginalGhostController::ComputeOriginalDirection(const Ghost& self, const Maze& maze,
                                                            int targetGridY, int targetGridX,
                                                            uint8_t ghostWeight, bool isFrightened,
                                                            LcgRng& rng) {
    // 1. Initialize weights for all 4 directions to 1:
    // Index 0 = Up, 1 = Down, 2 = Left, 3 = Right
    std::array<int, 4> weights = {1, 1, 1, 1};

    int gridY = self.GetGridY();
    int gridX = self.GetGridX();
    int weight = ghostWeight;

    // Vertical preference:
    int dy = targetGridY - gridY;
    if (isFrightened) dy = -dy; // Reverse target in frightened mode (run away from Pac-Man!)
    if (dy < 0) {
        weights[static_cast<int>(Direction::Up)] = weight;
    } else if (dy > 0) {
        weights[static_cast<int>(Direction::Down)] = weight;
    }

    // Horizontal preference:
    int dx = targetGridX - gridX;
    if (isFrightened) dx = -dx; // Reverse target in frightened mode
    if (dx < 0) {
        weights[static_cast<int>(Direction::Left)] = weight;
    } else if (dx > 0) {
        weights[static_cast<int>(Direction::Right)] = weight;
    }

    // 3. Wall collision check (0x0A94..0x0AAD):
    // Any direction that hits a wall (cell value == 0) receives weight 0!
    int curRow = gridY / 2;
    int curCol = gridX / 2;

    static constexpr std::array<int, 4> dRow = {-1, 1, 0, 0}; // Up, Down, Left, Right
    static constexpr std::array<int, 4> dCol = {0, 0, -1, 1};

    for (int d = 0; d < 4; ++d) {
        int nr = curRow + dRow[d];
        int nc = curCol + dCol[d];
        if (!maze.IsWalkable(nr, nc)) {
            weights[d] = 0;
        }
    }

    // 4. Inertia & Reversal Prohibition (0x0AAF..0x0AD8):
    Direction curDir = self.GetDirection();
    int curDirIdx = static_cast<int>(curDir);
    if (curDirIdx >= 0 && curDirIdx < 4) {
        // Double weight of continuing straight UNLESS speedSkip == 1
        // OR in vertical tunnel (gridX == 32 && (curDir == Up || curDir == Down)):
        bool inVerticalTunnel = (gridX == TUNNEL_GRID_X) && (curDir == Direction::Up || curDir == Direction::Down);
        if (self.GetSpeedSkip() != 1 && !inVerticalTunnel) {
            weights[curDirIdx] *= 2;
        }

        // Zero weight for turning 180 degrees backward (reversals are strictly forbidden)
        int oppDirIdx = curDirIdx ^ 1;
        weights[oppDirIdx] = 0;
    }

    // 5. Roulette-wheel selection using 16-bit PRNG (0x0AD9..0x0AFE):
    int sum = weights[0] + weights[1] + weights[2] + weights[3];
    if (sum <= 0) {
        // Dead end or all blocked: reverse or find any walkable neighbor
        for (int d = 0; d < 4; ++d) {
            int nr = curRow + dRow[d];
            int nc = curCol + dCol[d];
            if (maze.IsWalkable(nr, nc)) {
                return static_cast<Direction>(d);
            }
        }
        return curDir;
    }

    uint16_t rnd = rng.Next();
    int threshold = static_cast<int>((static_cast<uint32_t>(rnd) * static_cast<uint32_t>(sum)) >> 16);

    for (int d = 0; d < 4; ++d) {
        threshold -= weights[d];
        if (threshold < 0) {
            return static_cast<Direction>(d);
        }
    }

    return Direction::Right;
}

Direction OriginalGhostController::ChooseDirection(const Ghost& self,
                                                   const GameContext& ctx,
                                                   const LegalActions& /*legalActions*/) {
    return ComputeOriginalDirection(self, ctx.maze,
                                    ctx.pacman.GetGridY(), ctx.pacman.GetGridX(),
                                    ctx.levelConfig.ghostWeight, self.IsFrightened(),
                                    ctx.rng);
}

Direction RandomGhostController::ChooseDirection(const Ghost& self,
                                                 const GameContext& ctx,
                                                 const LegalActions& legalActions) {
    if (legalActions.Empty()) {
        return self.GetDirection();
    }
    if (legalActions.Size() == 1) {
        return legalActions[0];
    }

    uint16_t rnd = ctx.rng.Next();
    size_t idx = static_cast<size_t>((static_cast<uint32_t>(rnd) * static_cast<uint32_t>(legalActions.Size())) >> 16);
    return legalActions[idx];
}

Direction IGhostController::ChooseEatenDirection(const Ghost& self,
                                                 const GameContext& ctx,
                                                 const LegalActions& /*legalActions*/) {
    // Eaten eyes returning to pen entrance door at (grid_y = 26, grid_x = 38) with maximum weight 0x3C (60)
    return OriginalGhostController::ComputeOriginalDirection(
        self, ctx.maze, PEN_DOOR_GRID_Y, PEN_DOOR_GRID_X, 0x3C, false, ctx.rng);
}

std::shared_ptr<IPacmanController> MakeHumanPacmanController() {
    return std::make_shared<HumanPacmanController>();
}

std::shared_ptr<IPacmanController> MakeRandomPacmanController() {
    return std::make_shared<RandomPacmanController>();
}

std::shared_ptr<IGhostController> MakeOriginalGhostController() {
    return std::make_shared<OriginalGhostController>();
}

std::shared_ptr<IGhostController> MakeRandomGhostController() {
    return std::make_shared<RandomGhostController>();
}

std::shared_ptr<IPacmanController> CreatePacmanControllerByName(std::string_view name) {
    if (name == "human") {
        return MakeHumanPacmanController();
    }
    if (name == "random") {
        return MakeRandomPacmanController();
    }
    return nullptr;
}

std::shared_ptr<IGhostController> CreateGhostControllerByName(std::string_view name) {
    if (name == "original") {
        return MakeOriginalGhostController();
    }
    if (name == "random") {
        return MakeRandomGhostController();
    }
    return nullptr;
}

} // namespace pcman
