# PC-Man RL Sandbox

A fast, self-contained C++20 Pac-Man test-bed for learning and experimenting with reinforcement learning and search algorithms.

The game itself is a faithful C++ recreation of the 1982 DOS classic *PC-Man*, but the codebase is structured so simulation is completely decoupled from rendering and audio. That gives you the best of both worlds: you can train and evaluate agents over thousands of games in headless C++ at high speed, save a trained policy to disk, and then load that exact checkpoint into the live Raylib GUI to watch what your agent actually learned.

## Why Use This to Learn RL?

Classic arcade games are great RL benchmarks, yet Python environments with slow image-capture wrappers often turn simple experiments (like testing a reward tweak or exploration schedule) into a long wait. Keeping everything in native C++ changes the feedback loop:

* **Fast headless training & benchmarking:** Run thousands of training and evaluation episodes in seconds on a single CPU core, complete with mean/min/max scores, dots eaten, survival time, and level-clear rates.
* **Visual debugging with saved checkpoints:** Save a Q-table with `--qtable-file` during headless training, then launch the GUI with the same file to see *why* a policy plateaued—whether it gets trapped in corners, oscillates between two pellets, or refuses to chase frightened ghosts.
* **Deterministic, reproducible runs:** Every episode is driven by a seeded PRNG (`--seed`). If an agent fails in an interesting situation, you can reproduce that exact game deterministically or snapshot and restore state (`Game::SaveState()` / `Game::LoadState()`) for tree search and rollouts.
* **Swappable opponents & baselines:** Control Pac-Man and each of the four ghosts independently (`original`, `random`, or your own controller). Compare your agent against the random baseline or mix ghost behaviors from the CLI.

## Quick Start

### Prerequisites
* A C++20 compiler (`clang++` or `g++`)
* [raylib](https://www.raylib.com/) (`brew install raylib` on macOS, `sudo apt install libraylib-dev` on Debian/Ubuntu)

### Build & Test

```bash
make          # builds ./pcman
make test     # builds and runs the unit test suite
```

CMake is also supported (`mkdir build && cd build && cmake .. && cmake --build . && ctest`).

### Play, Train, and Watch

```bash
# 1. Play manually in the GUI
./pcman

# 2. Measure the random Pac-Man baseline over 100 headless games
./pcman --headless --pacman random --episodes 100

# 3. Train the built-in Tabular Q-Learning agent for 50,000 games and save it
./pcman --headless --pacman qtable --train-episodes 50000 --episodes 200 --qtable-file agent.bin

# 4. Watch the trained agent play live in the GUI
./pcman --pacman qtable --qtable-file agent.bin
```

## Exploring RL with the Built-In Q-Learning Agent

Naive tabular Q-learning fails on raw Pac-Man coordinates because the state space is far too large. To make tabular RL practical and easy to study, [`TabularQPacmanController`](src/qlearning.hpp) solves two core RL challenges out of the box:

### 1. Compact Tactical State Representation
Instead of memorizing raw grid coordinates, the agent sees a ~20k-state local radar centered on Pac-Man:
* **Where is the nearest food?** Direction to the closest pellet or energizer using a memoized maze BFS solver ([`src/pathfinder.hpp`](src/pathfinder.hpp)).
* **Which exits are safe right now?** Looking ahead down each legal direction, the agent compares how quickly Pac-Man can reach the next junction against how quickly active ghosts can intercept him there—coding each direction as blocked, safe, caution, or danger.
* **What tactical target matters most?** Direction toward a catchable frightened ghost, nearby energizer, bonus fruit, or best escape corridor when chased.
* **Current heading:** Pac-Man's current direction of travel.

### 2. Credit Assignment & Hyperparameter Experiments
When Pac-Man walks into a dead-end corridor, getting caught happens several decisions later. To assign blame back to the mistake that caused the trap, the agent uses **Watkins's Q(λ)** with eligibility traces (cutting traces on random exploratory moves so earlier good moves aren't penalized for exploration).

Because learning behavior changes as a policy matures, the CLI lets you run controlled experiments without recompiling:

```bash
# Test decaying epsilon down to 0.001 at 95% of training with a pure-greedy tail,
# while ramping gamma from short-sighted (0.80) early on to far-sighted (0.95) later:
./pcman --headless --pacman qtable \
  --train-episodes 100000 --trace-capacity 64 --lambda 0.90 \
  --eps-schedule cutoff --eps-start 1.000 --eps-min 0.001 --eps-cutoff 0.95 \
  --gamma-schedule linear --gamma-start 0.80 --gamma 0.95 \
  --episodes 200 --qtable-file agent.bin
```

