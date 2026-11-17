#include "game.hpp"
#include "rom_data.hpp"
#include <raylib.h>
#include <algorithm>
#include <cstdio>

namespace pcman {

namespace {
    // Authentic Fruit spawn pixel location (0x2E86 in ROM: gridX=26, gridY=25, X=152, Y=93)
    constexpr int FRUIT_X = 152;
    constexpr int FRUIT_Y = 93;
}

Game::Game() = default;

void Game::Init() {
    m_renderer.Init();
    m_speaker.Init();

    m_state = GameState::TitleScreen;
    m_speaker.PlayTitleTune();
}

void Game::Close() {
    m_speaker.Close();
    m_renderer.Close();
}

void Game::SetPacmanController(std::shared_ptr<IPacmanController> controller) {
    m_pacmanController = controller ? std::move(controller) : MakeHumanPacmanController();
}

void Game::SetPacmanController(FnPacmanController::FnWithLegal fn) {
    SetPacmanController(std::make_shared<FnPacmanController>(std::move(fn)));
}

void Game::SetPacmanController(FnPacmanController::FnSimple fn) {
    SetPacmanController(std::make_shared<FnPacmanController>(std::move(fn)));
}

void Game::SetGhostController(GhostType type, std::shared_ptr<IGhostController> controller) {
    SetGhostController(static_cast<size_t>(type), std::move(controller));
}

void Game::SetGhostController(GhostType type, FnGhostController::FnWithLegal fn) {
    SetGhostController(type, std::make_shared<FnGhostController>(std::move(fn)));
}

void Game::SetGhostController(GhostType type, FnGhostController::FnSimple fn) {
    SetGhostController(type, std::make_shared<FnGhostController>(std::move(fn)));
}

void Game::SetGhostController(size_t index, std::shared_ptr<IGhostController> controller) {
    if (index < m_ghosts.size()) {
        m_ghosts[index].SetController(std::move(controller));
    }
}

void Game::SetAllGhostControllers(std::shared_ptr<IGhostController> controller) {
    for (auto& g : m_ghosts) {
        g.SetController(controller);
    }
}

void Game::SetAllGhostControllers(FnGhostController::FnWithLegal fn) {
    SetAllGhostControllers(std::make_shared<FnGhostController>(std::move(fn)));
}

void Game::SetAllGhostControllers(FnGhostController::FnSimple fn) {
    SetAllGhostControllers(std::make_shared<FnGhostController>(std::move(fn)));
}

GameContext Game::MakeContext(LcgRng& rng) const {
    return GameContext{
        .maze = m_maze,
        .pacman = m_pacman,
        .ghosts = &m_ghosts,
        .levelConfig = RomData::GetLevelConfig(m_level),
        .level = m_level,
        .score = m_score,
        .lives = m_lives,
        .frightTimer = m_frightTimer,
        .fruitActive = m_fruitActive,
        .fruitTimer = m_fruitTimer,
        .input = m_pendingInput,
        .rng = rng
    };
}

GameSnapshot Game::SaveState(const LcgRng& rng) const {
    GameSnapshot snap{};
    snap.maze = m_maze.SaveState();
    snap.pacman = m_pacman.SaveState();
    for (size_t i = 0; i < m_ghosts.size(); ++i) {
        snap.ghosts[i] = m_ghosts[i].SaveState();
    }
    snap.rngSeed = rng.GetSeed();
    snap.state = m_state;
    snap.score = m_score;
    snap.highScore = m_highScore;
    snap.lives = m_lives;
    snap.level = m_level;
    snap.fruitActive = m_fruitActive;
    snap.fruitTimer = m_fruitTimer;
    snap.fruitPopupTimer = m_fruitPopupTimer;
    snap.ghostCombo = m_ghostCombo;
    snap.dotPauseToggle = m_dotPauseToggle;
    snap.ghostFreezeTimer = m_ghostFreezeTimer;
    snap.eatenGhostPosX = m_eatenGhostPos.x;
    snap.eatenGhostPosY = m_eatenGhostPos.y;
    snap.deathTicks = m_deathTicks;
    snap.awardedExtraLife = m_awardedExtraLife;
    snap.stateTimer = m_stateTimer;
    snap.tickAccumulator = m_tickAccumulator;
    snap.frightTimer = m_frightTimer;
    snap.pendingInputDir = m_pendingInput.desiredDir;
    snap.pacmanPollTimer = static_cast<uint8_t>(std::clamp(m_pacmanPollTimer, 0, 255));
    return snap;
}

bool Game::LoadState(const GameSnapshot& snapshot, LcgRng& rng) {
    if (!ValidateSnapshot(snapshot)) return false;
    if (!m_maze.LoadState(snapshot.maze)) return false;

    m_pacman.LoadState(snapshot.pacman);
    for (size_t i = 0; i < m_ghosts.size(); ++i) {
        m_ghosts[i].LoadState(snapshot.ghosts[i]);
    }
    rng.Reset(snapshot.rngSeed);

    m_state = snapshot.state;
    m_score = snapshot.score;
    m_highScore = snapshot.highScore;
    m_lives = snapshot.lives;
    m_level = snapshot.level;
    m_fruitActive = snapshot.fruitActive;
    m_fruitTimer = snapshot.fruitTimer;
    m_fruitPopupTimer = snapshot.fruitPopupTimer;
    m_ghostCombo = snapshot.ghostCombo;
    m_dotPauseToggle = snapshot.dotPauseToggle;
    m_ghostFreezeTimer = snapshot.ghostFreezeTimer;
    m_eatenGhostPos = { snapshot.eatenGhostPosX, snapshot.eatenGhostPosY };
    m_deathTicks = snapshot.deathTicks;
    m_awardedExtraLife = snapshot.awardedExtraLife;
    m_stateTimer = snapshot.stateTimer;
    m_tickAccumulator = snapshot.tickAccumulator;
    m_frightTimer = snapshot.frightTimer;
    m_pendingInput = InputState{};
    m_pendingInput.desiredDir = snapshot.pendingInputDir;
    m_pacmanPollTimer = snapshot.pacmanPollTimer;

    if (m_state != GameState::LevelClear) {
        m_renderer.SetFlashMode(false);
    }
    return true;
}

std::vector<uint8_t> Game::SerializeState(const LcgRng& rng) const {
    return SerializeSnapshot(SaveState(rng));
}

bool Game::DeserializeState(std::span<const uint8_t> data, LcgRng& rng) {
    GameSnapshot snap{};
    if (!DeserializeSnapshot(data, snap)) return false;
    return LoadState(snap, rng);
}

bool Game::SaveStateToFile(const std::filesystem::path& path, const LcgRng& rng) const {
    return SaveSnapshotToFile(SaveState(rng), path);
}

bool Game::LoadStateFromFile(const std::filesystem::path& path, LcgRng& rng) {
    GameSnapshot snap{};
    if (!LoadSnapshotFromFile(path, snap)) return false;
    return LoadState(snap, rng);
}

void Game::StartNewGame(int startLevel, uint16_t seed) {
    ResetGlobalRandom(seed);
    m_score = 0;
    m_lives = 3;
    m_level = std::clamp(startLevel, 1, 12);
    m_awardedExtraLife = false;
    m_maze.Reset();
    StartNewRound();
}

void Game::StartNewRound() {
    m_pacman.Reset();
    if (m_pacmanController) {
        m_pacmanController->OnReset();
    }
    m_pendingInput = InputState{};
    m_pacmanPollTimer = 0;
    for (auto& g : m_ghosts) {
        g.Reset(m_level);
    }
    m_frightTimer = 0;
    m_tickAccumulator = 0.0f;
    m_fruitActive = false;
    m_fruitTimer = 0;
    m_fruitPopupTimer = 0;
    m_ghostCombo = 0;
    m_dotPauseToggle = 1;

    m_state = GameState::Ready;
    m_stateTimer = READY_DURATION; // Authentic 1.63s PC-XT ready delay (0x045A & 0x04AC)
    m_speaker.Silence();
}

InputState Game::PollInput() {
    InputState input{};

    // Audio mute toggle (Ctrl-S)
    if (IsKeyPressed(KEY_S) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        input.muteToggled = true;
    }

    // Start game / skip title tune (Space, 'J', or Gamepad face buttons)
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_J) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
        IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) {
        input.startPressed = true;
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        input.escapePressed = true;
    }

    // Pac-Man movement input supporting Arrow keys, WASD, IJKL, and ESDX diamond:
    // UP:    Up Arrow, 'W', 'E', 'I'
    // DOWN:  Down Arrow, 'S', 'X', 'J'
    // LEFT:  Left Arrow, 'A', 'K'
    // RIGHT: Right Arrow, 'D', 'L'
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || IsKeyDown(KEY_E) || IsKeyDown(KEY_I) ||
        IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_UP)) {
        input.desiredDir = Direction::Up;
    } else if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || IsKeyDown(KEY_X) || IsKeyDown(KEY_J) ||
               IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) {
        input.desiredDir = Direction::Down;
    } else if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) || IsKeyDown(KEY_K) ||
               IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) {
        input.desiredDir = Direction::Left;
    } else if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || IsKeyDown(KEY_L) ||
               IsGamepadButtonDown(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) {
        input.desiredDir = Direction::Right;
    }

    // Left analog stick
    float stickX = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
    float stickY = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y);
    if (std::abs(stickX) > 0.5f || std::abs(stickY) > 0.5f) {
        if (std::abs(stickX) > std::abs(stickY)) {
            input.desiredDir = (stickX > 0) ? Direction::Right : Direction::Left;
        } else {
            input.desiredDir = (stickY > 0) ? Direction::Down : Direction::Up;
        }
    }

    return input;
}

