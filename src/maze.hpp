#pragma once
#include "cga_renderer.hpp"
#include <array>
#include <cstdint>

namespace pcman {

class Maze {
public:
    Maze();

    void Reset();

    // Authentic 30-stride collision map queries matching 0x1673 in v1.com
    static constexpr int COLLISION_STRIDE = 30;
    static constexpr int COLLISION_ROWS = 29;
    static constexpr size_t COLLISION_SIZE = data::COLLISION_MAP.size();

    bool IsWalkable(int cellRow, int cellCol) const;
    uint8_t GetCell(int cellRow, int cellCol) const;
    uint8_t CheckAndEatDotAtGrid(int gridY, int gridX);

    int GetRemainingDots() const;
    static constexpr int GetTotalDots() { return TOTAL_PELLETS; }

    // Render the maze background tiles & remaining dots
    void Render(CgaRenderer& renderer) const;

private:
    std::array<uint8_t, COLLISION_SIZE> m_cells{};
    int m_remainingDots = 0;

    static constexpr int MAZE_SCREEN_X = 52;
    static constexpr int MAZE_SCREEN_Y = 0;
    static constexpr int DOT_OFFSET_X = 60;
    static constexpr int DOT_OFFSET_Y = 4;
};

} // namespace pcman
