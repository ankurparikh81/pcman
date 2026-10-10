#pragma once
#include "cga_renderer.hpp"
#include "pc_speaker.hpp"
#include "maze.hpp"
#include "entities.hpp"
#include <array>

namespace pcman {

enum class GameState {
    TitleScreen = 0,
    Ready,
    Playing,
    PacmanDying,
    LevelClear,
    GameOver
};

// Check if two points are within rectangular touch radius (used for fruit touch)
constexpr bool CheckTouchBox(int x1, int y1, int x2, int y2, int radius = 8) {
    int dx = x1 - x2;
    int dy = y1 - y2;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return dx < radius && dy < radius;
}

// Authentic ghost eat combo score (200, 400, 800, 1600 points)
constexpr int GhostComboScore(int combo) {
    if (combo <= 0) return 200;
    int shift = combo - 1;
    if (shift > 3) shift = 3;
    return 200 * (1 << shift);
}

class Game {
public:
    Game();

    void Init();
    void Close();

    void Update();
    void Render();

    // Game state accessors & simulation for testing
    int GetScore() const { return m_score; }
    int GetLives() const { return m_lives; }
    void SetScore(int score) { m_score = score; }
    bool HasAwardedExtraLife() const { return m_awardedExtraLife; }
    void StartNewGame();
    void Tick18Hz();

private:
    CgaRenderer m_renderer;
    PcSpeaker m_speaker;
    Maze m_maze;

    Pacman m_pacman;
    std::array<Ghost, 4> m_ghosts{
        Ghost(GhostType::Blinky),
        Ghost(GhostType::Pinky),
        Ghost(GhostType::Inky),
        Ghost(GhostType::Clyde)
    };

    GameState m_state = GameState::TitleScreen;

    int m_score = 0;
    int m_highScore = 0;
    int m_lives = 3;
    int m_level = 1;

    // Fruit bonus state
    bool m_fruitActive = false;
    int m_fruitTimer = 0;
    int m_fruitScorePopup = 0;
    int m_fruitPopupTimer = 0;

    // Ghost eating combo points (200, 400, 800, 1600)
    int m_ghostCombo = 0;

    // Authentic dot eating pause toggle matching DS:0x2CCC at 0x0733 in v1.com (neg byte [0x2ccc])
    int m_dotPauseToggle = 1;

    // Ghost eating freeze state & popup (0x0CE1..0x0D09 in ROM)
    float m_ghostFreezeTimer = 0.0f;
    EntityPosition m_eatenGhostPos{0, 0};
    int m_eatenGhostScore = 0;

    // Death animation tick counter (34 ticks total at 18.2Hz)
    int m_deathTicks = 0;

    // Extra life awarded flag (granted once at 10,000 points matching 0x0E5D..0x0E72 in v1.com)
    bool m_awardedExtraLife = false;

    // Authentic IBM PC-XT timing constants
    static constexpr float BIOS_TICK_INTERVAL = 1.0f / 18.2065f; // ~54.925ms per tick
    static constexpr float READY_DURATION = 1.63f;               // 4.77MHz CPU busy-wait loops at 0x045A & 0x04AC
    static constexpr float DEATH_DURATION = 34.0f / 18.2065f;   // 17 frames * 2 ticks = 34 ticks (~1.87s)
    static constexpr float LEVEL_CLEAR_DURATION = 1.50f;         // 7 border flash cycles at 0x04EC
    static constexpr float GAME_OVER_DURATION = 3.00f;
    // 24 arpeggio notes * 806 samples @ 44.1kHz (~0.4386s freeze matching 0x0CE1..0x0D09 in ROM)
    static constexpr float GHOST_FREEZE_DURATION = 24.0f * (806.0f / 44100.0f);
    static constexpr float MAX_TICK_ACCUMULATOR = 0.25f;         // Prevent spiral of death after pauses
    static constexpr float LEVEL_CLEAR_FLASH_INTERVAL = 0.12f;   // ~8Hz CGA border flashing

    // Authentic scoring and bonus constants matching 0x0676..0x0BF1 in v1.com
    static constexpr int SCORE_PELLET        = 10;
    static constexpr int SCORE_ENERGIZER     = 50;
    static constexpr int SCORE_EXTRA_LIFE    = 10000;
    static constexpr int FRUIT_SPAWN_DOTS_1  = 170; // 0xAA dots remaining
    static constexpr int FRUIT_SPAWN_DOTS_2  = 80;  // 0x50 dots remaining
    static constexpr int FRUIT_ACTIVE_TICKS  = 135; // 0x87 ticks in v1.com (~7.4 seconds)
    static constexpr int FRUIT_POPUP_TICKS   = 60;  // 0x3C ticks in v1.com (~3.3 seconds)

    // State transition timers (in seconds, frame-rate independent)
    float m_stateTimer = 0.0f;
    float m_flashTimer = 0.0f;
    bool m_flashState = false;

    // Authentic 18.2065Hz DOS BIOS timer accumulator
    float m_tickAccumulator = 0.0f;
    int m_frightTimer = 0;

    void StartNewRound();
    void HandleInput();

    void UpdateTitleScreen();
    void UpdateReady();
    void UpdatePlaying();
    void UpdatePacmanDying();
    void UpdateLevelClear();
    void UpdateGameOver();

    void RenderHUD();
    void RenderTitleScreen();
};

} // namespace pcman
