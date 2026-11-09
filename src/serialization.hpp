#pragma once
#include "entities.hpp"
#include "maze.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <type_traits>
#include <vector>

namespace pcman {

enum class GameState : uint8_t {
    TitleScreen = 0,
    Ready,
    Playing,
    PacmanDying,
    LevelClear,
    GameOver
};

struct MazeSnapshot {
    std::array<uint8_t, Maze::COLLISION_SIZE> cells{};
    int32_t remainingDots = 0;

    bool operator==(const MazeSnapshot&) const = default;
};

struct PacmanSnapshot {
    int32_t gridY = 25;
    int32_t gridX = 14;
    Direction currentDir = Direction::Up;
    Direction requestedDir = Direction::Up;
    bool isMoving = true;
    bool closedMouth = false;
    bool isDying = false;
    int32_t mouthFrame = 3;
    int32_t deathFrame = 0;
    int32_t deathTick = 0;

    bool operator==(const PacmanSnapshot&) const = default;
};

struct GhostSnapshot {
    GhostType type = GhostType::Blinky;
    uint8_t flags = GhostFlags::Active;
    Direction dir = Direction::Up;
    uint8_t speedSkip = 0;
    int32_t gridY = 26;
    int32_t gridX = 38;
    int32_t penPixelX = 180;
    int32_t penPixelY = 105;
    int32_t footFrame = 0;

    bool operator==(const GhostSnapshot&) const = default;
};

struct GameSnapshot {
    MazeSnapshot maze{};
    PacmanSnapshot pacman{};
    std::array<GhostSnapshot, 4> ghosts{};
    uint16_t rngSeed = 0;
    GameState state = GameState::TitleScreen;
    int32_t score = 0;
    int32_t highScore = 0;
    int32_t lives = 3;
    int32_t level = 1;
    bool fruitActive = false;
    int32_t fruitTimer = 0;
    int32_t fruitPopupTimer = 0;
    int32_t ghostCombo = 0;
    int32_t dotPauseToggle = 1;
    float ghostFreezeTimer = 0.0f;
    int32_t eatenGhostPosX = 0;
    int32_t eatenGhostPosY = 0;
    int32_t deathTicks = 0;
    bool awardedExtraLife = false;
    float stateTimer = 0.0f;
    float tickAccumulator = 0.0f;
    int32_t frightTimer = 0;
    Direction pendingInputDir = Direction::None;
    uint8_t pacmanPollTimer = 0;

    bool operator==(const GameSnapshot&) const = default;
};

static_assert(std::is_trivially_copyable_v<MazeSnapshot>);
static_assert(std::is_trivially_copyable_v<PacmanSnapshot>);
static_assert(std::is_trivially_copyable_v<GhostSnapshot>);
static_assert(std::is_trivially_copyable_v<GameSnapshot>);

// Binary Wire Format Constants
inline constexpr uint32_t SNAPSHOT_MAGIC   = 0x4E4D4350u; // "PCMN" in little-endian
inline constexpr uint16_t SNAPSHOT_VERSION = 1;

// Validate semantic invariants of a GameSnapshot (bounds, enums, dot count consistency)
bool ValidateSnapshot(const GameSnapshot& snapshot);

// Encode/decode GameSnapshot to/from a portable, padding-free little-endian byte buffer
std::vector<uint8_t> SerializeSnapshot(const GameSnapshot& snapshot);
bool DeserializeSnapshot(std::span<const uint8_t> bytes, GameSnapshot& outSnapshot);

// Stream & file helpers
bool WriteSnapshotToStream(const GameSnapshot& snapshot, std::ostream& os);
bool ReadSnapshotFromStream(std::istream& is, GameSnapshot& outSnapshot);
bool SaveSnapshotToFile(const GameSnapshot& snapshot, const std::filesystem::path& path);
bool LoadSnapshotFromFile(const std::filesystem::path& path, GameSnapshot& outSnapshot);

} // namespace pcman
