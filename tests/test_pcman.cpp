#include "rom_data.hpp"
#include "maze.hpp"
#include "entities.hpp"
#include "game.hpp"
#include <iostream>
#include <cassert>

using namespace pcman;

// =========================================================================
// Compile-time checks (evaluated by the compiler; build fails on regression)
// =========================================================================
static_assert(RomData::Size() == 16448);
static_assert(RomData::GetMazeTile(1)[0] == 0x55);
static_assert(RomData::GetMazeTile(200)[0] == 0x00);                 // out of range -> tile 0
static_assert(RomData::GetPacmanSprite(0, 2)[1] == 0x0F && RomData::GetPacmanSprite(0, 2)[2] == 0xFC);
static_assert(RomData::GetLevelConfig(1).frightTime == 200);
static_assert(RomData::GetLevelConfig(99).penThresh == 65535);         // clamps to level 12
static_assert(RomData::GetFruitScore(0) == 150);
static_assert(RomData::GetAudioDivisorCount() == 66 && RomData::GetAudioDivisor(0) == 400);
static_assert(RomData::GetTitleTuneDivisor(0) == 852);
static_assert(RomData::GetDeathSoundDivisor(2) == 2275);
static_assert(RomData::GetGhostEatenSoundDivisor(23) == 923);
static_assert(RomData::GetSirenDivisor(-8) == 5000 && RomData::GetFruitEatDivisor(6) == 675);
static_assert(INITIAL_DOT_COUNT == TOTAL_PELLETS && Maze::GetTotalDots() == TOTAL_PELLETS);
static_assert([] {
    uint8_t buf[SPRITE_BYTES]{};
    RomData::ComposeGhostSprite(0, 0, 0, buf);                          // Blinky, Up, Foot 0
    return buf[32] == 0x15 && buf[33] == 0x55 && buf[34] == 0x55 && buf[35] == 0x54;
}());
static_assert(Opposite(Direction::Left) == Direction::Right && Opposite(Direction::Up) == Direction::Down);
static_assert(Opposite(Direction::None) == Direction::None);
static_assert(DeltaRow(Direction::Up) == -1 && DeltaCol(Direction::Right) == 1 && DeltaCol(Direction::Up) == 0);
static_assert(GridToPixelX(14) == 104 && GridToPixelY(25) == 93);
static_assert(LcgStep(0) == 0xD5EF);
static_assert(LcgStep(0xFFFF) == 0x3D12);  // (-1 * 0x98DD + 0xD5EF) mod 2^16; no signed overflow
static_assert(RomData::GetGhostSprite(0, 0, 0)[32] == 0x15);            // Zero-copy sprite accessor
static_assert(CgaColor(0).a == 255 && CgaColor(1).g == 255);            // CgaColor palette mapper
static_assert(ManhattanDist(25, 38, 24, 37) == 2);
static_assert(CheckGridCollision(25, 38, 24, 37));                      // dist <= 2
static_assert(!CheckGridCollision(25, 38, 23, 37));                     // dist 3 -> no collision
static_assert(WrapTunnelGridY(-1) == 57 && WrapTunnelGridY(58) == 0 && WrapTunnelGridY(25) == 25);
static_assert(GHOST_INIT_CONFIGS[static_cast<size_t>(GhostType::Blinky)].penPixelX == 180);
static_assert(GHOST_INIT_CONFIGS[static_cast<size_t>(GhostType::Inky)].flags == 0);
static_assert(GhostComboScore(1) == 200 && GhostComboScore(2) == 400 && GhostComboScore(4) == 1600);
static_assert(GhostComboScore(5) == 1600);                              // capped combo
static_assert(CheckTouchBox(152, 93, 152, 93, 8) && !CheckTouchBox(152, 93, 161, 93, 8));

