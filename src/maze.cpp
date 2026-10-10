#include "maze.hpp"
#include "game_data.hpp"
#include "rom_data.hpp"

namespace pcman {

Maze::Maze() {
    Reset();
}

void Maze::Reset() {
    m_cells = data::COLLISION_MAP;
    // Dot + energizer count of the pristine map is computed at compile time
    m_remainingDots = INITIAL_DOT_COUNT;
}

bool Maze::IsWalkable(int cellRow, int cellCol) const {
    if (cellCol == 16) {
        // Authentic vertical wrap tunnel at column 16 (gridX = 32)
        cellRow = (cellRow % COLLISION_ROWS + COLLISION_ROWS) % COLLISION_ROWS;
    }
    if (cellRow < 0 || cellRow >= COLLISION_ROWS || cellCol < 0 || cellCol >= COLLISION_STRIDE) {
        return false;
    }
    return m_cells[cellRow * COLLISION_STRIDE + cellCol] != 0;
}

uint8_t Maze::GetCell(int cellRow, int cellCol) const {
    if (cellCol == 16) {
        cellRow = (cellRow % COLLISION_ROWS + COLLISION_ROWS) % COLLISION_ROWS;
    }
    if (cellRow < 0 || cellRow >= COLLISION_ROWS || cellCol < 0 || cellCol >= COLLISION_STRIDE) {
        return 0;
    }
    return m_cells[cellRow * COLLISION_STRIDE + cellCol];
}

uint8_t Maze::CheckAndEatDotAtGrid(int gridY, int gridX) {
    int r = gridY / 2;
    int c = gridX / 2;
    if (c == 16) {
        r = (r % COLLISION_ROWS + COLLISION_ROWS) % COLLISION_ROWS;
    }
    if (r >= 0 && r < COLLISION_ROWS && c >= 0 && c < COLLISION_STRIDE) {
        size_t idx = static_cast<size_t>(r * COLLISION_STRIDE + c);
        uint8_t val = m_cells[idx];
        if (val == 2 || val == 3) {
            m_cells[idx] = 1;
            m_remainingDots--;
            return val;
        }
    }
    return 0;
}

int Maze::GetRemainingDots() const {
    return m_remainingDots;
}

void Maze::Render(CgaRenderer& renderer) const {
    // 1. Draw maze wall tiles
    for (int r = 0; r < MAZE_ROWS; ++r) {
        for (int c = 0; c < MAZE_COLS; ++c) {
            uint8_t t = data::MAZE_GRID[r * MAZE_COLS + c];
            if (t != 0) {
                renderer.BlitTile4x4(MAZE_SCREEN_X + c * TILE_WIDTH, MAZE_SCREEN_Y + r * TILE_HEIGHT, t);
            }
        }
    }

    // 2. Draw remaining dots
    for (int r = 0; r < DOT_ROWS; ++r) {
        for (int c = 0; c < DOT_COLS; ++c) {
            size_t idx = static_cast<size_t>((r + 1) * COLLISION_STRIDE + (c + 1));
            if (idx < COLLISION_SIZE) {
                uint8_t d = m_cells[idx];
                if (d == 2 || d == 3) {
                    renderer.BlitDot8x8(DOT_OFFSET_X + c * 8, DOT_OFFSET_Y + r * 8, d);
                }
            }
        }
    }
}

} // namespace pcman
