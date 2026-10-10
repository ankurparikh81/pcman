#include "entities.hpp"
#include "rom_data.hpp"
#include <cmath>
#include <algorithm>
#include <array>

namespace pcman {

// =========================================================================
// PAC-MAN IMPLEMENTATION
// =========================================================================

Pacman::Pacman() {
    Reset();
}

void Pacman::Reset() {
    // Authentic starting grid coordinates at DS:0x0725 in ROM (file offset 0x1725)
    // grid_y = 25 (0x19), grid_x = 14 (0x0E), dir = Up (0)
    m_gridY = 25;
    m_gridX = 14;
    m_currentDir = Direction::Up;
    m_requestedDir = Direction::Up;
    m_isMoving = true;
    m_mouthFrame = 3; // Authentic initial sprite matching 0x02F1 (mov si, 0x1e09 / offset 0x2E09) in v1.com
    m_closedMouth = false;
    m_isDying = false;
    m_deathFrame = 0;
    m_deathTick = 0;
}

void Pacman::SetRequestedDirection(Direction dir) {
    if (dir != Direction::None) {
        m_requestedDir = dir;
    }
}

void Pacman::StartDying() {
    m_isDying = true;
    m_deathFrame = 0;
    m_deathTick = 0;
}

void Pacman::UpdateDeath() {
    if (!m_isDying) return;
    m_deathTick++;
    if (m_deathTick >= 2) { // 2 BIOS ticks per melting frame in v1.com (0x041A..0x0470)
        m_deathTick = 0;
        if (m_deathFrame < 16) {
            m_deathFrame++;
        }
    }
}

void Pacman::Update(const Maze& maze) {
    if (m_isDying) return;

    // In Pcman v1.com (0x0576..0x0657):
    // Movement evaluates turns strictly when coordinates are on grid nodes (both even):
    // test byte [0x2A31], 1 == 0 && test byte [0x2A30], 1 == 0
    bool atIntersection = ((m_gridY & 1) == 0) && ((m_gridX & 1) == 0);

    if (atIntersection) {
        int curRow = m_gridY / 2;
        int curCol = m_gridX / 2;

        // 1. If user buffered a turn, check if the perpendicular/new corridor is open:
        if (m_requestedDir != Direction::None && m_requestedDir != m_currentDir) {
            int reqRow = curRow + DeltaRow(m_requestedDir);
            int reqCol = curCol + DeltaCol(m_requestedDir);
            if (maze.IsWalkable(reqRow, reqCol)) {
                m_currentDir = m_requestedDir;
            }
        }

        // 2. Check if Pac-Man can continue forward in current direction:
        int nextRow = curRow + DeltaRow(m_currentDir);
        int nextCol = curCol + DeltaCol(m_currentDir);
        if (maze.IsWalkable(nextRow, nextCol)) {
            m_gridY += DeltaRow(m_currentDir);
            m_gridX += DeltaCol(m_currentDir);
            m_isMoving = true;
        } else {
            // Hit wall dead end: Pac-Man stops centered at the intersection
            m_isMoving = false;
        }
    } else {
        // Between intersections (in the middle of a corridor lane):
        // If the user requested the exact opposite direction (180 turn around),
        // reverse immediately without waiting for the intersection!
        if (m_requestedDir == Opposite(m_currentDir)) {
            m_currentDir = m_requestedDir;
        }

        // Keep moving straight ahead in current direction to reach next node
        m_gridY += DeltaRow(m_currentDir);
        m_gridX += DeltaCol(m_currentDir);
        m_isMoving = true;
    }

    // Authentic vertical wrap-around tunnel (rows 0..57 in grid units, matching 0x0770..0x0795)
    m_gridY = WrapTunnelGridY(m_gridY);

    if (m_isMoving) {
        m_mouthFrame = (m_mouthFrame + 1) & 3;
    }
}

void Pacman::Render(CgaRenderer& renderer) const {
    int px = GridToPixelX(m_gridX);
    int py = GridToPixelY(m_gridY);

    if (m_isDying) {
        auto deathSprite = RomData::GetDeathSprite(m_deathFrame);
        renderer.BlitSpriteRaw(px, py, deathSprite.data(), true);
    } else {
        int dirIdx = static_cast<int>(m_currentDir);
        if (dirIdx < 0 || dirIdx > 3) dirIdx = 0;
        int frame = m_closedMouth ? 2 : m_mouthFrame; // Frame 2 is solid circle at 0x2DD1 in ROM
        auto pacSprite = RomData::GetPacmanSprite(dirIdx, frame);
        renderer.BlitSpriteRaw(px, py, pacSprite.data(), true);
    }
}

// =========================================================================
// GHOST IMPLEMENTATION (Authentic Pcman v1.com AI Algorithm)
// =========================================================================

Ghost::Ghost(GhostType type) : m_type(type) {
    Reset(1);
}

// Authentic 16-bit Linear Congruential PRNG verbatim from routine 0x0C01 at DS:0x2D24 in v1.com:
// seed = (seed * 0x98DD + 0xD5EF) & 0xFFFF
static uint16_t s_globalRngSeed = 0x0000;

uint16_t NextGlobalRandom() {
    s_globalRngSeed = LcgStep(s_globalRngSeed);
    return s_globalRngSeed;
}

void ResetGlobalRandom(uint16_t seed) {
    s_globalRngSeed = seed;
}

void Ghost::Reset(int level) {
    (void)level;
    m_footFrame = 0;
    m_speedSkip = 0;

    const auto& cfg = GHOST_INIT_CONFIGS[static_cast<size_t>(m_type)];
    m_flags = cfg.flags;
    m_penPixelX = cfg.penPixelX;
    m_penPixelY = cfg.penPixelY;
    m_gridY = cfg.gridY;
    m_gridX = cfg.gridX;
    m_dir = cfg.dir;
}

void Ghost::SetFrightened() {
    if (m_flags == 0) {
        m_flags |= 1; // Frightened blue
        m_dir = Opposite(m_dir); // Immediate 180 reverse (0x060A in v1.com)
    }
}

void Ghost::ClearFrightened() {
    m_flags &= 0xFE; // Clear bit 0 (0x0504 in v1.com)
}

void Ghost::Eat() {
    m_flags = 6; // Set to eaten eyes returning to pen (0x0CC4 in v1.com)
}

EntityPosition Ghost::GetPos() const {
    if (m_flags == 4 || m_flags == 2) {
        return { m_penPixelX, m_penPixelY };
    }
    return { GridToPixelX(m_gridX), GridToPixelY(m_gridY) };
}

bool Ghost::CheckCollision(int pacmanGridY, int pacmanGridX) const {
    // In v1.com (0x0897..0x08D8):
    // cmp [bx+0x2a38], 2; jnl no_collision (eyes, pen, exiting cannot collide)
    if (m_flags >= 2) return false;

    // Authentic Manhattan distance check (|dY| + |dX| <= 2)
    return CheckGridCollision(m_gridY, m_gridX, pacmanGridY, pacmanGridX);
}

// Exact translation of the 8086 Ghost AI decision function at 0x095A..0x0AFF in Pcman v1.com
Direction Ghost::ChooseDirection(const Maze& maze, int pacmanGridY, int pacmanGridX, uint8_t ghostWeight) {
    // 1. Initialize weights for all 4 directions to 1:
    // Index 0 = Up, 1 = Down, 2 = Left, 3 = Right
    std::array<int, 4> weights = {1, 1, 1, 1};

    // 2. Target coordinates and pursuit weight
    int targetY = pacmanGridY;
    int targetX = pacmanGridX;
    int weight = ghostWeight;
    bool isFrightened = (m_flags & 1) != 0;

    if (m_flags >= 6) {
        // Eaten eyes returning to pen entrance door at (grid_y = 26, grid_x = 38)
        weight = 0x3C; // Maximum pursuit weight (60)
        targetY = PEN_DOOR_GRID_Y;
        targetX = PEN_DOOR_GRID_X;
        isFrightened = false;
    }

    // Vertical preference:
    int dy = targetY - m_gridY;
    if (isFrightened) dy = -dy; // Reverse target in frightened mode (run away from Pac-Man!)
    if (dy < 0) {
        weights[static_cast<int>(Direction::Up)] = weight;
    } else if (dy > 0) {
        weights[static_cast<int>(Direction::Down)] = weight;
    }

    // Horizontal preference:
    int dx = targetX - m_gridX;
    if (isFrightened) dx = -dx; // Reverse target in frightened mode
    if (dx < 0) {
        weights[static_cast<int>(Direction::Left)] = weight;
    } else if (dx > 0) {
        weights[static_cast<int>(Direction::Right)] = weight;
    }

    // 3. Wall collision check (0x0A94..0x0AAD):
    // Any direction that hits a wall (cell value == 0) receives weight 0!
    int curRow = m_gridY / 2;
    int curCol = m_gridX / 2;

    static constexpr std::array<int, 4> dRow = {-1, 1, 0, 0}; // Up, Down, Left, Right
    static constexpr std::array<int, 4> dCol = {0, 0, -1, 1};

    for (int d = 0; d < 4; ++d) {
        int nr = curRow + dRow[d];
        int nc = curCol + dCol[d];
        if (!maze.IsWalkable(nr, nc)) {
            weights[d] = 0;
        }
    }

    // 4. Inertia & Reversal Prohibition (0x0AAF..0x0AD8):
    int curDirIdx = static_cast<int>(m_dir);
    if (curDirIdx >= 0 && curDirIdx < 4) {
        // Double weight of continuing straight UNLESS speedSkip == 1
        // OR in vertical tunnel (gridX == 32 && (curDir == Up || curDir == Down)):
        bool inVerticalTunnel = (m_gridX == 32) && (m_dir == Direction::Up || m_dir == Direction::Down);
        if (m_speedSkip != 1 && !inVerticalTunnel) {
            weights[curDirIdx] *= 2;
        }

        // Zero weight for turning 180 degrees backward (reversals are strictly forbidden)
        int oppDirIdx = curDirIdx ^ 1;
        weights[oppDirIdx] = 0;
    }

    // 5. Roulette-wheel selection using 16-bit PRNG (0x0AD9..0x0AFE):
    int sum = weights[0] + weights[1] + weights[2] + weights[3];
    if (sum <= 0) {
        // Dead end or all blocked: reverse or find any walkable neighbor
        for (int d = 0; d < 4; ++d) {
            int nr = curRow + dRow[d];
            int nc = curCol + dCol[d];
            if (maze.IsWalkable(nr, nc)) {
                return static_cast<Direction>(d);
            }
        }
        return m_dir;
    }

    uint16_t rnd = NextGlobalRandom();
    int threshold = static_cast<int>((static_cast<uint32_t>(rnd) * static_cast<uint32_t>(sum)) >> 16);

    for (int d = 0; d < 4; ++d) {
        threshold -= weights[d];
        if (threshold < 0) {
            return static_cast<Direction>(d);
        }
    }

    return Direction::Right;
}

void Ghost::Update(const Maze& maze, int pacmanGridY, int pacmanGridX, const LevelConfig& levelCfg) {
    // Authentic speed/skip counter from 0x07B2..0x07BE & 0x0896..0x08C4 in Pcman v1.com:
    // When m_speedSkip >= 2 (tunnel or frightened mode), skip this tick and decrement
    if (m_speedSkip >= 2) {
        m_speedSkip = 1;
        return;
    }

    // Eaten eyes move at 200% speed by taking two steps per tick (0x075E..0x0763)
    int steps = (m_flags >= 6) ? 2 : 1;

    for (int step = 0; step < steps; ++step) {
        if (m_flags == 4) {
            // Inside pen bouncing horizontally (0x09AD..0x09EF)
            m_penPixelX += DeltaCol(m_dir) * 4;

            if (m_penPixelX <= PEN_BOUNCE_MIN_X) {
                m_penPixelX = PEN_BOUNCE_MIN_X;
                m_dir = Direction::Right;

                // Authentic pen release roll when turning from left boundary to Right (0x0AD2 in v1.com)
                if ((m_flags & 1) == 0 && NextGlobalRandom() < levelCfg.penThresh) {
                    m_flags = 2; // Transition to exiting pen
                }
            } else if (m_penPixelX >= PEN_BOUNCE_MAX_X) {
                m_penPixelX = PEN_BOUNCE_MAX_X;
                m_dir = Direction::Left;
            }
            m_footFrame ^= 1;
            break;
        } else if (m_flags == 2) {
            // Exiting pen: move towards door position (pixel_x = 200, pixel_y = 93)
            if (m_penPixelX < PEN_DOOR_PIXEL_X) {
                m_penPixelX = std::min(m_penPixelX + 4, PEN_DOOR_PIXEL_X);
                m_dir = Direction::Right;
            } else if (m_penPixelX > PEN_DOOR_PIXEL_X) {
                m_penPixelX = std::max(m_penPixelX - 4, PEN_DOOR_PIXEL_X);
                m_dir = Direction::Left;
            } else if (m_penPixelY < PEN_DOOR_PIXEL_Y) {
                m_penPixelY = std::min(m_penPixelY + 4, PEN_DOOR_PIXEL_Y);
                m_dir = Direction::Down;
            } else if (m_penPixelY > PEN_DOOR_PIXEL_Y) {
                m_penPixelY = std::max(m_penPixelY - 4, PEN_DOOR_PIXEL_Y);
                m_dir = Direction::Up;
            }

            if (m_penPixelX == PEN_DOOR_PIXEL_X && m_penPixelY == PEN_DOOR_PIXEL_Y) {
                // Ghost passed through door into maze!
                m_flags = 0;
                m_gridY = 25;
                m_gridX = PEN_DOOR_GRID_X;
                m_dir = (NextGlobalRandom() & 1) ? Direction::Up : Direction::Down;
            }
            m_footFrame ^= 1;
            break;
        } else {
            // Active in maze (Normal, Frightened, or Eaten Eyes)
            bool atIntersection = ((m_gridY & 1) == 0) && ((m_gridX & 1) == 0);
            if (atIntersection) {
                Direction newDir = ChooseDirection(maze, pacmanGridY, pacmanGridX, levelCfg.ghostWeight);
                if (newDir != m_dir) {
                    // Authentic corner turn pause matching 0x07FA..0x080A in v1.com:
                    // Ghost pauses for 1 tick facing old direction before turning!
                    if (m_speedSkip != 1) {
                        m_speedSkip = 1;
                        return;
                    }
                    m_dir = newDir;
                }
            }
            m_speedSkip = 0;

            m_gridY += DeltaRow(m_dir);
            m_gridX += DeltaCol(m_dir);
            m_footFrame ^= 1;

            // Authentic vertical tunnel wrapping (rows 0..57 in grid units, matching 0x0870..0x0896)
            m_gridY = WrapTunnelGridY(m_gridY);

            // If eaten eyes reached door entrance (grid_y = 26, grid_x = 38):
            if (m_flags >= 6 && m_gridY == PEN_DOOR_GRID_Y && m_gridX == PEN_DOOR_GRID_X) {
                m_flags = 4; // Back in pen bouncing
                m_penPixelX = 172;
                m_penPixelY = PEN_DOOR_PIXEL_Y;
                m_dir = Direction::Right;
                break;
            }
        }
    }

    // Authentic post-movement speed flags for next tick (0x0896..0x08C4 in v1.com):
    // Evaluated for ALL ghost states (in-pen, exiting-pen, frightened, tunnel)
    if (m_speedSkip != 1) {
        if (m_gridX == 32) {
            m_speedSkip = 2; // 50% speed in tunnel
            if (m_gridY >= 12 && m_gridY <= 38) {
                m_speedSkip = 0; // normal speed in middle corridor
            }
        }
        if (m_flags > 0 && m_flags < 6) {
            m_speedSkip = 2; // 50% speed for in-pen (flags 4), exiting (flags 2), and frightened (flags 1)!
        }
    }
}

void Ghost::Render(CgaRenderer& renderer, bool flashWhite) const {
    int variation = 0;
    if (m_flags >= 6) {
        variation = 6; // Eaten eyes
    } else if ((m_flags & 1) != 0) {
        variation = flashWhite ? 5 : 4; // Frightened blue or flashing white
    } else {
        variation = static_cast<int>(m_type); // 0=Blinky, 1=Pinky, 2=Inky, 3=Clyde
    }

    int px = (m_flags == 4 || m_flags == 2) ? m_penPixelX : GridToPixelX(m_gridX);
    int py = (m_flags == 4 || m_flags == 2) ? m_penPixelY : GridToPixelY(m_gridY);

    int dirIdx = static_cast<int>(m_dir);
    if (dirIdx < 0 || dirIdx > 3) dirIdx = 0;

    auto sprite = RomData::GetGhostSprite(variation, dirIdx, m_footFrame);
    renderer.BlitSpriteRaw(px, py, sprite.data(), true);
}

} // namespace pcman