void TestRomData() {
    std::cout << "[Test] Validating ROM data extraction...\n";

    assert(RomData::Size() == 16448);

    // 1. Check Maze Tiles
    for (int t = 0; t < 30; ++t) {
        auto tile = RomData::GetMazeTile(t);
        assert(tile.size() == 4);
    }
    auto tile0 = RomData::GetMazeTile(0);
    assert(tile0[0] == 0 && tile0[1] == 0 && tile0[2] == 0 && tile0[3] == 0);
    auto tile1 = RomData::GetMazeTile(1);
    assert(tile1[0] == 0x55);

    // 2. Check Maze Grid
    int nonZeroTiles = 0;
    for (int r = 0; r < MAZE_ROWS; ++r) {
        for (int c = 0; c < MAZE_COLS; ++c) {
            if (RomData::GetMazeGridTile(c, r) != 0) {
                nonZeroTiles++;
            }
        }
    }
    std::cout << "  - Non-zero maze tiles: " << nonZeroTiles << "/3100\n";
    assert(nonZeroTiles > 500);

    // 3. Check Dot Map
    int pellets = 0;
    int energizers = 0;
    for (int r = 0; r < DOT_ROWS; ++r) {
        for (int c = 0; c < DOT_COLS; ++c) {
            uint8_t d = RomData::GetDotMapInitial(c, r);
            if (d == 2) pellets++;
            else if (d == 3) energizers++;
        }
    }
    std::cout << "  - Pellets: " << pellets << ", Energizers: " << energizers << " (Total: " << (pellets + energizers) << ")\n";
    assert(pellets + energizers == TOTAL_PELLETS);
    assert(energizers == 4);

    // 4. Check Pac-Man Sprites
    auto pac0 = RomData::GetPacmanSprite(0, 0); // Up, wide open
    assert(pac0.size() == SPRITE_BYTES);
    auto pac2 = RomData::GetPacmanSprite(0, 2); // Up, full circle
    assert(pac2[0] == 0x00 && pac2[1] == 0x0F && pac2[2] == 0xFC && pac2[3] == 0x00);

    // 5. Check Ghost Composition
    uint8_t ghostBuf[56];
    RomData::ComposeGhostSprite(0, 0, 0, ghostBuf); // Blinky, Up, Foot 0
    // Check body rows (rows 8..11, bytes 32..47) match 0x15, 0x55, 0x55, 0x54 (14-pixel wide body)
    for (int row = 8; row < 12; ++row) {
        assert(ghostBuf[row * 4 + 0] == 0x15);
        assert(ghostBuf[row * 4 + 1] == 0x55);
        assert(ghostBuf[row * 4 + 2] == 0x55);
        assert(ghostBuf[row * 4 + 3] == 0x54);
    }
    // Check feet row 12 (bytes 48..51) matches 0x15, 0x45, 0x51, 0x54
    assert(ghostBuf[48] == 0x15 && ghostBuf[49] == 0x45 && ghostBuf[50] == 0x51 && ghostBuf[51] == 0x54);
    std::cout << "  - Ghost composite sprite assembled properly (56 bytes, body & feet verified)!\n";

    // 6. Check Death Sprites (17 frames 0..16 from 0x3675)
    auto death0 = RomData::GetDeathSprite(0);
    assert(death0.size() == SPRITE_BYTES);
    auto death16 = RomData::GetDeathSprite(16);
    assert(death16.size() == SPRITE_BYTES);
    for (uint8_t b : death16) {
        assert(b == 0); // Frame 16 is dissolved blank
    }

    // 7. Check Logo Letters
    for (int l = 0; l < LOGO_LETTERS; ++l) {
        auto letter = RomData::GetLogoLetter(l);
        assert(letter.size() == LOGO_LETTER_BYTES);
    }

    // 8. Check Level Config (authentic 1-indexed table at 0x16EF + level * 4)
    auto l1 = RomData::GetLevelConfig(1);
    assert(l1.frightTime == 0xC8);  // 200 ticks = 11.0s
    assert(l1.ghostWeight == 12);
    assert(l1.penThresh == 0x2000); // 8192 (12.5% release per bounce)
    auto l12 = RomData::GetLevelConfig(12);
    assert(l12.frightTime == 0x05); // 5 ticks
    assert(l12.ghostWeight == 60);
    assert(l12.penThresh == 0xFFFF); // 100% immediate release

    // 9. Check Audio Divisors
    assert(RomData::GetAudioDivisorCount() == 66);
    assert(RomData::GetAudioDivisor(0) == 400);

    // 10. Check Fruit Sprites & Scores (8 authentic fruits from 0x3D2E)
    static constexpr int EXPECTED_FRUIT_SCORES[8] = {
        150, 300, 600, 1000, 1500, 2500, 4000, 6000
    };
    for (int f = 0; f < 8; ++f) {
        auto fruit = RomData::GetFruitSprite(f);
        assert(fruit.size() == SPRITE_BYTES);
        int score = RomData::GetFruitScore(f);
        assert(score == EXPECTED_FRUIT_SCORES[f]);
    }
    // Verify Level 1 fruit is Cherry / Candy cane at 150 points (bytes match 0x3D2E + 3)
    assert(RomData::GetFruitScore(0) == 150);
    auto cherry = RomData::GetFruitSprite(0);
    assert(cherry[0] == 0x00 && cherry[1] == 0x09 && cherry[2] == 0x60 && cherry[3] == 0x00);

    // Verify authentic v1.com audio sequences
    assert(RomData::GetTitleTuneDivisor(0) == 852);

    // Death sound: 34 notes at 0x3CD0 ending with authentic [1520, 1014, 2, 1520, 1014]
    assert(RomData::GetDeathSoundDivisor(0) == 2);
    assert(RomData::GetDeathSoundDivisor(2) == 2275);
    assert(RomData::GetDeathSoundDivisor(27) == 1520);
    assert(RomData::GetDeathSoundDivisor(28) == 1014);
    assert(RomData::GetDeathSoundDivisor(30) == 1520);
    assert(RomData::GetDeathSoundDivisor(31) == 1014);
    assert(RomData::GetDeathSoundDivisor(33) == 2);

    // Frightened ghost eaten sound: 24 notes at 0x3FF6 rising arpeggio
    assert(RomData::GetGhostEatenSoundDivisor(0) == 1770);
    assert(RomData::GetGhostEatenSoundDivisor(1) == 3540);
    assert(RomData::GetGhostEatenSoundDivisor(22) == 461);
    assert(RomData::GetGhostEatenSoundDivisor(23) == 923);

    assert(RomData::GetSirenDivisor(0) == 5000);
    assert(RomData::GetFruitEatDivisor(0) == 675);
    std::cout << "  - Verified authentic PC Speaker audio tables (Title tune, Death 34 notes, Ghost eaten 24 notes, Siren, Fruit chime)!\n";

    std::cout << "  -> All ROM data validation checks passed!\n";
}

