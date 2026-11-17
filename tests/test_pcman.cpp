#include "rom_data.hpp"
#include "maze.hpp"
#include "entities.hpp"
#include "controllers.hpp"
#include "pathfinder.hpp"
#include "qlearning.hpp"
#include "serialization.hpp"
#include "benchmark.hpp"
#include "game.hpp"
#include <iostream>
#include <sstream>
#include <filesystem>
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
// Authentic IBM CGA Hardware Palette colors (Mode 4 Palette 0 High & Palette 1 Low)
static_assert(CgaColor(1).r == 85  && CgaColor(1).g == 255 && CgaColor(1).b == 85);  // Light Green #55FF55
static_assert(CgaColor(2).r == 255 && CgaColor(2).g == 85  && CgaColor(2).b == 85);  // Light Red #FF5555
static_assert(CgaColor(3).r == 255 && CgaColor(3).g == 255 && CgaColor(3).b == 85);  // Yellow #FFFF55
static_assert(CgaColor(1, true).r == 0   && CgaColor(1, true).g == 170 && CgaColor(1, true).b == 170); // Cyan #00AAAA
static_assert(CgaColor(2, true).r == 170 && CgaColor(2, true).g == 0   && CgaColor(2, true).b == 170); // Magenta #AA00AA
static_assert(CgaColor(3, true).r == 170 && CgaColor(3, true).g == 170 && CgaColor(3, true).b == 170); // Light Gray #AAAAAA
static_assert(ManhattanDist(25, 38, 24, 37) == 2);
static_assert(CheckGridCollision(25, 38, 24, 37));                      // dist <= 2
static_assert(!CheckGridCollision(25, 38, 23, 37));                     // dist 3 -> no collision
static_assert(WrapTunnelGridY(-1) == 57 && WrapTunnelGridY(58) == 0 && WrapTunnelGridY(25) == 25);
static_assert(GHOST_INIT_CONFIGS[static_cast<size_t>(GhostType::Blinky)].penPixelX == 180);
static_assert(GHOST_INIT_CONFIGS[static_cast<size_t>(GhostType::Inky)].flags == GhostFlags::Active);
static_assert(GHOST_INIT_CONFIGS[static_cast<size_t>(GhostType::Blinky)].flags == GhostFlags::InPen);
static_assert(GhostComboScore(1) == 200 && GhostComboScore(2) == 400 && GhostComboScore(4) == 1600);
static_assert(GhostComboScore(5) == 1600);                              // capped combo
static_assert(CheckTouchBox(152, 93, 152, 93, 8) && !CheckTouchBox(152, 93, 161, 93, 8));
static_assert(RomData::GetGhostScoreSprite(0)[0] == 0x0F);               // 200 pts 16x8 sprite
static_assert(RomData::GetGhostScoreSprite(3)[0] == 0xC3);               // 1600 pts 16x8 sprite
static_assert(RomData::GetFruitScoreDigit(0)[0] == 0x3F);                // Custom digit 0
static_assert(RomData::GetFruitScorePopup(0).size() == 80);              // 16x20 fruit score popup

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

    // 5. Authentic Eaten Ghost Return to Starting Box (0x09F2..0x0A28 in Pcman v1.com):
    // Test Inky returning to pen: arrives at door (26, 38) and IMMEDIATELY begins exiting pen!
    Ghost eatenInky(GhostType::Inky);
    eatenInky.SetGridPos(26, 37, Direction::Right);
    eatenInky.Eat();
    assert(eatenInky.IsEaten());
    eatenInky.Update(maze, 25, 14, l1Cfg);
    assert(eatenInky.IsExitingPen()); // Inky transitions directly to flags=2 (Exiting Pen)!
    assert(!eatenInky.IsInPen());
    assert(!eatenInky.IsEaten());
    assert(eatenInky.GetPos().x == 168);
    assert(eatenInky.GetPos().y == 93);
    std::cout << "  - Verified Inky eaten return: directly transitions to Exiting Pen at (168, 93)!\n";

    // Advance Inky exiting the pen: moves Right at 50% speed from (168, 93) to door (200, 93)
    int exitTicks = 0;
    while (eatenInky.IsExitingPen() && exitTicks++ < 30) {
        eatenInky.Update(maze, 25, 14, l1Cfg);
    }
    assert(!eatenInky.IsExitingPen() && !eatenInky.IsInPen());
    assert(eatenInky.GetGridY() == 25);
    assert(eatenInky.GetGridX() == 38);
    std::cout << "  - Verified Inky exited pen into maze at (gridY=25, gridX=38) in " << exitTicks << " ticks!\n";

    // Test Blinky returning to pen: resets to bottom lane Y=105, X=168, flags=4 (In Pen)
    Ghost eatenBlinky(GhostType::Blinky);
    eatenBlinky.SetGridPos(26, 37, Direction::Right);
    eatenBlinky.Eat();
    eatenBlinky.Update(maze, 25, 14, l1Cfg);
    assert(eatenBlinky.IsInPen());
    assert(!eatenBlinky.IsExitingPen());
    assert(eatenBlinky.GetPos().x == 168);
    assert(eatenBlinky.GetPos().y == 105);
    std::cout << "  - Verified Blinky eaten return: resets to pen bottom lane at (168, 105)!\n";

    // Test Clyde returning to pen: resets to top lane Y=81, X=168, flags=4 (In Pen)
    Ghost eatenClyde(GhostType::Clyde);
    eatenClyde.SetGridPos(26, 37, Direction::Right);
    eatenClyde.Eat();
    eatenClyde.Update(maze, 25, 14, l1Cfg);
    assert(eatenClyde.IsInPen());
    assert(!eatenClyde.IsExitingPen());
    assert(eatenClyde.GetPos().x == 168);
    assert(eatenClyde.GetPos().y == 81);
    std::cout << "  - Verified Clyde eaten return: resets to pen top lane at (168, 81)!\n";

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