void Game::ApplyInput(const InputState& input) {
    if (input.muteToggled) {
        m_speaker.ToggleMute();
    }

    if (m_state == GameState::Playing && input.desiredDir != Direction::None) {
        m_pendingInput.desiredDir = input.desiredDir;
        m_pacman.SetRequestedDirection(input.desiredDir);
    }
}

void Game::UpdateTitleScreen(float dt, const InputState& input) {
    // Clock title intro tune strictly at 18.2065 Hz BIOS tick frequency
    m_tickAccumulator += dt;
    if (m_tickAccumulator > MAX_TICK_ACCUMULATOR) m_tickAccumulator = MAX_TICK_ACCUMULATOR;
    while (m_tickAccumulator >= BIOS_TICK_INTERVAL) {
        m_tickAccumulator -= BIOS_TICK_INTERVAL;
        m_speaker.Tick18Hz();
    }

    // Press Space or 'J' or Gamepad button to start game
    if (input.startPressed) {
        if (m_speaker.IsTitleTuneDone()) {
            StartNewGame();
        } else {
            // Skip remainder of title tune and immediately reveal developer name & text
            m_speaker.Silence();
        }
    }
}

void Game::UpdateReady(float dt) {
    m_speaker.Silence();
    m_stateTimer -= dt;
    if (m_stateTimer <= 0.0f) {
        m_state = GameState::Playing;
    }
}