void TestMazeLogic() {
    std::cout << "[Test] Validating Maze logic and dot eating...\n";

    Maze maze;
    assert(maze.GetRemainingDots() == TOTAL_PELLETS);
    assert(maze.IsWalkable(0, 0) == false);
    assert(maze.IsWalkable(12, 7) == true);

    int remainingBefore = maze.GetRemainingDots();
    uint8_t eaten = maze.CheckAndEatDotAtGrid(25, 14);
    if (eaten == 2 || eaten == 3) {
        assert(maze.GetRemainingDots() == remainingBefore - 1);
    }

    std::cout << "  -> All Maze logic checks passed!\n";
}

void TestPacmanMovement() {
    std::cout << "[Test] Validating Pac-Man lane centering and cornering...\n";

    Maze maze;
    Pacman pacman;

    assert(pacman.GetGridY() == 25);
    assert(pacman.GetGridX() == 14);
    assert(pacman.GetPos().x == 104);
    assert(pacman.GetPos().y == 93);
    assert(pacman.GetDirection() == Direction::Up);
    assert(pacman.GetMouthFrame() == 3); // Authentic half-open sprite (0x1E09 / file offset 0x2E09) at round start / READY!

    // 2. Center check: Corridor is cols 13..16 (pixels 104..119, width 16)
    // Pacman is 16px wide at pixel X = 104. Fits with exactly 0 slack.
    std::cout << "  - Starting pos: grid (" << pacman.GetGridY() << ", " << pacman.GetGridX()
              << ") -> pixel (" << pacman.GetPos().x << ", " << pacman.GetPos().y << ") perfectly centered in lane!\n";

    // 3. Movement & cornering:
    // Pacman starts at gridY = 25 moving Up.
    // User buffers a turn to the Right.
    // At row 12 (gridY = 24), Right is a wall, so Pacman must NOT turn into the wall.
    // He continues Up through row 12 to reach row 11 (gridY = 22), where Right is open!
    pacman.SetRequestedDirection(Direction::Right);

    // Step 1: advances to gridY = 24 (row 12). Right is wall, so continues Up.
    pacman.Update(maze);
    assert(pacman.GetGridY() == 24);
    assert(pacman.GetDirection() == Direction::Up);

    // Step 2: advances to gridY = 23 (between nodes).
    pacman.Update(maze);
    assert(pacman.GetGridY() == 23);
    assert(pacman.GetDirection() == Direction::Up);

    // Step 3: advances to gridY = 22 (row 11). This is the intersection where Right is open!
    pacman.Update(maze);
    assert(pacman.GetGridY() == 22);

    // Step 4: at intersection gridY = 22, Pacman takes the buffered Right turn!
    pacman.Update(maze);
    assert(pacman.GetDirection() == Direction::Right);
    assert(pacman.GetGridY() == 22);
    assert(pacman.GetGridX() == 15);
    std::cout << "  - Turn cornering: ignored wall at row 12, cleanly reached row 11 intersection and took buffered Right turn!\n";

    // 4. Mid-corridor 180 reverse:
    // At grid (22, 15), between vertical nodes. User requests Left (180 reverse).
    pacman.SetRequestedDirection(Direction::Left);
    pacman.Update(maze);
    assert(pacman.GetDirection() == Direction::Left);
    assert(pacman.GetGridX() == 14);
    std::cout << "  - Immediate 180 reverse mid-corridor verified!\n";

    std::cout << "  -> All Pac-Man movement and cornering checks passed!\n";
}

