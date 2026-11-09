#pragma once
#include "cga_renderer.hpp"
#include "pc_speaker.hpp"
#include "maze.hpp"
#include "entities.hpp"
#include "controllers.hpp"
#include "serialization.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace pcman {

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
    void Update(float dt, const InputState& input);
    void RenderFrame();
    void Render();

    // Controller injection API for Pac-Man and individual Ghosts
    static constexpr int PACMAN_POLL_INTERVAL_TICKS = 3; // 3 ticks @ 18.2065Hz = ~164.8ms (~6.07Hz)

    void SetPacmanController(std::shared_ptr<IPacmanController> controller);
    void SetPacmanController(FnPacmanController::FnWithLegal fn);
    void SetPacmanController(FnPacmanController::FnSimple fn);
    const std::shared_ptr<IPacmanController>& GetPacmanController() const { return m_pacmanController; }
    void SetPacmanPollInterval(int ticks) { m_pacmanPollInterval = (ticks > 0) ? ticks : 1; }
    int GetPacmanPollInterval() const { return m_pacmanPollInterval; }

    void SetGhostController(GhostType type, std::shared_ptr<IGhostController> controller);
    void SetGhostController(GhostType type, FnGhostController::FnWithLegal fn);
    void SetGhostController(GhostType type, FnGhostController::FnSimple fn);
    void SetGhostController(size_t index, std::shared_ptr<IGhostController> controller);
    void SetAllGhostControllers(std::shared_ptr<IGhostController> controller);
    void SetAllGhostControllers(FnGhostController::FnWithLegal fn);
    void SetAllGhostControllers(FnGhostController::FnSimple fn);

    // Game state accessors & simulation for testing
    int GetScore() const { return m_score; }
    int GetLives() const { return m_lives; }
    int GetLevel() const { return m_level; }
    GameState GetState() const { return m_state; }
    void SetState(GameState state) { m_state = state; }
    void SetScore(int score) { m_score = score; }
    bool HasAwardedExtraLife() const { return m_awardedExtraLife; }
    const Pacman& GetPacman() const { return m_pacman; }
    Pacman& GetPacman() { return m_pacman; }
    const Maze& GetMaze() const { return m_maze; }
    Maze& GetMaze() { return m_maze; }
    const std::array<Ghost, 4>& GetGhosts() const { return m_ghosts; }
    const Ghost& GetGhost(GhostType type) const { return m_ghosts[static_cast<size_t>(type)]; }
    Ghost& GetGhost(GhostType type) { return m_ghosts[static_cast<size_t>(type)]; }
    int GetFrightTimer() const { return m_frightTimer; }
    bool IsFruitActive() const { return m_fruitActive; }
    int GetFruitTimer() const { return m_fruitTimer; }
    GameContext MakeContext(LcgRng& rng = GlobalRandom()) const;
    const CgaRenderer& GetRenderer() const { return m_renderer; }
    PcSpeaker& GetSpeaker() { return m_speaker; }
    const PcSpeaker& GetSpeaker() const { return m_speaker; }
    void StartNewGame(int startLevel = 1, uint16_t seed = 0);
    void Tick18Hz();

    // State snapshotting & binary serialization
    GameSnapshot SaveState(const LcgRng& rng = GlobalRandom()) const;
    bool LoadState(const GameSnapshot& snapshot, LcgRng& rng = GlobalRandom());
    std::vector<uint8_t> SerializeState(const LcgRng& rng = GlobalRandom()) const;
    bool DeserializeState(std::span<const uint8_t> data, LcgRng& rng = GlobalRandom());
    bool SaveStateToFile(const std::filesystem::path& path, const LcgRng& rng = GlobalRandom()) const;
    bool LoadStateFromFile(const std::filesystem::path& path, LcgRng& rng = GlobalRandom());