void Game::UpdatePlaying(float dt) {
    // Freeze gameplay during frightened ghost eating chime (~0.438s matching 0x0CE1..0x0D09 in ROM)
    if (m_ghostFreezeTimer > 0.0f) {
        m_ghostFreezeTimer -= dt;
        return;
    }

    m_tickAccumulator += dt;
    if (m_tickAccumulator > MAX_TICK_ACCUMULATOR) m_tickAccumulator = MAX_TICK_ACCUMULATOR; // Prevent spiral if paused

    while (m_tickAccumulator >= BIOS_TICK_INTERVAL) {
        m_tickAccumulator -= BIOS_TICK_INTERVAL;
        Tick18Hz();
        if (m_state != GameState::Playing || m_ghostFreezeTimer > 0.0f) break;
    }
}

void Game::Tick18Hz() {
    // 1. Authentic Dot-Eating Check BEFORE moving (0x0676..0x0743 in Pcman v1.com):
    // In Pcman v1.com, when Pac-Man enters a dot cell, he stops and consumes the dot
    // for 1 tick (takes 0 movement steps). This gives the authentic 3-tick-per-dot pace!
    bool atIntersection = IsAtGridIntersection(m_pacman.GetGridY(), m_pacman.GetGridX());
    bool dotEatenThisTick = false;

    if (atIntersection) {
        uint8_t dot = m_maze.CheckAndEatDotAtGrid(m_pacman.GetGridY(), m_pacman.GetGridX());
        if (dot == 2) {
            m_score += SCORE_PELLET;
            // Authentic dot eating pacing from 0x0733 in v1.com: neg byte [0x2ccc]
            // Pac-Man pauses on alternate dots (every 2nd dot), not every single dot!
            m_dotPauseToggle = -m_dotPauseToggle;
            m_speaker.TriggerDotEat(m_dotPauseToggle <= 0);
            if (m_dotPauseToggle <= 0) {
                dotEatenThisTick = true;
            }
        } else if (dot == 3) {
            m_score += SCORE_ENERGIZER;
            m_ghostCombo = 0;
            m_speaker.TriggerEnergizer();
            auto cfg = RomData::GetLevelConfig(m_level);
            m_frightTimer = cfg.frightTime;
            for (auto& g : m_ghosts) {
                g.SetFrightened();
            }
            m_dotPauseToggle = -m_dotPauseToggle;
            if (m_dotPauseToggle <= 0) {
                dotEatenThisTick = true;
            }
        }
    }

    // 2. Poll Pac-Man controller every m_pacmanPollInterval ticks (default 3 ticks = ~6.07Hz / 164.8ms)
    if (m_pacmanPollTimer <= 0) {
        if (m_pacmanController) {
            GameContext pacCtx = MakeContext();
            LegalActions legal = GetPacmanLegalActions(m_maze, m_pacman);
            Direction dir = m_pacmanController->ChooseDirection(m_pacman, pacCtx, legal);
            if (dir != Direction::None) {
                m_pacman.SetRequestedDirection(dir);
            }
        }
        m_pendingInput.desiredDir = Direction::None;
        m_pacmanPollTimer = m_pacmanPollInterval - 1;
    } else {
        m_pacmanPollTimer--;
    }

    // Pac-Man moves ONLY if no dot was eaten this tick (matching 0x073D & 0x0743 skip in v1.com)
    if (!dotEatenThisTick) {
        m_pacman.Update(m_maze);
    }

    if (m_score > m_highScore) {
        m_highScore = m_score;
    }

    // Extra life at 10,000 points (authentic 0x0E5D..0x0E72 in v1.com)
    if (m_score >= SCORE_EXTRA_LIFE && !m_awardedExtraLife) {
        m_lives++;
        m_awardedExtraLife = true;
        m_speaker.TriggerExtraLife();
    }

    // Frightened timer countdown
    if (m_frightTimer > 0) {
        m_frightTimer--;
        if (m_frightTimer == 0) {
            for (auto& g : m_ghosts) {
                g.ClearFrightened();
            }
        }
    }

    // Authentic Pcman v1.com Fruit spawn trigger (0x0BF1..0x0C0B in ROM):
    // Spawns when dots remaining reaches 170 (0xAA) or 80 (0x50)
    int remainingDots = m_maze.GetRemainingDots();
    if (!m_fruitActive && (remainingDots == FRUIT_SPAWN_DOTS_1 || remainingDots == FRUIT_SPAWN_DOTS_2)) {
        m_fruitActive = true;
        m_fruitTimer = FRUIT_ACTIVE_TICKS; // Authentic 0x87 ticks in v1.com (~7.4 seconds)
    }
    if (m_fruitActive) {
        m_fruitTimer--;
        if (m_fruitTimer <= 0) {
            m_fruitActive = false;
        } else {
            // Check Pac-Man touching fruit at authentic CGA address 0x2E86 (pixel X=152, Y=93)
            if (CheckTouchBox(m_pacman.GetPos().x, m_pacman.GetPos().y, FRUIT_X, FRUIT_Y, 8)) {
                int fruitIdx = std::min(m_level - 1, 7);
                int fruitBonus = RomData::GetFruitScore(fruitIdx);
                m_score += fruitBonus;
                m_fruitActive = false;
                m_fruitPopupTimer = FRUIT_POPUP_TICKS; // Authentic 0x3C ticks in v1.com (~3.3 seconds)
                m_speaker.TriggerFruitEat();
            }
        }
    }

    if (m_fruitPopupTimer > 0) {
        m_fruitPopupTimer--;
    }

    // 3. Update Ghosts & check collisions
    int pacY = m_pacman.GetGridY();
    int pacX = m_pacman.GetGridX();
    GameContext ghostCtx = MakeContext();

    // Authentic reverse index update matching 0x07AF (Clyde -> Inky -> Pinky -> Blinky):
    for (int i = static_cast<int>(m_ghosts.size()) - 1; i >= 0; --i) {
        auto& g = m_ghosts[i];
        g.Update(ghostCtx);

        // Authentic Manhattan distance collision check (|dY| + |dX| <= 2)
        if (g.CheckCollision(pacY, pacX)) {
            if (g.IsFrightened()) {
                g.Eat();
                m_ghostCombo++;
                int comboScore = GhostComboScore(m_ghostCombo);
                m_score += comboScore;
                m_eatenGhostPos = g.GetPos();
                m_ghostFreezeTimer = GHOST_FREEZE_DURATION; // ~0.4386s freeze matching 0x0CE1..0x0D09 in ROM
                m_speaker.TriggerGhostEatenSound();
                return;
            } else if (!g.IsEaten()) {
                // Pac-Man hit by ghost!
                if (m_pacmanController) {
                    m_pacmanController->OnPacmanDied(MakeContext());
                }
                m_pacman.StartDying();
                m_state = GameState::PacmanDying;
                m_deathTicks = 0;
                m_speaker.TriggerDeathSound();
                return;
            }
        }
    }

    // Authentic v1.com sound engine tick (matching 0x07F0: call 0xf21)
    // Sound only plays when channels are active (e.g. eating dots). Otherwise silent!
    m_speaker.Tick18Hz();

    // 4. Check Level Clear
    if (m_maze.GetRemainingDots() == 0) {
        if (m_pacmanController) {
            m_pacmanController->OnLevelCleared(MakeContext());
        }
        m_state = GameState::LevelClear;
        m_stateTimer = LEVEL_CLEAR_DURATION; // Authentic ~2.60s sequence at 0x038C..0x040A in v1.com
        m_pacman.SetClosedMouth(true);       // Authentic solid circle mouth frame (0x2DD1 in ROM)
        m_renderer.SetFlashMode(false);
        m_speaker.Silence();
    }
}

