#pragma once
#include "game_data.hpp"
#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <cstring>
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

using LevelConfig = data::LevelConfig;

class RomData {
public:
    static constexpr size_t Size() { return 16448; }

    static std::span<const uint8_t, 4> GetMazeTile(uint8_t tileId) {
        if (tileId >= std::size(data::MAZE_TILES)) tileId = 0;
        return std::span<const uint8_t, 4>(data::MAZE_TILES[tileId].data(), 4);
    }

    static uint8_t GetMazeGridTile(int col, int row) {
        if (col < 0 || col >= MAZE_COLS || row < 0 || row >= MAZE_ROWS) return 0;
        return data::MAZE_GRID[row * MAZE_COLS + col];
    }

    static uint8_t GetDotMapInitial(int col, int row) {
        if (col < 0 || col >= DOT_COLS || row < 0 || row >= DOT_ROWS) return 0;
        constexpr int DOT_STRIDE = 30;
        size_t index = 31 + static_cast<size_t>(row) * DOT_STRIDE + col;
        return (index < data::COLLISION_MAP.size()) ? data::COLLISION_MAP[index] : 0;
    }

    static std::span<const uint8_t, 16> GetDotBitmap(uint8_t dotType) {
        if (dotType >= std::size(data::DOT_BITMAPS)) dotType = 0;
        return std::span<const uint8_t, 16>(data::DOT_BITMAPS[dotType].data(), 16);
    }

    static std::span<const uint8_t, SPRITE_BYTES> GetPacmanSprite(int direction, int mouthFrame) {
        direction = std::clamp(direction, 0, 3);
        mouthFrame = std::clamp(mouthFrame, 0, 3);
        return std::span<const uint8_t, SPRITE_BYTES>(data::PACMAN_SPRITES[direction * 4 + mouthFrame].data(), SPRITE_BYTES);
    }

    static std::span<const uint8_t, SPRITE_BYTES> GetDeathSprite(int frame) {
        frame = std::clamp(frame, 0, 16);
        return std::span<const uint8_t, SPRITE_BYTES>(data::DEATH_SPRITES[frame].data(), SPRITE_BYTES);
    }

    static void ComposeGhostSprite(int variation, int direction, int footFrame, uint8_t* outSprite56) {
        variation = std::clamp(variation, 0, 6);
        direction = std::clamp(direction, 0, 3);
        footFrame = (footFrame != 0) ? 1 : 0;
        std::memcpy(outSprite56, data::GHOST_SPRITES[variation][direction][footFrame].data(), 56);
    }

    static std::span<const uint8_t, SPRITE_BYTES> GetFruitSprite(int fruitIndex) {
        fruitIndex = std::clamp(fruitIndex, 0, 7);
        return std::span<const uint8_t, SPRITE_BYTES>(data::FRUITS[fruitIndex].sprite.data(), SPRITE_BYTES);
    }

    static int GetFruitScore(int fruitIndex) {
        fruitIndex = std::clamp(fruitIndex, 0, 7);
        return data::FRUITS[fruitIndex].score;
    }

    static std::span<const uint8_t, SPRITE_BYTES> GetSprite(int spriteIndex) {
        if (spriteIndex < 0 || spriteIndex >= static_cast<int>(std::size(data::PACMAN_SPRITES))) spriteIndex = 0;
        return std::span<const uint8_t, SPRITE_BYTES>(data::PACMAN_SPRITES[spriteIndex].data(), SPRITE_BYTES);
    }

    static std::span<const uint8_t, LOGO_LETTER_BYTES> GetLogoLetter(int letterIndex) {
        if (letterIndex < 0 || letterIndex >= LOGO_LETTERS) letterIndex = 0;
        return std::span<const uint8_t, LOGO_LETTER_BYTES>(data::LOGO_LETTERS[letterIndex].data(), LOGO_LETTER_BYTES);
    }

    static LevelConfig GetLevelConfig(int level) {
        if (level < 1) level = 1;
        if (level > 12) level = 12;
        return data::LEVEL_CONFIGS[level - 1];
    }

    static uint16_t GetAudioDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::AUDIO_DIVISORS.size())) return 0;
        return data::AUDIO_DIVISORS[index];
    }

    static size_t GetAudioDivisorCount() {
        return data::AUDIO_DIVISORS.size();
    }

    static uint16_t GetTitleTuneDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::TITLE_TUNE_DIVISORS.size())) return 2;
        return data::TITLE_TUNE_DIVISORS[index];
    }

    static uint16_t GetDeathSoundDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::DEATH_SOUND_DIVISORS.size())) return 2;
        return data::DEATH_SOUND_DIVISORS[index];
    }

    static uint16_t GetGhostEatenSoundDivisor(int index) {
        if (index < 0 || index >= static_cast<int>(data::GHOST_EATEN_DIVISORS.size())) return 2;
        return data::GHOST_EATEN_DIVISORS[index];
    }

    static uint16_t GetSirenDivisor(int step) {
        step = (step % 8 + 8) % 8;
        return data::SIREN_DIVISORS[step];
    }

    static uint16_t GetFruitEatDivisor(int step) {
        step = (step % 6 + 6) % 6;
        return data::FRUIT_EAT_DIVISORS[step];
    }
};

} // namespace pcman
