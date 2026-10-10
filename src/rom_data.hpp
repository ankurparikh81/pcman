#pragma once
#include "game_data.hpp"
#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <algorithm>

namespace pcman {

// CGA Mode 4 dimensions
inline constexpr int CGA_WIDTH = 320;
inline constexpr int CGA_HEIGHT = 200;

// Tile grid dimensions
inline constexpr int MAZE_COLS = 62;
inline constexpr int MAZE_ROWS = 50;
inline constexpr int TILE_WIDTH = 4;
inline constexpr int TILE_HEIGHT = 4;

// Dot map dimensions
inline constexpr int DOT_COLS = 29;
inline constexpr int DOT_ROWS = 24;
inline constexpr int TOTAL_PELLETS = 230;

// Sprite dimensions
inline constexpr int SPRITE_WIDTH = 16;
inline constexpr int SPRITE_HEIGHT = 14;
inline constexpr int SPRITE_BYTES = 56;

// Logo dimensions
inline constexpr int LOGO_LETTERS = 6;
inline constexpr int LOGO_LETTER_WIDTH = 36;
inline constexpr int LOGO_LETTER_HEIGHT = 23;
inline constexpr int LOGO_LETTER_BYTES = 207;

// Compile-time guards: dimension constants must agree with the embedded ROM tables
// (CGA Mode 4 is 2 bits per pixel => 4 pixels per byte)
static_assert(data::MAZE_GRID.size() == MAZE_COLS * MAZE_ROWS);
static_assert(SPRITE_BYTES == (SPRITE_WIDTH / 4) * SPRITE_HEIGHT);
static_assert(LOGO_LETTER_BYTES == (LOGO_LETTER_WIDTH / 4) * LOGO_LETTER_HEIGHT);
static_assert(std::size(data::LOGO_LETTERS) == LOGO_LETTERS);
static_assert(data::LOGO_LETTERS[0].size() == LOGO_LETTER_BYTES);
static_assert(data::PACMAN_SPRITES[0].size() == SPRITE_BYTES);
static_assert(data::DEATH_SPRITES[0].size() == SPRITE_BYTES);
static_assert(data::GHOST_SPRITES[0][0][0].size() == SPRITE_BYTES);
static_assert(data::FRUITS[0].sprite.size() == SPRITE_BYTES);

namespace detail {
// Number of dots (2) and energizers (3) in the initial collision map
constexpr int CountInitialDots() {
    int n = 0;
    for (uint8_t d : data::COLLISION_MAP) {
        if (d == 2 || d == 3) ++n;
    }
    return n;
}

constexpr int LastIndex(std::size_t count) { return static_cast<int>(count) - 1; }
} // namespace detail

inline constexpr int INITIAL_DOT_COUNT = detail::CountInitialDots();
static_assert(INITIAL_DOT_COUNT == TOTAL_PELLETS, "COLLISION_MAP pellet count != TOTAL_PELLETS");

using LevelConfig = data::LevelConfig;

class RomData {
public:
    static constexpr size_t Size() { return 16448; }

    static constexpr std::span<const uint8_t, 4> GetMazeTile(uint8_t tileId) {
        if (tileId >= std::size(data::MAZE_TILES)) tileId = 0;
        return std::span<const uint8_t, 4>(data::MAZE_TILES[tileId].data(), 4);
    }

    static constexpr uint8_t GetMazeGridTile(int col, int row) {
        if (col < 0 || col >= MAZE_COLS || row < 0 || row >= MAZE_ROWS) return 0;
        return data::MAZE_GRID[row * MAZE_COLS + col];
    }

    static constexpr uint8_t GetDotMapInitial(int col, int row) {
        if (col < 0 || col >= DOT_COLS || row < 0 || row >= DOT_ROWS) return 0;
        constexpr int DOT_STRIDE = 30;
        size_t index = 31 + static_cast<size_t>(row) * DOT_STRIDE + col;
        return (index < data::COLLISION_MAP.size()) ? data::COLLISION_MAP[index] : 0;
    }

    static constexpr std::span<const uint8_t, 16> GetDotBitmap(uint8_t dotType) {
        if (dotType >= std::size(data::DOT_BITMAPS)) dotType = 0;
        return std::span<const uint8_t, 16>(data::DOT_BITMAPS[dotType].data(), 16);
    }