void Game::UpdatePacmanDying(float dt) {
    // Advance death melting animation and audio using authentic 18.2Hz clock (34 ticks total)
    m_tickAccumulator += dt;
    if (m_tickAccumulator > MAX_TICK_ACCUMULATOR) m_tickAccumulator = MAX_TICK_ACCUMULATOR;
    while (m_tickAccumulator >= BIOS_TICK_INTERVAL) {
        m_tickAccumulator -= BIOS_TICK_INTERVAL;
        m_pacman.UpdateDeath();
        m_speaker.Tick18Hz();
        m_deathTicks++;
        if (m_deathTicks >= 34) {
            m_lives--;
            if (m_lives > 0) {
                StartNewRound();
            } else {
                m_state = GameState::GameOver;
                m_stateTimer = GAME_OVER_DURATION;
                m_speaker.Silence();
            }
            break;
        }
    }
}

void Game::UpdateLevelClear(float dt) {
    m_stateTimer -= dt;
    if (m_stateTimer <= 0.0f) {
        m_renderer.SetFlashMode(false);
        m_level++;
        if (m_level > 12) m_level = 12;
        m_maze.Reset();
        StartNewRound();
        return;
    }

    float elapsed = LEVEL_CLEAR_DURATION - m_stateTimer;
    if (elapsed < LEVEL_CLEAR_INITIAL_PAUSE) {
        m_renderer.SetFlashMode(false);
    } else {
        float flashTime = elapsed - LEVEL_CLEAR_INITIAL_PAUSE;
        int cycle = static_cast<int>(flashTime / LEVEL_CLEAR_FLASH_INTERVAL);
        if (cycle >= LEVEL_CLEAR_FLASH_COUNT) cycle = LEVEL_CLEAR_FLASH_COUNT - 1;
        // Even cycle (0, 2, 4, 6) = Palette 1 (0x20 in v1.com: Cyan/Magenta/White)
        // Odd cycle (1, 3, 5) = Palette 0 (0x10 in v1.com: Green/Red/Yellow)
        bool flashPalette = (cycle % 2 == 0);
        m_renderer.SetFlashMode(flashPalette);
    }
}