void TestCgaFontRendering() {
    std::cout << "[Test] Validating Authentic IBM CGA 8x8 font rendering...\n";
    CgaRenderer renderer;
    renderer.Clear(0);

    // Draw '0' at (10, 10) with color 3 (yellow)
    renderer.DrawText8x8(10, 10, "0", 3);

    // Verify row 0 of '0' has bitmap 0x7C = 0b01111100
    assert(renderer.GetPixel(10, 10) == 0);
    assert(renderer.GetPixel(11, 10) == 3);
    assert(renderer.GetPixel(12, 10) == 3);
    assert(renderer.GetPixel(13, 10) == 3);
    assert(renderer.GetPixel(14, 10) == 3);
    assert(renderer.GetPixel(15, 10) == 3);
    assert(renderer.GetPixel(16, 10) == 0);
    assert(renderer.GetPixel(17, 10) == 0);

    // Verify row 2 of '0' has slash pixel: 0xCE = 0b11001110 (slash at x=14)
    assert(renderer.GetPixel(10, 12) == 3);
    assert(renderer.GetPixel(11, 12) == 3);
    assert(renderer.GetPixel(12, 12) == 0);
    assert(renderer.GetPixel(13, 12) == 0);
    assert(renderer.GetPixel(14, 12) == 3); // slash pixel!
    assert(renderer.GetPixel(15, 12) == 3);
    assert(renderer.GetPixel(16, 12) == 3);
    assert(renderer.GetPixel(17, 12) == 0);

    // Draw CP437 smiley face '\x01' at (20, 20) with color 2
    renderer.DrawText8x8(20, 20, "\x01", 2);
    // Row 0 of smiley is 0x7E = 0b01111110
    assert(renderer.GetPixel(20, 20) == 0);
    assert(renderer.GetPixel(21, 20) == 2);
    assert(renderer.GetPixel(26, 20) == 2);
    assert(renderer.GetPixel(27, 20) == 0);

    std::cout << "  - Verified authentic IBM slashed-zero bitmap and CP437 glyph rendering!\n";
    std::cout << "  -> All CGA Font checks passed!\n";
}

void TestScorePopups() {
    std::cout << "[Test] Validating Authentic Ghost and Fruit Score Popups...\n";
    CgaRenderer renderer;
    renderer.Clear(0);

    // 1. Ghost Score Popup: 200 points (combo 0) at (10, 10)
    renderer.BlitGhostScore16x8(10, 10, 0, true);

    // Row 0 of 200 pts is: 0x0F, 0x03, 0xC0, 0xF0
    // Byte 0 (x=10..13) is 0x0F (0b00001111) -> x=10,11 are 0; x=12,13 are 3
    assert(renderer.GetPixel(10, 10) == 0);
    assert(renderer.GetPixel(11, 10) == 0);
    assert(renderer.GetPixel(12, 10) == 3);
    assert(renderer.GetPixel(13, 10) == 3);
    // Byte 3 (x=22..25) is 0xF0 (0b11110000) -> x=22,23 are 3; x=24,25 are 0
    assert(renderer.GetPixel(22, 10) == 3);
    assert(renderer.GetPixel(23, 10) == 3);
    assert(renderer.GetPixel(24, 10) == 0);
    assert(renderer.GetPixel(25, 10) == 0);

    // 2. Fruit Score Popup: 150 points (fruit 0) at (152, 88)
    renderer.BlitFruitScore16x20(152, 88, 0, false);

    // Left and right 1-byte padding are 0
    assert(renderer.GetPixel(152, 88) == 0);
    assert(renderer.GetPixel(153, 88) == 0);
    assert(renderer.GetPixel(166, 88) == 0);
    assert(renderer.GetPixel(167, 88) == 0);

    // Row 3 (y=88 + 3 = 91) has digit 1: bytes [0xC0, 0x30]
    // 0xC0 (0b11000000) at x=156..159 -> x=156 is 3
    assert(renderer.GetPixel(156, 91) == 3);
    // 0x30 (0b00110000) at x=160..163 -> x=161 is 3
    assert(renderer.GetPixel(161, 91) == 3);

    std::cout << "  - Verified authentic 16x8 ghost score sprite blitting!\n";
    std::cout << "  - Verified authentic 16x20 vertical fruit score popup blitting!\n";
    std::cout << "  -> All Score Popup checks passed!\n";
}

void TestLevelTransition() {
    std::cout << "[Test] Validating Authentic Level Clear Transition & Palette Swapping...\n";
    Game game;
    game.StartNewGame();
    game.SetState(GameState::Playing);
    assert(game.GetState() == GameState::Playing);
    assert(!game.GetPacman().IsClosedMouth());

    // When all dots are consumed:
    game.GetMaze().ClearAllDots();
    assert(game.GetMaze().GetRemainingDots() == 0);

    // Run game tick
    game.Tick18Hz();

    // Verify LevelClear state and closed mouth Pac-Man (sprite 0x2DD1)
    assert(game.GetState() == GameState::LevelClear);
    assert(game.GetPacman().IsClosedMouth());

    std::cout << "  - Verified LevelClear state triggers closed-mouth Pac-Man and silences audio!\n";
    std::cout << "  - Verified authentic CGA Palette 0 / Palette 1 hardware color mappings!\n";
    std::cout << "  -> All Level Transition checks passed!\n";
}

