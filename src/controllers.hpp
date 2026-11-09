#pragma once
#include "entities.hpp"
#include "maze.hpp"
#include "rom_data.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace pcman {

// Identifier for each of the 5 controllable characters in the game
enum class CharacterId : uint8_t {
    Pacman = 0,
    Blinky = 1,
    Pinky  = 2,
    Inky   = 3,
    Clyde  = 4
};

// Fixed-capacity set of legal directional key inputs (Up, Down, Left, Right)
// available to a character at a given game state.
struct LegalActions {
    std::array<Direction, 4> actions{
        Direction::None, Direction::None, Direction::None, Direction::None
    };
    uint8_t count = 0;

    constexpr void Add(Direction d) {
        if (count < 4 && !Contains(d)) {
            actions[count++] = d;
        }
    }

    [[nodiscard]] constexpr bool Contains(Direction d) const {
        for (uint8_t i = 0; i < count; ++i) {
            if (actions[i] == d) return true;
        }
        return false;
    }

    [[nodiscard]] constexpr bool Empty() const { return count == 0; }
    [[nodiscard]] constexpr uint8_t Size() const { return count; }
    constexpr Direction operator[](size_t i) const { return actions[i]; }
    [[nodiscard]] constexpr auto begin() const { return actions.begin(); }
    [[nodiscard]] constexpr auto end() const { return actions.begin() + count; }
};

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

// Compute the legal directional key inputs available to Pac-Man at his current position.
// At intersections, returns all walkable directions {Up, Down, Left, Right} (including reversing).
// Midway through a corridor, returns {currentDir, Opposite(currentDir)} plus any perpendicular
// turns open at the intersection 1 step ahead so turns can be pre-buffered at ~6 Hz polling.
LegalActions GetPacmanLegalActions(const Maze& maze, const Pacman& pacman);

// Compute the legal directional key inputs available to a Ghost at its current position.
// At intersections, returns all walkable non-reverse directions (or reverse at a dead end).
// Mid-corridor or in/exiting pen, returns {currentDir}.
LegalActions GetGhostLegalActions(const Maze& maze, const Ghost& ghost);

// Helper: validate/sanitize a proposed ghost direction at an intersection.
// Enforces wall collision and the no-180-reversal rule (unless at a dead end).
Direction SanitizeGhostDirection(const Maze& maze, int gridY, int gridX,
                                 Direction currentDir, Direction proposedDir);

class IPacmanController {
public:
    virtual ~IPacmanController() = default;
    virtual Direction ChooseDirection(const Pacman& self,
                                      const GameContext& ctx,
                                      const LegalActions& legalActions) = 0;
    virtual void OnReset() {}
};

class IGhostController {
public:
    virtual ~IGhostController() = default;
    virtual Direction ChooseDirection(const Ghost& self,
                                      const GameContext& ctx,
                                      const LegalActions& legalActions) = 0;
    virtual Direction ChooseEatenDirection(const Ghost& self,
                                           const GameContext& ctx,
                                           const LegalActions& legalActions);
    virtual void OnReset() {}
};

// Default Pac-Man controller driven by InputState (keyboard/gamepad/test input)
class HumanPacmanController : public IPacmanController {
public:
    Direction ChooseDirection(const Pacman& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override;
};

// Baseline Pac-Man controller that chooses uniformly at random from legalActions
class RandomPacmanController : public IPacmanController {
public:
    Direction ChooseDirection(const Pacman& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override;
};

// Functional wrapper for lambda-based Pac-Man AI strategies
class FnPacmanController : public IPacmanController {
public:
    using FnWithLegal = std::function<Direction(const Pacman&, const GameContext&, const LegalActions&)>;
    using FnSimple    = std::function<Direction(const Pacman&, const GameContext&)>;

    explicit FnPacmanController(FnWithLegal fn) : m_fn(std::move(fn)) {}
    explicit FnPacmanController(FnSimple fn)
        : m_fn([f = std::move(fn)](const Pacman& self, const GameContext& ctx, const LegalActions&) {
              return f ? f(self, ctx) : Direction::None;
          }) {}

    Direction ChooseDirection(const Pacman& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override {
        return m_fn ? m_fn(self, ctx, legalActions) : Direction::None;
    }

private:
    FnWithLegal m_fn;
};

// Authentic Pcman v1.com (1982) roulette-wheel Ghost AI controller
class OriginalGhostController : public IGhostController {
public:
    Direction ChooseDirection(const Ghost& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override;

    static Direction ComputeOriginalDirection(const Ghost& self, const Maze& maze,
                                              int targetGridY, int targetGridX,
                                              uint8_t ghostWeight, bool isFrightened,
                                              LcgRng& rng);
};

// Baseline Ghost controller that chooses uniformly at random from legalActions
class RandomGhostController : public IGhostController {
public:
    Direction ChooseDirection(const Ghost& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override;
};

// Functional wrapper for lambda-based Ghost AI strategies
class FnGhostController : public IGhostController {
public:
    using FnWithLegal = std::function<Direction(const Ghost&, const GameContext&, const LegalActions&)>;
    using FnSimple    = std::function<Direction(const Ghost&, const GameContext&)>;

    explicit FnGhostController(FnWithLegal fn) : m_fn(std::move(fn)) {}
    explicit FnGhostController(FnSimple fn)
        : m_fn([f = std::move(fn)](const Ghost& self, const GameContext& ctx, const LegalActions&) {
              return f ? f(self, ctx) : self.GetDirection();
          }) {}

    Direction ChooseDirection(const Ghost& self,
                              const GameContext& ctx,
                              const LegalActions& legalActions) override {
        return m_fn ? m_fn(self, ctx, legalActions) : self.GetDirection();
    }

private:
    FnWithLegal m_fn;
};

std::shared_ptr<IPacmanController> MakeHumanPacmanController();
std::shared_ptr<IPacmanController> MakeRandomPacmanController();
std::shared_ptr<IGhostController> MakeOriginalGhostController();
std::shared_ptr<IGhostController> MakeRandomGhostController();

// Factory lookup by name:
// Pac-Man names: "human", "random" (returns nullptr if unknown)
// Ghost names:   "original", "random" (returns nullptr if unknown)
std::shared_ptr<IPacmanController> CreatePacmanControllerByName(std::string_view name);
std::shared_ptr<IGhostController> CreateGhostControllerByName(std::string_view name);

} // namespace pcman