void Game::UpdateGameOver(const InputState& input) {
    if (input.startPressed) {
        StartNewGame();
    } else if (input.escapePressed) {
        m_state = GameState::TitleScreen;
        m_speaker.PlayTitleTune();
    }
}

void Game::Update(float dt, const InputState& input) {
    ApplyInput(input);

    switch (m_state) {
        case GameState::TitleScreen: UpdateTitleScreen(dt, input); break;
        case GameState::Ready:       UpdateReady(dt);              break;
        case GameState::Playing:     UpdatePlaying(dt);            break;
        case GameState::PacmanDying: UpdatePacmanDying(dt);        break;
        case GameState::LevelClear:  UpdateLevelClear(dt);         break;
        case GameState::GameOver:    UpdateGameOver(input);        break;
    }
}

void Game::Update() {
    Update(GetFrameTime(), PollInput());
}

void Game::RenderMaze() {
    // 1. Draw maze wall tiles
    for (int r = 0; r < MAZE_ROWS; ++r) {
        for (int c = 0; c < MAZE_COLS; ++c) {
            uint8_t t = RomData::GetMazeGridTile(c, r);
            if (t != 0) {
                m_renderer.BlitTile4x4(Maze::MAZE_SCREEN_X + c * TILE_WIDTH, Maze::MAZE_SCREEN_Y + r * TILE_HEIGHT, t);
            }
        }
    }

    // 2. Draw remaining dots
    for (int r = 0; r < DOT_ROWS; ++r) {
        for (int c = 0; c < DOT_COLS; ++c) {
            uint8_t d = m_maze.GetDotAt(r, c);
            if (d == 2 || d == 3) {
                m_renderer.BlitDot8x8(Maze::DOT_OFFSET_X + c * 8, Maze::DOT_OFFSET_Y + r * 8, d);
            }
        }
    }
}