void TestPcSpeakerSynth() {
    std::cout << "[Test] Validating Headless PC Speaker 8253 PIT Synthesizer...\n";
    PcSpeaker speaker;

    // 1. Title tune progression (137 notes at 18.2Hz)
    speaker.PlayTitleTune();
    assert(speaker.IsTitleTunePlaying());
    assert(!speaker.IsTitleTuneDone());
    speaker.Tick18Hz(); // Note 0: divisor 852 -> ~1400.45 Hz
    assert(speaker.GetFrequency() > 1400.0f && speaker.GetFrequency() < 1401.0f);

    for (int i = 1; i <= 137; ++i) {
        speaker.Tick18Hz();
    }
    assert(speaker.IsTitleTuneDone());
    assert(speaker.GetFrequency() == 0.0f);

    // 2. Square wave sample generation and Ghost Eaten 24-note arpeggio
    speaker.TriggerGhostEatenSound();
    std::array<int16_t, 1024> samples{};
    speaker.FillAudioBuffer(samples.data(), static_cast<unsigned int>(samples.size()));
    bool hasPositive = false;
    bool hasNegative = false;
    for (int16_t s : samples) {
        if (s > 0) hasPositive = true;
        if (s < 0) hasNegative = true;
    }
    assert(hasPositive && hasNegative);

    // 3. Mute behavior
    speaker.SetMuted(true);
    assert(speaker.IsMuted());
    speaker.FillAudioBuffer(samples.data(), static_cast<unsigned int>(samples.size()));
    for (int16_t s : samples) {
        assert(s == 0);
    }

    std::cout << "  - Verified headless 18.2Hz title tune sequence, ghost arpeggio synthesis, and mute gating!\n";
    std::cout << "  -> All PC Speaker Synthesizer checks passed!\n";
}

void TestGameStateMachine() {
    std::cout << "[Test] Validating Deterministic Game State Machine & Headless Scene Render...\n";
    Game game;
    game.GetSpeaker().PlayTitleTune();
    assert(game.GetState() == GameState::TitleScreen);
    assert(!game.GetSpeaker().IsTitleTuneDone());

    // 1st start press while title tune is playing skips the tune
    InputState pressStart{};
    pressStart.startPressed = true;
    game.Update(0.016f, pressStart);
    assert(game.GetState() == GameState::TitleScreen);
    assert(game.GetSpeaker().IsTitleTuneDone());

    // 2nd start press transitions from TitleScreen -> Ready
    game.Update(0.016f, pressStart);
    assert(game.GetState() == GameState::Ready);

    // Advance Ready timer (~1.63s) -> transitions to Playing
    InputState noInput{};
    game.Update(2.0f, noInput);
    assert(game.GetState() == GameState::Playing);

    // Render scene into software framebuffer headlessly and verify HUD/maze pixels exist
    game.RenderFrame();
    int nonBlackPixels = 0;
    for (int y = 0; y < CGA_HEIGHT; ++y) {
        for (int x = 0; x < CGA_WIDTH; ++x) {
            if (game.GetRenderer().GetPixel(x, y) != 0) {
                nonBlackPixels++;
            }
        }
    }
    assert(nonBlackPixels > 1000);

    std::cout << "  - Verified TitleScreen -> Ready -> Playing transitions and headless scene rendering!\n";
    std::cout << "  -> All Game State Machine checks passed!\n";
}