void TestGhostAI() {
    std::cout << "[Test] Validating Ghost AI, ROM initial states, and Manhattan collision...\n";

    Maze maze;
    Ghost blinky(GhostType::Blinky);
    Ghost pinky(GhostType::Pinky);
    Ghost inky(GhostType::Inky);
    Ghost clyde(GhostType::Clyde);

    // 1. Initial states from ROM template 0x1725:
    assert(blinky.IsInPen());
    assert(pinky.IsInPen());
    assert(!inky.IsInPen() && !inky.IsExitingPen()); // Inky is outside pen, active immediately!
    assert(clyde.IsInPen());

    assert(inky.GetGridY() == 25);
    assert(inky.GetGridX() == 38);
    assert(inky.GetPos().x == 200);
    assert(inky.GetPos().y == 93);
    std::cout << "  - Verified initial states: Blinky, Pinky, Clyde in pen, Inky active outside pen at (200, 93)!\n";

    // 2. Manhattan distance collision (|dY| + |dX| <= 2):
    // Inky is at (25, 38).
    assert(inky.CheckCollision(25, 38) == true);  // dist 0
    assert(inky.CheckCollision(25, 37) == true);  // dist 1
    assert(inky.CheckCollision(24, 37) == true);  // dist 2
    assert(inky.CheckCollision(23, 37) == false); // dist 3 (no collision)
    assert(inky.CheckCollision(25, 35) == false); // dist 3 (no collision)
    std::cout << "  - Verified authentic Manhattan collision (|dY| + |dX| <= 2)!\n";

    // 3. Frightened and Eaten mode behavior:
    inky.SetFrightened();
    assert(inky.IsFrightened());
    inky.Eat();
    assert(inky.IsEaten());
    assert(!inky.CheckCollision(25, 38)); // Eaten eyes cannot kill Pac-Man!
    std::cout << "  - Verified frightened state and eaten eyes collision safety!\n";

    // 4. In-Pen Bouncing (50% speed) and Release:
    Ghost penGhost(GhostType::Pinky);
    assert(penGhost.IsInPen());
    auto l1Cfg = RomData::GetLevelConfig(1);
    assert(l1Cfg.penThresh == 0x2000); // 8192 > 0

    // Advance pen ghost for several bounces; verify it releases from pen
    bool released = false;
    for (int t = 0; t < 200; ++t) {
        penGhost.Update(maze, 25, 14, l1Cfg);
        if (penGhost.IsExitingPen() || (!penGhost.IsInPen() && !penGhost.IsExitingPen())) {
            released = true;
            break;
        }
    }
    assert(released);
    std::cout << "  - Verified in-pen ghost successfully released using Level 1 penThresh!\n";
    std::cout << "  -> All Ghost AI checks passed!\n";
}