private:
    CgaRenderer m_renderer;
    PcSpeaker m_speaker;
    Maze m_maze;

    Pacman m_pacman;
    std::shared_ptr<IPacmanController> m_pacmanController = MakeHumanPacmanController();
    InputState m_pendingInput{};
    int m_pacmanPollInterval = PACMAN_POLL_INTERVAL_TICKS;
    int m_pacmanPollTimer = 0;
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
    int m_fruitPopupTimer = 0;

    // Ghost eating combo points (200, 400, 800, 1600)
    int m_ghostCombo = 0;

    // Authentic dot eating pause toggle matching DS:0x2CCC at 0x0733 in v1.com (neg byte [0x2ccc])
    int m_dotPauseToggle = 1;

    // Ghost eating freeze state & popup (0x0CE1..0x0D09 in ROM)
    float m_ghostFreezeTimer = 0.0f;
    EntityPosition m_eatenGhostPos{0, 0};

    // Death animation tick counter (34 ticks total at 18.2Hz)
    int m_deathTicks = 0;

    // Extra life awarded flag (granted once at 10,000 points matching 0x0E5D..0x0E72 in v1.com)
    bool m_awardedExtraLife = false;

    // Authentic IBM PC-XT timing constants
    static constexpr float BIOS_TICK_INTERVAL = 1.0f / 18.2065f; // ~54.925ms per tick
    static constexpr float READY_DURATION = 1.63f;               // 4.77MHz CPU busy-wait loops at 0x045A & 0x04AC
    static constexpr float DEATH_DURATION = 34.0f / 18.2065f;   // 17 frames * 2 ticks = 34 ticks (~1.87s)
    // Authentic 7 hardware CGA palette flips at 0x03EC..0x0408 on 4.77MHz 8088 PC-XT:
    // Initial pause ~0.15s (0x03AC), 7 toggles of 0x20/0x10 to Port 0x3D9 at ~0.35s each (total ~2.60s)
    static constexpr float LEVEL_CLEAR_INITIAL_PAUSE = 0.15f;
    static constexpr float LEVEL_CLEAR_FLASH_INTERVAL = 0.35f;
    static constexpr int   LEVEL_CLEAR_FLASH_COUNT = 7;
    static constexpr float LEVEL_CLEAR_DURATION = LEVEL_CLEAR_INITIAL_PAUSE + LEVEL_CLEAR_FLASH_COUNT * LEVEL_CLEAR_FLASH_INTERVAL;
    static constexpr float GAME_OVER_DURATION = 3.00f;
    // 24 arpeggio notes * 806 samples @ 44.1kHz (~0.4386s freeze matching 0x0CE1..0x0D09 in ROM)
    static constexpr float GHOST_FREEZE_DURATION = 24.0f * (806.0f / 44100.0f);
    static constexpr float MAX_TICK_ACCUMULATOR = 0.25f;         // Prevent spiral of death after pauses

    // Authentic scoring and bonus constants matching 0x0676..0x0BF1 in v1.com
    static constexpr int SCORE_PELLET        = 10;
    static constexpr int SCORE_ENERGIZER     = 50;
    static constexpr int SCORE_EXTRA_LIFE    = 10000;
    static constexpr int FRUIT_SPAWN_DOTS_1  = 170; // 0xAA dots remaining
    static constexpr int FRUIT_SPAWN_DOTS_2  = 80;  // 0x50 dots remaining
    static constexpr int FRUIT_ACTIVE_TICKS  = 135; // 0x87 ticks in v1.com (~7.4 seconds)
    static constexpr int FRUIT_POPUP_TICKS   = 60;  // 0x3C ticks in v1.com (~3.3 seconds)

    // State transition timer (in seconds, frame-rate independent)
    float m_stateTimer = 0.0f;

    // Authentic 18.2065Hz DOS BIOS timer accumulator
    float m_tickAccumulator = 0.0f;
    int m_frightTimer = 0;

    static InputState PollInput();

    void StartNewRound();
    void ApplyInput(const InputState& input);

    void UpdateTitleScreen(float dt, const InputState& input);
    void UpdateReady(float dt);
    void UpdatePlaying(float dt);
    void UpdatePacmanDying(float dt);
    void UpdateLevelClear(float dt);
    void UpdateGameOver(const InputState& input);

    void RenderMaze();
    void RenderPacman();
    void RenderGhost(const Ghost& ghost, bool flashWhite);
    void RenderHUD();
    void RenderTitleScreen();
};

} // namespace pcman