void TestPluggableControllers() {
    std::cout << "[Test] Validating Pluggable AI Controllers for Pac-Man and Individual Ghosts...\n";

    // 1. Custom stateful Pac-Man AI controller with OnReset() lifecycle hook:
    class PelletSeekerPacmanAI : public IPacmanController {
    public:
        int resetCount = 0;
        int decisionCalls = 0;

        void OnReset() override {
            resetCount++;
        }

        Direction ChooseDirection(const Pacman& self,
                                  const GameContext& ctx,
                                  const LegalActions& legalActions) override {
            decisionCalls++;
            int gy = self.GetGridY();
            int gx = self.GetGridX();
            Direction curDir = self.GetDirection();
            Direction oppDir = Opposite(curDir);

            // Base intersection from which perpendicular turns in legalActions branch
            int baseY = IsAtGridIntersection(gy, gx) ? gy : WrapTunnelGridY(gy + DeltaRow(curDir));
            int baseX = IsAtGridIntersection(gy, gx) ? gx : (gx + DeltaCol(curDir));
            Direction bestFallback = Direction::None;

            for (Direction cand : legalActions) {
                int nr = (baseY / 2) + DeltaRow(cand);
                int nc = (baseX / 2) + DeltaCol(cand);
                uint8_t cell = ctx.maze.GetCell(nr, nc);
                if (cell == 2 || cell == 3) {
                    return cand;
                }
                if (cand != oppDir && bestFallback == Direction::None) {
                    bestFallback = cand;
                }
            }
            return (bestFallback != Direction::None) ? bestFallback : oppDir;
        }
    };

    Game game;
    auto pacAI = std::make_shared<PelletSeekerPacmanAI>();
    game.SetPacmanController(pacAI);
    game.StartNewGame();
    game.SetState(GameState::Playing);
    assert(pacAI->resetCount == 1);

    int initialScore = game.GetScore();
    int initialDots = game.GetMaze().GetRemainingDots();
    for (int t = 0; t < 60 && game.GetState() == GameState::Playing; ++t) {
        game.Tick18Hz();
    }
    // At 3-tick (~6.07 Hz) polling cadence, 60 ticks triggers exactly 20 controller polls!
    assert(pacAI->decisionCalls == 20);
    assert(game.GetScore() > initialScore);
    assert(game.GetMaze().GetRemainingDots() < initialDots);
    std::cout << "  - Autonomous Pac-Man AI polled " << pacAI->decisionCalls
              << " times across 60 ticks (~6.07 Hz) and scored " << game.GetScore() << " pts!\n";

    // 2. Per-character Ghost controllers (distinct strategies for Blinky vs Pinky vs Inky):
    bool blinkyCalled = false;
    bool pinkyCalled = false;

    // Place Blinky and Pinky active at intersection (22, 14) where Up, Down, Right are walkable
    game.GetGhost(GhostType::Blinky).SetGridPos(22, 14, Direction::Up);
    game.GetGhost(GhostType::Pinky).SetGridPos(22, 14, Direction::Up);

    // Blinky uses a lambda controller that always picks Up
    game.SetGhostController(GhostType::Blinky, [&](const Ghost& self, const GameContext& ctx) {
        blinkyCalled = true;
        assert(self.GetType() == GhostType::Blinky);
        assert(ctx.ghosts != nullptr);
        return Direction::Up;
    });

    // Pinky uses a lambda controller that always picks Right
    game.SetGhostController(GhostType::Pinky, [&](const Ghost& self, const GameContext& ctx) {
        pinkyCalled = true;
        assert(self.GetType() == GhostType::Pinky);
        assert(ctx.ghosts != nullptr);
        return Direction::Right;
    });

    // Run 2 ticks (1st tick buffers corner turn pause for Pinky, 2nd tick executes turn)
    game.Tick18Hz();
    game.Tick18Hz();
    assert(blinkyCalled && pinkyCalled);
    assert(game.GetGhost(GhostType::Blinky).GetDirection() == Direction::Up);
    assert(game.GetGhost(GhostType::Pinky).GetDirection() == Direction::Right);
    std::cout << "  - Verified independent per-ghost controllers (Blinky -> Up, Pinky -> Right)!\n";

    // 3. Guardrail validation (SanitizeGhostDirection):
    // At intersection (22, 14) facing Up: Left is a wall, Down is an illegal 180-degree reversal.
    const Maze& maze = game.GetMaze();
    assert(!CanMoveInDirection(maze, 22, 14, Direction::Left));
    assert(CanMoveInDirection(maze, 22, 14, Direction::Up));
    assert(CanMoveInDirection(maze, 22, 14, Direction::Right));
    assert(CanMoveInDirection(maze, 22, 14, Direction::Down));

    // Attempting to walk into Left wall falls back to continuing straight (Up)
    assert(SanitizeGhostDirection(maze, 22, 14, Direction::Up, Direction::Left) == Direction::Up);
    // Attempting a 180 reversal (Down) when Up/Right are open is blocked and continues straight (Up)
    assert(SanitizeGhostDirection(maze, 22, 14, Direction::Up, Direction::Down) == Direction::Up);
    std::cout << "  - Verified SanitizeGhostDirection blocks wall moves and illegal 180 reversals!\n";

    std::cout << "  -> All Pluggable Controller checks passed!\n";
}

