#pragma once
#include "entities.hpp"
#include "maze.hpp"
#include "rom_data.hpp"
#include <array>
#include <functional>
#include <memory>

namespace pcman {

// Platform-independent frame input state
struct InputState {
    Direction desiredDir = Direction::None;
    bool startPressed = false;
    bool escapePressed = false;
    bool muteToggled = false;
};

// Read-only world state passed to Pac-Man and Ghost controllers
struct GameContext {
    const Maze& maze;
    const Pacman& pacman;
    const std::array<Ghost, 4>* ghosts = nullptr; // Non-null during Game::Tick18Hz()
    LevelConfig levelConfig{};
    int level = 1;
    int score = 0;
    int lives = 3;
    int frightTimer = 0;
    bool fruitActive = false;
    int fruitTimer = 0;
    InputState input{};
    LcgRng& rng = GlobalRandom();
};

// Navigation helper: step (gridY, gridX) in direction d with tunnel wrapping
constexpr void StepGridCoord(int& gridY, int& gridX, Direction d) {
    gridY = WrapTunnelGridY(gridY + DeltaRow(d));
    gridX += DeltaCol(d);
}

// Helper: query whether moving in direction `d` from an intersection (gridY, gridX) is walkable
inline bool CanMoveInDirection(const Maze& maze, int gridY, int gridX, Direction d) {
    if (d == Direction::None) return false;
    int nextRow = (gridY / 2) + DeltaRow(d);
    int nextCol = (gridX / 2) + DeltaCol(d);
    return maze.IsWalkable(nextRow, nextCol);
}

// Helper: validate/sanitize a proposed ghost direction at an intersection.
// Enforces wall collision and the no-180-reversal rule (unless at a dead end).
Direction SanitizeGhostDirection(const Maze& maze, int gridY, int gridX,
                                 Direction currentDir, Direction proposedDir);

class IPacmanController {
public:
    virtual ~IPacmanController() = default;
    virtual Direction ChooseDirection(const Pacman& self, const GameContext& ctx) = 0;
    virtual void OnReset() {}
};

class IGhostController {
public:
    virtual ~IGhostController() = default;
    virtual Direction ChooseDirection(const Ghost& self, const GameContext& ctx) = 0;
    virtual Direction ChooseEatenDirection(const Ghost& self, const GameContext& ctx);
    virtual void OnReset() {}
};

// Default Pac-Man controller driven by InputState (keyboard/gamepad/test input)
class HumanPacmanController : public IPacmanController {
public:
    Direction ChooseDirection(const Pacman& self, const GameContext& ctx) override;
};

// Functional wrapper for quick lambda-based Pac-Man AI strategies
class FnPacmanController : public IPacmanController {
public:
    using Fn = std::function<Direction(const Pacman&, const GameContext&)>;
    explicit FnPacmanController(Fn fn) : m_fn(std::move(fn)) {}
    Direction ChooseDirection(const Pacman& self, const GameContext& ctx) override {
        return m_fn ? m_fn(self, ctx) : Direction::None;
    }
private:
    Fn m_fn;
};

// Authentic Pcman v1.com (1982) roulette-wheel Ghost AI controller
class OriginalGhostController : public IGhostController {
public:
    Direction ChooseDirection(const Ghost& self, const GameContext& ctx) override;

    static Direction ComputeOriginalDirection(const Ghost& self, const Maze& maze,
                                              int targetGridY, int targetGridX,
                                              uint8_t ghostWeight, bool isFrightened,
                                              LcgRng& rng);
};

// Functional wrapper for quick lambda-based Ghost AI strategies
class FnGhostController : public IGhostController {
public:
    using Fn = std::function<Direction(const Ghost&, const GameContext&)>;
    explicit FnGhostController(Fn fn) : m_fn(std::move(fn)) {}
    Direction ChooseDirection(const Ghost& self, const GameContext& ctx) override {
        return m_fn ? m_fn(self, ctx) : self.GetDirection();
    }
private:
    Fn m_fn;
};

std::shared_ptr<IPacmanController> MakeHumanPacmanController();
std::shared_ptr<IGhostController> MakeOriginalGhostController();

} // namespace pcman