    static constexpr std::span<const uint8_t, SPRITE_BYTES> GetPacmanSprite(int direction, int mouthFrame) {
        direction = std::clamp(direction, 0, 3);
        mouthFrame = std::clamp(mouthFrame, 0, 3);
        return std::span<const uint8_t, SPRITE_BYTES>(data::PACMAN_SPRITES[direction * 4 + mouthFrame].data(), SPRITE_BYTES);
    }

    static constexpr std::span<const uint8_t, SPRITE_BYTES> GetDeathSprite(int frame) {
        frame = std::clamp(frame, 0, detail::LastIndex(std::size(data::DEATH_SPRITES)));
        return std::span<const uint8_t, SPRITE_BYTES>(data::DEATH_SPRITES[frame].data(), SPRITE_BYTES);
    }

    static constexpr std::span<const uint8_t, SPRITE_BYTES> GetGhostSprite(int variation, int direction, int footFrame) {
        variation = std::clamp(variation, 0, detail::LastIndex(std::size(data::GHOST_SPRITES)));
        direction = std::clamp(direction, 0, 3);
        footFrame = (footFrame != 0) ? 1 : 0;
        return std::span<const uint8_t, SPRITE_BYTES>(data::GHOST_SPRITES[variation][direction][footFrame].data(), SPRITE_BYTES);
    }

    static constexpr void ComposeGhostSprite(int variation, int direction, int footFrame, uint8_t* outSprite56) {
        auto sprite = GetGhostSprite(variation, direction, footFrame);
        std::copy_n(sprite.data(), SPRITE_BYTES, outSprite56);
    }

    static constexpr std::span<const uint8_t, SPRITE_BYTES> GetFruitSprite(int fruitIndex) {
        fruitIndex = std::clamp(fruitIndex, 0, detail::LastIndex(std::size(data::FRUITS)));
        return std::span<const uint8_t, SPRITE_BYTES>(data::FRUITS[fruitIndex].sprite.data(), SPRITE_BYTES);
    }

    static constexpr int GetFruitScore(int fruitIndex) {
        fruitIndex = std::clamp(fruitIndex, 0, detail::LastIndex(std::size(data::FRUITS)));
        return data::FRUITS[fruitIndex].score;
    }

    static constexpr std::span<const uint8_t, SPRITE_BYTES> GetSprite(int spriteIndex) {
        if (spriteIndex < 0 || spriteIndex >= static_cast<int>(std::size(data::PACMAN_SPRITES))) spriteIndex = 0;
        return std::span<const uint8_t, SPRITE_BYTES>(data::PACMAN_SPRITES[spriteIndex].data(), SPRITE_BYTES);
    }

    static constexpr std::span<const uint8_t, LOGO_LETTER_BYTES> GetLogoLetter(int letterIndex) {
        if (letterIndex < 0 || letterIndex >= LOGO_LETTERS) letterIndex = 0;
        return std::span<const uint8_t, LOGO_LETTER_BYTES>(data::LOGO_LETTERS[letterIndex].data(), LOGO_LETTER_BYTES);
    }

    static constexpr LevelConfig GetLevelConfig(int level) {
        constexpr int maxLevel = static_cast<int>(std::size(data::LEVEL_CONFIGS));
        if (level < 1) level = 1;
        if (level > maxLevel) level = maxLevel;
        return data::LEVEL_CONFIGS[level - 1];
    }

    static constexpr uint16_t GetAudioDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::AUDIO_DIVISORS.size())) return 0;
        return data::AUDIO_DIVISORS[index];
    }

    static constexpr size_t GetAudioDivisorCount() {
        return data::AUDIO_DIVISORS.size();
    }

    static constexpr uint16_t GetTitleTuneDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::TITLE_TUNE_DIVISORS.size())) return 2;
        return data::TITLE_TUNE_DIVISORS[index];
    }

    static constexpr uint16_t GetDeathSoundDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::DEATH_SOUND_DIVISORS.size())) return 2;
        return data::DEATH_SOUND_DIVISORS[index];
    }

    static constexpr uint16_t GetGhostEatenSoundDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::GHOST_EATEN_DIVISORS.size())) return 2;
        return data::GHOST_EATEN_DIVISORS[index];
    }

    static constexpr uint16_t GetSirenDivisor(int step) {
        step = (step % 8 + 8) % 8;
        return data::SIREN_DIVISORS[step];
    }

    static constexpr uint16_t GetFruitEatDivisor(int step) {
        step = (step % 6 + 6) % 6;
        return data::FRUIT_EAT_DIVISORS[step];
    }
};

} // namespace pcman