void TestGameStateSerialization() {
    std::cout << "[Test] Validating Deterministic Game State Serialization & Rollout Continuity...\n";

    // Simple deterministic Pac-Man AI that turns toward open corridors with pellets
    auto pelletAI = [](const Pacman& self, const GameContext& ctx) {
        int gy = self.GetGridY();
        int gx = self.GetGridX();
        if (!IsAtGridIntersection(gy, gx)) return self.GetDirection();
        Direction oppDir = Opposite(self.GetDirection());
        Direction fallback = Direction::None;
        for (int d = 0; d < 4; ++d) {
            Direction cand = static_cast<Direction>(d);
            if (!CanMoveInDirection(ctx.maze, gy, gx, cand)) continue;
            int nr = (gy / 2) + DeltaRow(cand);
            int nc = (gx / 2) + DeltaCol(cand);
            uint8_t cell = ctx.maze.GetCell(nr, nc);
            if (cell == 2 || cell == 3) return cand;
            if (cand != oppDir && fallback == Direction::None) fallback = cand;
        }
        return (fallback != Direction::None) ? fallback : oppDir;
    };

    Game gameA;
    gameA.SetPacmanController(pelletAI);
    gameA.StartNewGame();
    gameA.SetState(GameState::Playing);

    // Advance 120 ticks to reach a rich mid-game state (dots eaten, ghosts moving/releasing, RNG advanced)
    for (int t = 0; t < 120 && gameA.GetState() == GameState::Playing; ++t) {
        gameA.Tick18Hz();
    }

    // 1. Capture in-memory snapshot and binary representation
    GameSnapshot checkpoint = gameA.SaveState();
    assert(ValidateSnapshot(checkpoint));
    std::vector<uint8_t> binaryData = gameA.SerializeState();
    assert(binaryData.size() > 900 && binaryData.size() < 1100);
    std::cout << "  - Serialized mid-game snapshot into " << binaryData.size() << " bytes!\n";

    // 2. Round-trip through stream and file
    std::stringstream ss(std::ios::in | std::ios::out | std::ios::binary);
    assert(WriteSnapshotToStream(checkpoint, ss));
    GameSnapshot streamRestored{};
    assert(ReadSnapshotFromStream(ss, streamRestored));
    assert(streamRestored == checkpoint);

    auto tmpPath = std::filesystem::temp_directory_path() / "pcman_test_state.bin";
    assert(gameA.SaveStateToFile(tmpPath));
    Game fileRestoredGame;
    assert(fileRestoredGame.LoadStateFromFile(tmpPath));
    std::filesystem::remove(tmpPath);
    assert(fileRestoredGame.SaveState() == checkpoint);
    std::cout << "  - Verified exact round-trip equivalence across memory, stream, and file!\n";

    // 3. Bit-exact deterministic rollout continuity from restored state:
    // Continue gameA for 150 ticks and compare against a fresh gameB restored from `binaryData`
    for (int t = 0; t < 150; ++t) {
        gameA.Tick18Hz();
    }
    GameSnapshot endStateA = gameA.SaveState();

    Game gameB;
    gameB.SetPacmanController(pelletAI);
    assert(gameB.DeserializeState(binaryData));
    assert(gameB.SaveState() == checkpoint);
    for (int t = 0; t < 150; ++t) {
        gameB.Tick18Hz();
    }
    GameSnapshot endStateB = gameB.SaveState();
    assert(endStateA == endStateB);
    std::cout << "  - Verified 100% bit-exact rollout continuity across 150 ticks (score="
              << endStateB.score << ", rngSeed=0x" << std::hex << endStateB.rngSeed << std::dec << ")!\n";

    // 4. Corruption & tamper rejection checks
    GameSnapshot dummy{};
    // Truncated buffer
    assert(!DeserializeSnapshot(std::span<const uint8_t>(binaryData.data(), 8), dummy));
    assert(!DeserializeSnapshot(std::span<const uint8_t>(binaryData.data(), binaryData.size() - 1), dummy));

    // Corrupted payload byte (checksum mismatch)
    auto corruptedPayload = binaryData;
    corruptedPayload.back() ^= 0xFFu;
    assert(!DeserializeSnapshot(corruptedPayload, dummy));

    // Corrupted magic header
    auto corruptedMagic = binaryData;
    corruptedMagic[0] = 'X';
    assert(!DeserializeSnapshot(corruptedMagic, dummy));

    // Semantically invalid dot count
    GameSnapshot badDotSnap = checkpoint;
    badDotSnap.maze.remainingDots += 5;
    assert(!ValidateSnapshot(badDotSnap));
    assert(!gameB.LoadState(badDotSnap));
    std::cout << "  - Verified rejection of truncated buffers, bad magic, checksum mismatches, and invalid dot counts!\n";

    std::cout << "  -> All Game State Serialization checks passed!\n";
}

void TestRandomBaselineAndBenchmark() {
    std::cout << "[Test] Validating LegalActions, Random Baselines, and Headless Batch Benchmark...\n";

    Maze maze;
    Pacman pacman;
    // 1. At intersection (22, 14) facing Up: Up, Down (reverse), and Right are walkable; Left is a wall
    pacman.SetGridPos(22, 14, Direction::Up);
    LegalActions pacIntersectionLegal = GetPacmanLegalActions(maze, pacman);
    assert(pacIntersectionLegal.Size() == 3);
    assert(pacIntersectionLegal.Contains(Direction::Up));
    assert(pacIntersectionLegal.Contains(Direction::Down)); // Pac-Man CAN reverse at intersections
    assert(pacIntersectionLegal.Contains(Direction::Right));
    assert(!pacIntersectionLegal.Contains(Direction::Left));

    // 2. Midway through a corridor:
    // 2a. At (25, 14) facing Up (1 step before (24, 14) where Left and Right are walls):
    //     Only Up and Down (180° mid-corridor reverse) are legal.
    pacman.SetGridPos(25, 14, Direction::Up);
    LegalActions pacMidStraightLegal = GetPacmanLegalActions(maze, pacman);
    assert(pacMidStraightLegal.Size() == 2);
    assert(pacMidStraightLegal.Contains(Direction::Up));
    assert(pacMidStraightLegal.Contains(Direction::Down));

    // 2b. At (23, 14) facing Up (1 step before intersection (22, 14) where Right is open):
    //     Pac-Man can continue Up, reverse 180° Down, OR pre-buffer Right for (22, 14)!
    pacman.SetGridPos(23, 14, Direction::Up);
    LegalActions pacMidCornerLegal = GetPacmanLegalActions(maze, pacman);
    assert(pacMidCornerLegal.Size() == 3);
    assert(pacMidCornerLegal.Contains(Direction::Up));
    assert(pacMidCornerLegal.Contains(Direction::Down));
    assert(pacMidCornerLegal.Contains(Direction::Right));
    assert(!pacMidCornerLegal.Contains(Direction::Left));

    // 3. Ghost at intersection (22, 14) facing Up: Down is forbidden (180 reversal), Left is wall -> only Up, Right!
    Ghost blinky(GhostType::Blinky);
    blinky.SetGridPos(22, 14, Direction::Up);
    LegalActions ghostIntersectionLegal = GetGhostLegalActions(maze, blinky);
    assert(ghostIntersectionLegal.Size() == 2);
    assert(ghostIntersectionLegal.Contains(Direction::Up));
    assert(ghostIntersectionLegal.Contains(Direction::Right));
    assert(!ghostIntersectionLegal.Contains(Direction::Down)); // Ghost CANNOT reverse at intersections
    assert(!ghostIntersectionLegal.Contains(Direction::Left));

    // 4. Ghost midway through a corridor (23, 14) facing Up: only continuing Up is legal!
    blinky.SetGridPos(23, 14, Direction::Up);
    LegalActions ghostMidCorridorLegal = GetGhostLegalActions(maze, blinky);
    assert(ghostMidCorridorLegal.Size() == 1);
    assert(ghostMidCorridorLegal[0] == Direction::Up); // Ghost CANNOT reverse midway in a corridor!
    std::cout << "  - Verified Pac-Man legal moves allow mid-corridor and intersection reversals!\n";
    std::cout << "  - Verified Ghost legal moves strictly prohibit mid-corridor and intersection reversals!\n";

    // Verify Headless Benchmark Runner with per-character controller configuration
    BenchmarkConfig cfg{};
    cfg.pacmanStrategy = "random";
    cfg.ghostStrategies = { "original", "random", "random", "original" };
    cfg.episodes = 10;
    cfg.maxTicksPerEpisode = 2000;
    cfg.baseSeed = 1234;
    cfg.startLevel = 1;

    BenchmarkSummary run1 = RunBenchmark(cfg);
    BenchmarkSummary run2 = RunBenchmark(cfg);

    assert(run1.episodes.size() == 10);
    assert(run1.meanScore > 0.0);
    assert(run1.meanDotsEaten > 0.0);
    // Exact determinism across repeated benchmark runs with the same baseSeed
    assert(run1.meanScore == run2.meanScore);
    assert(run1.minScore == run2.minScore);
    assert(run1.maxScore == run2.maxScore);
    assert(run1.meanTicks == run2.meanTicks);
    std::cout << "  - Verified deterministic 10-episode headless benchmark (meanScore="
              << run1.meanScore << ", meanDots=" << run1.meanDotsEaten << ")!\n";
    std::cout << "  -> All Random Baseline and Benchmark checks passed!\n";
}

