#pragma once
#include "rom_data.hpp"
#include <array>
#include <cstdint>

namespace pcman {

struct MazeSnapshot;

class Maze {
public:
    Maze();

    void Reset();

    // Authentic 30-stride collision map queries matching 0x1673 in v1.com
    static constexpr int COLLISION_STRIDE = 30;
    static constexpr int COLLISION_ROWS = 29;
    static constexpr int TUNNEL_CELL_COL = 16;
    static constexpr size_t COLLISION_SIZE = data::COLLISION_MAP.size();

    // Screen layout offsets for maze tiles and dots
    static constexpr int MAZE_SCREEN_X = 52;
    static constexpr int MAZE_SCREEN_Y = 0;
    static constexpr int DOT_OFFSET_X = 60;
    static constexpr int DOT_OFFSET_Y = 4;

    static constexpr int NormalizeCellRow(int cellRow, int cellCol) {
        if (cellCol == TUNNEL_CELL_COL) {
            // Authentic vertical wrap tunnel at column 16 (gridX = 32)
            return (cellRow % COLLISION_ROWS + COLLISION_ROWS) % COLLISION_ROWS;
        }
        return cellRow;
    }

    bool IsWalkable(int cellRow, int cellCol) const;
    uint8_t GetCell(int cellRow, int cellCol) const;
    uint8_t GetDotAt(int dotRow, int dotCol) const;
    uint8_t CheckAndEatDotAtGrid(int gridY, int gridX);

    int GetRemainingDots() const;
    void ClearAllDots() { m_remainingDots = 0; }
    static constexpr int GetTotalDots() { return TOTAL_PELLETS; }

    MazeSnapshot SaveState() const;
    bool LoadState(const MazeSnapshot& snapshot);

private:
    std::array<uint8_t, COLLISION_SIZE> m_cells{};
    int m_remainingDots = 0;
};

} // namespace pcman
