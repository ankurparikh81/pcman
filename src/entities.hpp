#pragma once
#include "cga_renderer.hpp"
#include "maze.hpp"
#include "rom_data.hpp"
#include <array>
#include <cstdint>

namespace pcman {

// Direction values matching original Pcman v1.com:
// 0 = Up, 1 = Down, 2 = Left, 3 = Right
enum class Direction : uint8_t {
    Up = 0,
    Down = 1,
    Left = 2,
    Right = 3,
    None = 255
};

constexpr Direction Opposite(Direction d) {
    if (d == Direction::None) return Direction::None;
    return static_cast<Direction>(static_cast<uint8_t>(d) ^ 1);
}

constexpr int DeltaRow(Direction d) {
    switch (d) {
        case Direction::Up:   return -1;
        case Direction::Down: return 1;
        default:              return 0;
    }
}

constexpr int DeltaCol(Direction d) {
    switch (d) {
        case Direction::Left:  return -1;
        case Direction::Right: return 1;
        default:               return 0;
    }
}

// Authentic coordinate conversion from entity grid units to CGA screen pixels
constexpr int GridToPixelX(int gridX) { return 48 + gridX * 4; }
constexpr int GridToPixelY(int gridY) { return -7 + gridY * 4; }

// Manhattan distance between two grid points
constexpr int ManhattanDist(int y1, int x1, int y2, int x2) {
    int dy = y1 - y2;
    int dx = x1 - x2;
    return (dy < 0 ? -dy : dy) + (dx < 0 ? -dx : dx);
}

// Authentic Manhattan distance collision check matching 0x089E..0x08B6 (|dY| + |dX| <= 2)
constexpr bool CheckGridCollision(int y1, int x1, int y2, int x2) {
    return ManhattanDist(y1, x1, y2, x2) <= 2;
}

// Authentic vertical wrap-around tunnel (rows 0..57 in grid units, matching 0x0770..0x0795 & 0x0870..0x0896)
inline constexpr int TUNNEL_GRID_ROWS = 58;

constexpr int WrapTunnelGridY(int gridY) {
    if (gridY < 0) return gridY + TUNNEL_GRID_ROWS;
    if (gridY >= TUNNEL_GRID_ROWS) return gridY - TUNNEL_GRID_ROWS;
    return gridY;
}

// Authentic Ghost pen coordinates & boundaries from ROM template 0x0725 & AI routine 0x09AD..0x09EF
inline constexpr int PEN_DOOR_GRID_Y  = 26;
inline constexpr int PEN_DOOR_GRID_X  = 38;
inline constexpr int PEN_DOOR_PIXEL_X = 200;
inline constexpr int PEN_DOOR_PIXEL_Y = 93;
inline constexpr int PEN_BOUNCE_MIN_X = 168;
inline constexpr int PEN_BOUNCE_MAX_X = 184;

enum class GhostType {
    Blinky = 0, // Red / Chaser
    Pinky = 1,  // Pink / Ambusher
    Inky = 2,   // Cyan / Outside pen at start
    Clyde = 3   // Orange / Feigning
};

// Authentic Ghost state flags matching byte at [bx+0x2a38] in Pcman v1.com:
// Bit 0 is the frightened bit (1).
// Base state values: 0 (Active), 2 (ExitingPen), 4 (InPen), 6 (EatenEyes).
namespace GhostFlags {
    inline constexpr uint8_t Active     = 0; // Normal active in maze
    inline constexpr uint8_t Frightened = 1; // Bit 0: Frightened blue / flashing
    inline constexpr uint8_t ExitingPen = 2; // Moving through pen towards door
    inline constexpr uint8_t InPen      = 4; // Bouncing inside pen
    inline constexpr uint8_t EatenEyes  = 6; // Eaten eyes returning to pen entrance
}

struct GhostInitConfig {
    uint8_t flags = GhostFlags::Active;
    int penPixelX = 0;
    int penPixelY = 0;
    int gridY = 0;
    int gridX = 0;
    Direction dir = Direction::Up;
};

// Initial entity states from ROM template table at DS:0x0725
inline constexpr std::array<GhostInitConfig, 4> GHOST_INIT_CONFIGS = {{
    { GhostFlags::InPen,  180, 105, 26, 38, Direction::Left  }, // Ghost 0 (Blinky): inside pen
    { GhostFlags::InPen,  172,  93, 26, 38, Direction::Right }, // Ghost 1 (Pinky): inside pen
    { GhostFlags::Active, 200,  93, 25, 38, Direction::Up    }, // Ghost 2 (Inky): outside pen
    { GhostFlags::InPen,  180,  81, 26, 38, Direction::Left  }  // Ghost 3 (Clyde): inside pen
}};

struct EntityPosition {
    int x = 0;
    int y = 0;
};

class Pacman {
public:
    Pacman();