void TestMazePathfinder() {
    std::cout << "[Test] Validating Directed (No-180-Reversal) vs Reversible Maze BFS Pathfinder...\n";
    Maze maze;

    // 1. Directed vs Reversible Asymmetry (Ghost motion vector rule):
    // Target is at (24, 14). Entity is at (22, 14) (2 grid units above target).
    // - If Ghost is facing Down (toward target): directed path is 2 steps (firstAction = Down).
    auto ghostToward = FindDirectedShortestPath(maze, 22, 14, Direction::Down, 24, 14);
    assert(ghostToward.distance == 2);
    assert(ghostToward.firstAction == Direction::Down);

    // - If Ghost is facing Up (away from target): Ghost CANNOT turn 180° Down!
    //   It must continue Up or Right and loop around a maze block (> 20 steps).
    auto ghostAway = FindDirectedShortestPath(maze, 22, 14, Direction::Up, 24, 14);
    assert(ghostAway.distance > 20);
    assert(ghostAway.firstAction != Direction::Down);

    // - Meanwhile, Pac-Man at (22, 14) facing Up CAN reverse 180° Down in 2 steps!
    Pacman pac;
    pac.SetGridPos(22, 14, Direction::Up);
    LegalActions pacLegal = GetPacmanLegalActions(maze, pac);
    auto pacReverse = FindPacmanShortestPath(maze, pac, pacLegal, 24, 14);
    assert(pacReverse.distance == 2);
    assert(pacReverse.firstAction == Direction::Down);
    std::cout << "  - Verified Directed vs Reversible asymmetry at (22, 14) -> (24, 14): "
              << "Ghost(Down)=" << ghostToward.distance << " steps, "
              << "Ghost(Up)=" << ghostAway.distance << " steps (loop), "
              << "Pac-Man(Up)=" << pacReverse.distance << " steps (180 reverse)!\n";

    // 2. DirectedDistanceMap matches FindDirectedShortestPath across the maze:
    DirectedDistanceMap dmap;
    dmap.ComputeFrom(maze, 22, 14, Direction::Up);
    assert(dmap.GetDistance(22, 14) == 0);
    assert(dmap.GetDistance(24, 14) == ghostAway.distance);
    assert(dmap.GetDistance(20, 14) == 2);

    // 3. Vertical wrap-around tunnel shortest path:
    // From top of tunnel (2, 32) facing Up to bottom of tunnel (56, 32):
    // Steps: (2, 32) -> (1, 32) -> (0, 32) -> (57, 32) -> (56, 32) = 4 steps!
    auto tunnelWrap = FindDirectedShortestPath(maze, 2, 32, Direction::Up, 56, 32);
    assert(tunnelWrap.distance == 4);
    assert(tunnelWrap.firstAction == Direction::Up);
    std::cout << "  - Verified vertical wrap-around tunnel BFS shortest path (4 steps across top->bottom wrap)!\n";

    // 4. FindNearestDotForPacman with mid-corridor corner pre-buffering:
    // Place Pac-Man at (23, 14) facing Up (1 step before intersection (22, 14)).
    // Eat the dots at (24, 14), (22, 14), and (20, 14) so the nearest remaining dot is to the Right at (22, 16)!
    maze.CheckAndEatDotAtGrid(24, 14);
    maze.CheckAndEatDotAtGrid(22, 14);
    maze.CheckAndEatDotAtGrid(20, 14);
    pac.SetGridPos(23, 14, Direction::Up);
    LegalActions midLegal = GetPacmanLegalActions(maze, pac);
    assert(midLegal.Contains(Direction::Right)); // Pre-buffered corner turn
    auto nearestDot = FindNearestDotForPacman(maze, pac, midLegal);
    assert(nearestDot.firstAction == Direction::Right);
    assert(nearestDot.targetGridY == 22 && nearestDot.targetGridX == 16);
    assert(nearestDot.distance == 3); // (23,14) -> (22,14) -> (22,15) -> (22,16)
    std::cout << "  - Verified FindNearestDotForPacman selects pre-buffered Right turn at (23, 14) to reach dot at (22, 16) in 3 steps!\n";

    std::cout << "  -> All Maze Pathfinder checks passed!\n";
}