Key RL knobs exposed on the CLI (run `./pcman --help` for the full list):
* **Exploration schedules (`--eps-schedule`, `--eps-start`, `--eps-min`, `--eps-cutoff`):** Compare keeping a small exploration floor (`floor`) against cutting exploration to `0.000` for the final stretch of training (`cutoff`).
* **Discount factor horizon (`--gamma`, `--gamma-schedule`, `--gamma-start`):** Keep γ constant (`fixed`), ramp it linearly as the agent survives longer (`linear`), or couple it to `1 - ε` (`eps`).
* **Eligibility traces (`--lambda`, `--trace-capacity`):** Tune how far backward terminal death penalties and rewards propagate along recent moves.
* **Opponent mix (`--ghosts`, `--blinky`, `--pinky`, `--inky`, `--clyde`):** Test policies against the authentic game ghosts (`original`) or `random` ghosts per character.

## Writing Your Own Agents or Search Algorithms

If you want to implement your own policy—whether a heuristic bot, Monte Carlo Tree Search, or an external ML model—implement `IPacmanController` or `IGhostController` in [`src/controllers.hpp`](src/controllers.hpp) (or pass a lambda to `Game::SetPacmanController()`):

```cpp
#include "game.hpp"
#include "pathfinder.hpp"

class NearestDotAgent : public pcman::IPacmanController {
public:
    void OnReset() override { /* called at start of round */ }
    void OnPacmanDied(const pcman::GameContext& ctx) override { /* terminal loss */ }
    void OnLevelCleared(const pcman::GameContext& ctx) override { /* board cleared */ }

    pcman::Direction ChooseDirection(const pcman::Pacman& self,
                                      const pcman::GameContext& ctx,
                                      const pcman::LegalActions& legal) override {
        return pcman::FindNearestDotForPacman(ctx.maze, self, legal).firstAction;
    }
};
```

A few building blocks make custom agents much nicer to write:
* **Action masking (`LegalActions`):** Every decision call hands you the valid directions (including mid-corridor reversals and buffered corner turns for Pac-Man, while preventing illegal 180° turns for ghosts).
* **Maze BFS helpers ([`src/pathfinder.hpp`](src/pathfinder.hpp)):** Cached shortest-path queries handle tunnel wrap-around and respect the asymmetry between Pac-Man (can reverse) and ghosts (cannot reverse outside dead ends).
* **Tree search / rollouts:** Use `game.SaveState()` and `game.LoadState()` to clone game state, simulate candidate futures cheaply, and restore cleanly.

## Game Mechanics Worth Knowing

If you're designing features or reward functions, here are the quirks of *PC-Man* that show up in learned policies:
* **Ghosts can't pull 180° U-turns** in corridors or intersections (only at dead ends), whereas Pac-Man can reverse immediately mid-corridor. Exploiting ghost commitment around loops is a huge part of high-level play.
* **Speed asymmetry:** Eating pellets briefly slows Pac-Man down, frightened ghosts and ghosts in the vertical tunnel move at half speed, and ghosts pause briefly on 90° turns.
* **Scoring:** 226 pellets (10 pts), 4 energizers (50 pts), frightened ghosts (200 → 400 → 800 → 1,600 pts), two fruit spawns per level at 170 and 80 dots remaining (100–5,000 pts), and a bonus life at 10,000 pts. Clearing all 230 dots advances to the next level.

### GUI Controls
Arrow keys, `WASD`, `ESDX`, `IJKL`, or gamepad to move; `Space` or `J` to start; `Ctrl+S` to mute/unmute audio; `Escape` to quit.

## Codebase Map

* [`src/qlearning.hpp`](src/qlearning.hpp) / [`src/qlearning.cpp`](src/qlearning.cpp) — Tactical state encoder, Watkins's Q(λ), reward shaping, checkpoint save/load
* [`src/benchmark.hpp`](src/benchmark.hpp) / [`src/benchmark.cpp`](src/benchmark.cpp) — Training loop, ε/γ schedules, and headless multi-episode evaluator
* [`src/pathfinder.hpp`](src/pathfinder.hpp) / [`src/pathfinder.cpp`](src/pathfinder.cpp) — Memoized maze BFS for directed ghost and reversible Pac-Man queries
* [`src/controllers.hpp`](src/controllers.hpp) / [`src/controllers.cpp`](src/controllers.cpp) — Agent interfaces, legal action masking, baselines, and strategy registry
* [`src/game.hpp`](src/game.hpp) / [`src/game.cpp`](src/game.cpp) — Core game loop, controller polling, lifecycle hooks, and state snapshots
* [`src/entities.hpp`](src/entities.hpp), [`src/maze.hpp`](src/maze.hpp), [`src/serialization.hpp`](src/serialization.hpp) — Maze/entity rules, PRNG, and snapshot serialization
* [`src/cga_renderer.hpp`](src/cga_renderer.hpp), [`src/pc_speaker.hpp`](src/pc_speaker.hpp), [`src/game_data.hpp`](src/game_data.hpp) — Self-contained CGA visuals, PC speaker synth, and embedded assets
* [`tests/test_pcman.cpp`](tests/test_pcman.cpp) — Unit tests covering game rules, pathfinding, serialization, and Q-learning

---

*Built atop a C++20 recreation of Greg Kuperberg's 1982 IBM PC **PC-Man** (Orion Software, Inc.).*