void Game::RenderPacman() {
    const auto pos = m_pacman.GetPos();

    if (m_pacman.IsDying()) {
        auto deathSprite = RomData::GetDeathSprite(m_pacman.GetDeathFrame());
        m_renderer.BlitSpriteRaw(pos.x, pos.y, deathSprite.data(), true);
    } else {
        int dirIdx = static_cast<int>(m_pacman.GetDirection());
        if (dirIdx < 0 || dirIdx > 3) dirIdx = 0;
        int frame = m_pacman.GetActiveMouthFrame(); // Frame 2 is solid circle at 0x2DD1 in ROM
        auto pacSprite = RomData::GetPacmanSprite(dirIdx, frame);
        m_renderer.BlitSpriteRaw(pos.x, pos.y, pacSprite.data(), true);
    }
}

void Game::RenderGhost(const Ghost& ghost, bool flashWhite) {
    const auto pos = ghost.GetPos();
    int variation = ghost.GetSpriteVariation(flashWhite);
    int dirIdx = static_cast<int>(ghost.GetDirection());
    if (dirIdx < 0 || dirIdx > 3) dirIdx = 0;

    auto sprite = RomData::GetGhostSprite(variation, dirIdx, ghost.GetFootFrame());
    m_renderer.BlitSpriteRaw(pos.x, pos.y, sprite.data(), true);
}

void Game::RenderHUD() {
    // Authentic Pcman v1.com left margin HUD (0x01DA..0x0209 & 0x0EC4 in ROM):
    // SCORE at row 1 col 1 (X=8, Y=8), digits at X=0, Y=16
    // HIGH SCORE at row 7 col 1 (X=8, Y=56) & row 8 col 1 (X=8, Y=64), digits at X=0, Y=72
    m_renderer.DrawText8x8(8, 8, "SCORE", 3);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%5d", m_score);
    m_renderer.DrawText8x8(0, 16, buf, 3);

    m_renderer.DrawText8x8(8, 56, " HIGH", 3);
    m_renderer.DrawText8x8(8, 64, " SCORE", 3);
    std::snprintf(buf, sizeof(buf), "%5d", m_highScore);
    m_renderer.DrawText8x8(0, 72, buf, 3);

    // Authentic Pcman v1.com Level Fruit Icons (0x023E..0x025E in ROM):
    // Rendered in the left margin at X = 36 (X = 36..51), strictly OUTSIDE the maze boundary (X >= 52).
    // Stacked from bottom up: Y = 186, 170, 154, 138...
    int fruitCount = std::min(m_level, 8);
    for (int i = 0; i < fruitCount; ++i) {
        auto fruitSprite = RomData::GetFruitSprite(i);
        m_renderer.BlitSpriteRaw(36, 186 - i * 16, fruitSprite.data(), true);
    }

    // Authentic Pcman v1.com Extra Lives Icons (0x0260..0x029F in ROM):
    // Rendered in the right margin at X = 300 (X = 300..315), strictly OUTSIDE the maze boundary (X <= 299).
    // Uses authentic Sprite 9 at 0x1F59 in ROM (Pac-Man facing LEFT toward the maze).
    // Stacked from bottom up: Y = 182, 166, 150...
    for (int i = 0; i < m_lives - 1 && i < 4; ++i) {
        m_renderer.BlitSprite16x14(300, 182 - i * 16, 9, true);
    }
}