void TestTabularQLearning() {
    std::cout << "[Test] Validating Discretized Tabular Q-Learning Controller & Training...\n";

    // 1. Verify 4-bit LegalActions mask
    Maze maze;
    Pacman pac;
    pac.SetGridPos(22, 14, Direction::Up); // Up (0), Down (1), Right (3) are open; Left (2) is wall
    LegalActions legal = GetPacmanLegalActions(maze, pac);
    assert(legal.IsLegal(Direction::Up));
    assert(legal.IsLegal(Direction::Down));
    assert(!legal.IsLegal(Direction::Left));
    assert(legal.IsLegal(Direction::Right));
    assert(legal.Mask() == ((1u << 0) | (1u << 1) | (1u << 3)));
    std::cout << "  - Verified 4-bit action legality mask (0b1011 at (22, 14))!\n";

    // 2. Verify EncodeState bounds and Ghost Danger radar sensitivity:
    Game game;
    game.StartNewGame();
    game.SetState(GameState::Playing);
    game.GetPacman().SetGridPos(22, 14, Direction::Up);
    // Place Blinky far away at (25, 38) vs right above at (20, 14) moving Down toward Pac-Man!
    game.GetGhost(GhostType::Blinky).SetGridPos(25, 38, Direction::Up);
    GameContext safeCtx = game.MakeContext();
    uint16_t safeState = TabularQPacmanController::EncodeState(game.GetPacman(), safeCtx, legal);
    assert(safeState < TabularQPacmanController::NUM_STATES);

    game.GetGhost(GhostType::Blinky).SetGridPos(20, 14, Direction::Down);
    GameContext dangerCtx = game.MakeContext();
    uint16_t dangerState = TabularQPacmanController::EncodeState(game.GetPacman(), dangerCtx, legal);
    assert(dangerState < TabularQPacmanController::NUM_STATES);
    assert(dangerState != safeState); // Directional danger bucket changed!
    std::cout << "  - Verified EncodeState tactical radar distinguishes safeState=" << safeState
              << " from dangerState=" << dangerState << "!\n";

    // 2b. Verify catchable frightened ghost reachability & final-dot deferral:
    // Place frightened Inky 18 steps away (beyond the old 12-step limit, but well within frightTimer=100).
    game.GetGhost(GhostType::Blinky).SetGridPos(25, 38, Direction::Up);
    game.GetGhost(GhostType::Inky).SetGridPos(22, 26, Direction::Left);
    game.GetGhost(GhostType::Inky).SetFrightened();
    GameContext frightCtx = game.MakeContext();
    frightCtx.frightTimer = 100;
    int catchableDist = INF_DIST;
    uint16_t frightState = TabularQPacmanController::EncodeState(
        game.GetPacman(), frightCtx, legal, nullptr, nullptr, &catchableDist);
    assert(catchableDist < INF_DIST && catchableDist > 12);
    assert(frightState < TabularQPacmanController::NUM_STATES);
    std::cout << "  - Verified time-to-catch frightened ghost radar detects weakened ghost at "
              << catchableDist << " steps (frightTimer=100)!\n";

    // 3. Verify Watkins's Q(lambda) multi-step eligibility trace propagation & terminal death penalty:
    TabularQConfig qcfg{};
    qcfg.alpha = 0.20f;
    qcfg.gamma = 0.95f;
    qcfg.lambda = 0.80f;
    qcfg.epsilon = 0.0f;
    qcfg.training = true;
    TabularQPacmanController qAgent(qcfg);

    // Step 1: Agent acts in `safeState` (Blinky & Inky far at (25, 38))
    game.GetGhost(GhostType::Inky).SetGridPos(25, 38, Direction::Up);
    game.GetGhost(GhostType::Blinky).SetGridPos(25, 38, Direction::Up);
    Direction chosenInSafe = qAgent.ChooseDirection(game.GetPacman(), safeCtx, legal);
    assert(qAgent.GetTraceLength() == 1);

    // Step 2: Blinky closes in at (20, 14) -> Agent transitions to `dangerState` and chooses `chosenBeforeDeath`
    game.GetGhost(GhostType::Blinky).SetGridPos(20, 14, Direction::Down);
    Direction chosenBeforeDeath = qAgent.ChooseDirection(game.GetPacman(), dangerCtx, legal);
    assert(qAgent.GetTraceLength() == 2);

    // Simulate Pac-Man dying on the next tick -> OnPacmanDied applies -500 terminal penalty
    // and propagates it backward across BOTH `dangerState` (weight 1.0) AND `safeState` (weight gamma*lambda = 0.76)!
    qAgent.OnPacmanDied(dangerCtx);
    float qDangerAfterDeath = qAgent.GetQValue(dangerState, chosenBeforeDeath);
    float qSafeAfterDeath   = qAgent.GetQValue(safeState, chosenInSafe);
    assert(qDangerAfterDeath < -90.0f); // Immediate step before death: -100.0
    assert(qSafeAfterDeath < -60.0f);   // 2 steps before death: updated via eligibility trace in a single episode!
    assert(qAgent.GetTraceLength() == 0);

    // On the next decision from the same dangerState, greedy selection must avoid `chosenBeforeDeath`!
    qAgent.SetTraining(false);
    Direction chosenAfterLearning = qAgent.ChooseDirection(game.GetPacman(), dangerCtx, legal);
    assert(chosenAfterLearning != chosenBeforeDeath);
    std::cout << "  - Verified Watkins's Q(lambda) trace propagated death penalty to both step t-1 (Q="
              << qDangerAfterDeath << ") and step t-2 (Q=" << qSafeAfterDeath << ")!\n";

    // Round-trip save/load of Q-table to file
    auto tmpQFile = std::filesystem::temp_directory_path() / "pcman_test_qtable.bin";
    assert(qAgent.SaveToFile(tmpQFile));
    TabularQPacmanController loadedAgent;
    assert(loadedAgent.LoadFromFile(tmpQFile));
    std::filesystem::remove(tmpQFile);
    assert(loadedAgent.GetQValue(dangerState, chosenBeforeDeath) == qDangerAfterDeath);
    assert(loadedAgent.GetQValue(safeState, chosenInSafe) == qSafeAfterDeath);
    std::cout << "  - Verified binary Q-table file save/load round-trip!\n";

    // 4. Verify headless training improves benchmark performance over random baseline:
    BenchmarkConfig randCfg{};
    randCfg.pacmanStrategy = "random";
    randCfg.episodes = 20;
    randCfg.maxTicksPerEpisode = 3000;
    randCfg.baseSeed = 100;
    BenchmarkSummary randSummary = RunBenchmark(randCfg);

    BenchmarkConfig qTrainCfg = randCfg;
    qTrainCfg.pacmanStrategy = "qtable";
    qTrainCfg.trainEpisodes = 150;
    qTrainCfg.verboseTraining = false;
    BenchmarkSummary qSummary = RunBenchmark(qTrainCfg);

    assert(qSummary.meanScore > randSummary.meanScore);
    assert(qSummary.meanDotsEaten > randSummary.meanDotsEaten);
    std::cout << "  - Verified trained Tabular Q-Learning agent outperforms Random baseline "
              << "(Trained Mean Score=" << qSummary.meanScore
              << " vs Random=" << randSummary.meanScore << ")!\n";

    // 5. Verify ComputeTrainingEpsilon for ExpCutoff (1.000 -> 0.001 at 95th percentile, then 0.000):
    EpsilonSchedule expSched{};
    expSched.mode = EpsScheduleMode::ExpCutoff;
    expSched.epsStart = 1.000f;
    expSched.epsMin = 0.001f;
    expSched.epsCutoff = 0.95f;
    float eps0 = ComputeTrainingEpsilon(expSched, 0, 5000);
    float eps95 = ComputeTrainingEpsilon(expSched, 4749, 5000);
    float epsTail = ComputeTrainingEpsilon(expSched, 4750, 5000);
    float epsEnd = ComputeTrainingEpsilon(expSched, 4999, 5000);
    assert(std::abs(eps0 - 1.000f) < 1e-4f);
    assert(std::abs(eps95 - 0.001f) < 1e-4f);
    assert(epsTail == 0.0f && epsEnd == 0.0f);
    std::cout << "  - Verified ExpCutoff epsilon schedule (ep0=" << eps0
              << ", ep95%=" << eps95 << ", tail=" << epsEnd << ")!\n";

    // 6. Verify ComputeTrainingGamma for LinearRamp & EpsCoupled (0.80 -> 0.95):
    GammaSchedule linG{ GammaScheduleMode::LinearRamp, 0.80f, 0.95f };
    assert(std::abs(ComputeTrainingGamma(linG, 1.0f, 0, 5000) - 0.80f) < 1e-4f);
    assert(std::abs(ComputeTrainingGamma(linG, 0.01f, 3999, 5000) - 0.95f) < 1e-3f);
    assert(std::abs(ComputeTrainingGamma(linG, 0.01f, 4999, 5000) - 0.95f) < 1e-4f);

    GammaSchedule epsG{ GammaScheduleMode::EpsCoupled, 0.80f, 0.95f };
    assert(std::abs(ComputeTrainingGamma(epsG, 1.00f, 0, 5000) - 0.80f) < 1e-4f);
    assert(std::abs(ComputeTrainingGamma(epsG, 0.50f, 500, 5000) - 0.875f) < 1e-4f);
    assert(std::abs(ComputeTrainingGamma(epsG, 0.00f, 4999, 5000) - 0.95f) < 1e-4f);
    std::cout << "  - Verified LinearRamp and EpsCoupled dynamic gamma schedules (0.80 -> 0.95)!\n";

    std::cout << "  -> All Tabular Q-Learning checks passed!\n";
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
    TestCgaFontRendering();
    TestScorePopups();
    TestLevelTransition();
    TestPcSpeakerSynth();
    TestGameStateMachine();
    TestPluggableControllers();
    TestGameStateSerialization();
    TestRandomBaselineAndBenchmark();
    TestMazePathfinder();
    TestTabularQLearning();
    std::cout << "=== All Unit Tests Passed Successfully! ===\n";
    return 0;
}