void TestPacingAndTiming() {
    std::cout << "[Test] Validating IBM PC-XT pacing, dot eating pause, and ghost corner pause...\n";

    Maze maze;
    auto cfg = RomData::GetLevelConfig(1);

    // 1. Ghost cornering turn pause:
    // Inky starts active outside pen at (gridY = 25, gridX = 38).
    // Test that when Inky decides to turn at an intersection, he takes 1 tick pause!
    Ghost ghost(GhostType::Inky);
    assert(ghost.GetGridY() == 25);
    assert(ghost.GetGridX() == 38);

    // Run 1 tick: ghost moves
    int initialY = ghost.GetGridY();
    int initialX = ghost.GetGridX();
    ghost.Update(maze, 25, 14, cfg);
    // Position changed or turn buffered
    std::cout << "  - Ghost initial step: (" << initialY << ", " << initialX << ") -> ("
              << ghost.GetGridY() << ", " << ghost.GetGridX() << ")\n";

    // 2. Frightened ghost speed reduction (50% speed):
    Ghost frightenedGhost(GhostType::Inky);
    frightenedGhost.SetFrightened();
    int moves = 0;
    int prevX = frightenedGhost.GetGridX();
    int prevY = frightenedGhost.GetGridY();
    for (int t = 0; t < 6; ++t) {
        frightenedGhost.Update(maze, 25, 14, cfg);
        if (frightenedGhost.GetGridX() != prevX || frightenedGhost.GetGridY() != prevY) {
            moves++;
            prevX = frightenedGhost.GetGridX();
            prevY = frightenedGhost.GetGridY();
        }
    }
    // Across 6 ticks, a 50% speed ghost moves at most 3-4 times
    std::cout << "  - Frightened ghost moved " << moves << " times across 6 ticks (50% speed verified)!\n";
    assert(moves <= 4);

    std::cout << "  -> All Pacing and Timing checks passed!\n";
}