void Game::RenderTitleScreen() {
    m_renderer.Clear(0);

    // Authentic Pcman v1.com logo letters placement (0x0033..0x005C in ROM):
    // Letters 0..5 at X = {64, 92, 128, 148, 188, 216}, Y = 8
    static constexpr int LOGO_X[6] = { 64, 92, 128, 148, 188, 216 };
    for (int i = 0; i < LOGO_LETTERS; ++i) {
        m_renderer.BlitLogoLetter36x23(LOGO_X[i], 8, i);
    }

    // Authentic Pcman v1.com strings at DS:0x05F8 (row * 8, col * 8, Color 2 = Light Red):
    // Displayed ONLY AFTER title song has finished (or was skipped)!
    if (m_speaker.IsTitleTuneDone()) {
        m_renderer.DrawText8x8(152, 72, "by", 2);
        m_renderer.DrawText8x8(104, 88, "Greg Kuperberg", 2);
        m_renderer.DrawText8x8(24, 128, "PRESS space bar FOR KEYBOARD PLAY", 2);
        m_renderer.DrawText8x8(16, 144, "    OR \"J\"-key FOR JOYSTICK PLAY    ", 2);
        m_renderer.DrawText8x8(48, 192, "(C)1982 ORION SOFTWARE, INC.", 2);
    }
}

void Game::RenderFrame() {
    if (m_state == GameState::TitleScreen) {
        RenderTitleScreen();
    } else {
        m_renderer.Clear(0);
        RenderMaze();
        RenderHUD();

        // Active Fruit Bonus
        if (m_fruitActive) {
            int fruitIdx = std::min(m_level - 1, 7);
            auto fruitSprite = RomData::GetFruitSprite(fruitIdx);
            m_renderer.BlitSpriteRaw(FRUIT_X, FRUIT_Y, fruitSprite.data(), true);
        } else if (m_fruitPopupTimer > 0) {
            // Authentic Pcman v1.com vertical fruit score popup at 0x0DE6 (pixel X = 152..167, Y = 88..107)
            int fruitIdx = std::min(m_level - 1, 7);
            m_renderer.BlitFruitScore16x20(152, 88, fruitIdx, false);
        }

        // Entities
        RenderPacman();

        if (m_state != GameState::PacmanDying && m_state != GameState::LevelClear) {
            bool flashWhite = (m_frightTimer > 0 && m_frightTimer <= 70 && ((m_frightTimer / 8) % 2 == 1));
            for (const auto& g : m_ghosts) {
                // When displaying eaten ghost score during freeze pause, hide the eaten ghost's eyes
                if (m_ghostFreezeTimer > 0.0f && g.IsEaten() &&
                    static_cast<int>(g.GetPos().x) == static_cast<int>(m_eatenGhostPos.x) &&
                    static_cast<int>(g.GetPos().y) == static_cast<int>(m_eatenGhostPos.y)) {
                    continue;
                }
                RenderGhost(g, flashWhite);
            }
        }

        // Frightened Ghost Eaten Score Popup (0x0CE1..0x0D12 in ROM: 16x8 dedicated sprite)
        if (m_ghostFreezeTimer > 0.0f) {
            int comboIdx = std::clamp(m_ghostCombo - 1, 0, 3);
            m_renderer.BlitGhostScore16x8(static_cast<int>(m_eatenGhostPos.x), static_cast<int>(m_eatenGhostPos.y) + 3, comboIdx, true);
        }

        // Authentic Pcman v1.com overlays
        if (m_state == GameState::Ready) {
            // Authentic vertical "READY!" layout at 0x0C07 in ROM (X = 156, Y = 76, color = 2)
            m_renderer.DrawTextVertical8x8(156, 76, "READY!", 2, 8);
        } else if (m_state == GameState::GameOver) {
            // Authentic Pcman v1.com Game Over prompt matching 0x0119 in ROM (row 12 col 3 & row 14 col 2)
            m_renderer.DrawText8x8(24, 96, "PRESS space bar FOR KEYBOARD PLAY", 2);
            m_renderer.DrawText8x8(16, 112, "    OR \"J\"-key FOR JOYSTICK PLAY    ", 2);
        }
    }
}

void Game::Render() {
    RenderFrame();
    m_renderer.Present(GetScreenWidth(), GetScreenHeight());
}

} // namespace pcman