    void Reset();
    void SetRequestedDirection(Direction dir);
    void Update(const Maze& maze);
    void Render(CgaRenderer& renderer) const;

    EntityPosition GetPos() const {
        return { GridToPixelX(m_gridX), GridToPixelY(m_gridY) };
    }
    int GetGridY() const { return m_gridY; }
    int GetGridX() const { return m_gridX; }
    Direction GetDirection() const { return m_currentDir; }
    void SetGridPos(int gridY, int gridX, Direction dir = Direction::Up) {
        m_gridY = gridY;
        m_gridX = gridX;
        m_currentDir = dir;
        m_requestedDir = dir;
    }

    int GetMouthFrame() const { return m_mouthFrame; }
    void SetClosedMouth(bool closed) { m_closedMouth = closed; }
    bool IsClosedMouth() const { return m_closedMouth; }

    bool IsDying() const { return m_isDying; }
    void StartDying();
    bool IsDeathComplete() const { return m_deathFrame >= 16; }
    int GetDeathFrame() const { return m_deathFrame; }
    void UpdateDeath();

private:
    int m_gridY = 25; // Authentic starting grid coordinates (0x1725 in ROM)
    int m_gridX = 14;
    Direction m_currentDir = Direction::Up;
    Direction m_requestedDir = Direction::Up;
    bool m_isMoving = true;

    // Authentic initial mouth frame: 3 (half-open sprite at 0x1E09 / file offset 0x2E09 at 0x02F1 in v1.com)
    int m_mouthFrame = 3;
    bool m_closedMouth = false;

    bool m_isDying = false;
    int m_deathFrame = 0;
    int m_deathTick = 0;
};

class Ghost {
public:
    Ghost(GhostType type);

    void Reset(int level);
    void Update(const Maze& maze, int pacmanGridY, int pacmanGridX, const LevelConfig& levelCfg);
    void Render(CgaRenderer& renderer, bool flashWhite) const;

    void SetFrightened();
    void ClearFrightened();
    bool IsFrightened() const { return (m_flags & GhostFlags::Frightened) != 0; }
    bool IsEaten() const { return m_flags >= GhostFlags::EatenEyes; }
    bool IsInPen() const { return m_flags == GhostFlags::InPen; }
    bool IsExitingPen() const { return m_flags == GhostFlags::ExitingPen; }
    void Eat();

    EntityPosition GetPos() const;
    int GetGridY() const { return m_gridY; }
    int GetGridX() const { return m_gridX; }
    GhostType GetType() const { return m_type; }
    void SetGridPos(int gridY, int gridX, Direction dir = Direction::Up) {
        m_gridY = gridY;
        m_gridX = gridX;
        m_dir = dir;
        m_flags = GhostFlags::Active;
    }

    // Manhattan distance collision check matching 0x089E..0x08B6 (|dY| + |dX| <= 2)
    bool CheckCollision(int pacmanGridY, int pacmanGridX) const;

private:
    GhostType m_type;

    // Authentic flags matching 0x2A38 (GhostFlags::Active, Frightened, ExitingPen, InPen, EatenEyes)
    uint8_t m_flags = GhostFlags::Active;

    int m_gridY = 26;
    int m_gridX = 38;

    // Pixel coordinates for in-pen / exiting-pen motion
    int m_penPixelX = 180;
    int m_penPixelY = 105;

    Direction m_dir = Direction::Up;
    int m_footFrame = 0;
    // Authentic speed/skip counter matching 0x2A37 in Pcman v1.com:
    // 0 = normal, 1 = corner pause / intermediate, 2 = tunnel / frightened 50% speed
    uint8_t m_speedSkip = 0;

    Direction ChooseDirection(const Maze& maze, int pacmanGridY, int pacmanGridX, uint8_t ghostWeight);
};

// Authentic 16-bit LCG step verbatim from routine 0x0C01 in Pcman v1.com:
// seed = (seed * 0x98DD + 0xD5EF) & 0xFFFF
// Computed in uint32_t: uint16_t would promote to int, and seed * 0x98DD overflows int for large seeds.
constexpr uint16_t LcgStep(uint16_t seed) {
    return static_cast<uint16_t>(static_cast<uint32_t>(seed) * 0x98DDu + 0xD5EFu);
}

// Authentic shared global 16-bit PRNG at DS:0x2D24 in Pcman v1.com (0x0C01..0x0C0C)
uint16_t NextGlobalRandom();
void ResetGlobalRandom(uint16_t seed = 0);

} // namespace pcman