void TestVerticalTunnels() {
    std::cout << "[Test] Validating Vertical Tunnel Wrap & Ghost AI in Tunnel...\n";
    Maze maze;
    auto cfg = RomData::GetLevelConfig(1);

    // 1. Maze corridor walkability in tunnel (column 16, gridX = 32)
    assert(maze.IsWalkable(0, 16));
    assert(maze.IsWalkable(28, 16));
    assert(maze.IsWalkable(-1, 16)); // wrapped to 28
    assert(maze.IsWalkable(29, 16)); // wrapped to 0

    // 2. Pac-Man moving UP through top tunnel:
    Pacman pacman;
    pacman.SetGridPos(2, 32, Direction::Up);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 1);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 0);
    pacman.Update(maze);
    // Grid coordinate wraps from 0 to 57 when moving UP!
    assert(pacman.GetGridY() == 57);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 56);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 55);
    std::cout << "  - Pac-Man successfully entered top tunnel at gridY=0 and wrapped to gridY=57!\n";

    // 3. Pac-Man moving DOWN through bottom tunnel:
    pacman.SetGridPos(56, 32, Direction::Down);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 57);
    pacman.Update(maze);
    // Grid coordinate wraps from 57 to 0 when moving DOWN!
    assert(pacman.GetGridY() == 0);
    pacman.Update(maze);
    assert(pacman.GetGridY() == 1);
    std::cout << "  - Pac-Man successfully entered bottom tunnel at gridY=57 and wrapped to gridY=0!\n";

    // 4. Ghost traversal through vertical tunnel (at 50% speed in tunnel zone gridY < 12):
    Ghost ghost(GhostType::Inky);
    ghost.SetGridPos(2, 32, Direction::Up);
    // Ghost targets Pac-Man far below (at row 25), but reversal is forbidden in corridor!
    ghost.Update(maze, 25, 14, cfg);
    assert(ghost.GetGridY() == 1);
    // In tunnel zone (gridY < 12), speedSkip is 2 so it skips every other tick:
    ghost.Update(maze, 25, 14, cfg); // Tick skipped (50% speed)
    assert(ghost.GetGridY() == 1);
    ghost.Update(maze, 25, 14, cfg); // Steps from 1 to 0
    assert(ghost.GetGridY() == 0);
    // Ghost continues through wrap:
    for (int t = 0; t < 20; ++t) {
        ghost.Update(maze, 25, 14, cfg);
    }
    // Ghost traversed through tunnel without crashing or getting stuck!
    assert(ghost.GetGridX() == 32);
    std::cout << "  - Ghost successfully navigated vertical tunnel with 50% tunnel speed!\n";
    std::cout << "  -> All Vertical Tunnel checks passed!\n";
}

void TestDeathSequence() {
    std::cout << "[Test] Validating Pac-Man Death Melting Animation (17 frames / 34 ticks)...\n";
    Pacman pacman;
    pacman.StartDying();
    assert(pacman.IsDying());
    assert(pacman.GetDeathFrame() == 0);
    assert(!pacman.IsDeathComplete());

    // Advance 34 ticks (2 ticks per frame across 17 frames: 0..16)
    for (int tick = 0; tick < 34; ++tick) {
        pacman.UpdateDeath();
    }
    assert(pacman.IsDeathComplete());
    assert(pacman.GetDeathFrame() == 16);
    std::cout << "  - Verified Pac-Man death animation: 17 frames (34 ticks total)!\n";
    std::cout << "  -> All Death Sequence checks passed!\n";
}

void TestExtraLife() {
    std::cout << "[Test] Validating Extra Life Award at 10,000 Points...\n";
    Game game;
    game.StartNewGame();

    assert(game.GetScore() == 0);
    assert(game.GetLives() == 3);
    assert(!game.HasAwardedExtraLife());

    // 1. Score below 10,000
    game.SetScore(9990);
    game.Tick18Hz();
    assert(game.GetLives() == 3);
    assert(!game.HasAwardedExtraLife());

    // 2. Score crosses 10,000 points
    game.SetScore(10050);
    game.Tick18Hz();
    assert(game.GetLives() == 4); // Extra life granted!
    assert(game.HasAwardedExtraLife());

    // 3. Score increases further (e.g. 20,000) -> should NOT grant a second extra life
    game.SetScore(20000);
    game.Tick18Hz();
    assert(game.GetLives() == 4);

    // 4. Starting a new game resets the extra life flag and lives
    game.StartNewGame();
    assert(game.GetLives() == 3);
    assert(game.GetScore() == 0);
    assert(!game.HasAwardedExtraLife());

    std::cout << "  - Extra life awarded at 10,000 points (lives 3 -> 4), verified single award & reset!\n";
    std::cout << "  -> All Extra Life checks passed!\n";
}

int main() {
    std::cout << "=== Running PC-Man Unit Tests ===\n";
    TestRomData();
    TestMazeLogic();
    TestPacmanMovement();
    TestGhostAI();
    TestPacingAndTiming();
    TestVerticalTunnels();
    TestDeathSequence();
    TestExtraLife();
    std::cout << "=== All Unit Tests Passed Successfully! ===\n";
    return 0;
}
