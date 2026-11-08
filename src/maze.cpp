#include "maze.hpp"
#include "serialization.hpp"
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
    return GetCell(cellRow, cellCol) != 0;
}

uint8_t Maze::GetCell(int cellRow, int cellCol) const {
    cellRow = NormalizeCellRow(cellRow, cellCol);
    if (cellRow < 0 || cellRow >= COLLISION_ROWS || cellCol < 0 || cellCol >= COLLISION_STRIDE) {
        return 0;
    }
    return m_cells[cellRow * COLLISION_STRIDE + cellCol];
}

uint8_t Maze::GetDotAt(int dotRow, int dotCol) const {
    if (dotRow < 0 || dotRow >= DOT_ROWS || dotCol < 0 || dotCol >= DOT_COLS) {
        return 0;
    }
    size_t idx = static_cast<size_t>((dotRow + 1) * COLLISION_STRIDE + (dotCol + 1));
    return (idx < COLLISION_SIZE) ? m_cells[idx] : 0;
}

uint8_t Maze::CheckAndEatDotAtGrid(int gridY, int gridX) {
    int r = NormalizeCellRow(gridY / 2, gridX / 2);
    int c = gridX / 2;
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

MazeSnapshot Maze::SaveState() const {
    return MazeSnapshot{
        .cells = m_cells,
        .remainingDots = m_remainingDots
    };
}

bool Maze::LoadState(const MazeSnapshot& snapshot) {
    int countedDots = 0;
    for (uint8_t c : snapshot.cells) {
        if (c > 3) return false;
        if (c == 2 || c == 3) countedDots++;
    }
    if (snapshot.remainingDots != countedDots) return false;

    m_cells = snapshot.cells;
    m_remainingDots = snapshot.remainingDots;
    return true;
}

} // namespace pcman
