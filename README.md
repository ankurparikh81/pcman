# PC-Man (1982) — C++20 / Raylib Recreation

[![Play in Browser](https://img.shields.io/badge/Play%20Online-WebAssembly%20%2F%20HTML5-brightgreen?style=for-the-badge&logo=webassembly)](https://ankurparikh81.github.io/pcman/)

A faithful, cycle-accurate, zero-dependency C++20 recreation of the classic 1982 IBM PC DOS game **PC-Man (v1.0)** by Greg Kuperberg (Orion Software), powered by [raylib](https://www.raylib.com/).


Every visual element, audio note, collision rule, entity movement mechanic, and ghost AI behavior was reverse-engineered from the original 16,448-byte `Pcman v1.com` 8086 disassembly and preserved with 100% authenticity.

---

## Features & Historical Authenticity

### 1. 100% Standalone Native C++ Assets (`constexpr`)
* **Zero External Data Files**: All graphics, maze layouts, sprites, letter bitmaps, and sound tables have been reverse-engineered from the original binary and embedded directly as typed C++20 `constexpr` structures in [`src/game_data.hpp`](file:///Users/ankur/src/pcman/src/game_data.hpp).
* The resulting executable is completely self-contained—no ROM files, asset directories, or disk loading required.

### 2. Authentic IBM PC CGA Mode 4 Visuals
* **Palette 0 High-Intensity**: Faithful 4-color raster graphics (Black, Light Green, Bright Red, Yellow) matching early IBM PC Color Graphics Adapters.
* **4:3 CRT Geometry**: 320×200 discrete raster geometry scaled with aspect-ratio preservation (letterboxed / pillarboxed on any window size and resolution).
* **Level Clear Border Flashing**: Authentic ~8 Hz CGA border flash sequence upon clearing all dots.
* **Pixel-Accurate Sprites**: All 16 directional Pac-Man frames, 17-frame death melting sequence, 56-byte pre-composed ghost sprites (with walking feet animation), 8 level fruits, and the original 6-letter 36×23 "PC-MAN" title logo.

### 3. Authentic IBM PC Speaker 1-Bit Sound Engine
* **Real-Time Square Wave Synthesis**: Directly emulates the Intel 8253 Programmable Interval Timer (PIT) channel 2 driving the PC speaker at 44.1 kHz.
* **Original Pit Divisor Tables**: Verbatim pitch divisor frequencies extracted from the original DOS binary:
  * Title screen intro song (137 notes)
  * Siren sound generator (8 frequencies)
  * Dot eating alternate-pitch chomp
  * Energizer sound & fruit bonus chime
  * Frightened ghost eaten ascending arpeggio (24 notes)
  * Pac-Man death sound (34 notes)
  * Extra life fanfare at 10,000 points

### 4. True 18.2065 Hz DOS BIOS Timer Cadence
* **Accurate Tick Pacing**: Gameplay mechanics are tied to the authentic IBM PC BIOS timer frequency ($18.2065\text{ Hz} \approx 54.925\text{ ms per tick}$) independent of video refresh rate.
* **Dot-Eating Pacing (`neg byte [0x2ccc]`)**: Pac-Man pauses for 1 tick on alternating dots, faithfully recreating the original 3-tick-per-dot cadence.
* **Death Melting Pacing**: Authentic 34-tick (~1.87s) death sequence.
* **Freeze Frame on Ghost Eat**: Authentic ~0.44s gameplay freeze when Pac-Man consumes a frightened ghost while the eaten score popup is displayed.

### 5. Verbatim 8086 Ghost AI & PRNG Engine
* **16-bit LCG Random Generator**: Matches the original pseudo-random formula at `0x0B01`:
  $$\text{seed}_{n+1} = (\text{seed}_n \times \text{0x98DD} + \text{0xD5EF}) \pmod{65536}$$
* **Roulette Wheel Decision Making**: Ghosts choose movement at intersections using weighted probability tables based on target Pac-Man direction, mode (Chase, Frightened, or Eaten Eyes), inertia (2× weight on current direction), and level difficulty thresholds.
* **Reverse Processing Order**: Ghosts update in reverse array order (Clyde → Inky → Pinky → Blinky) matching the original `0x07AF` loop.
* **Authentic Pen Release**: Ghosts exit the center pen according to authentic level-based dot thresholds (`penThresh`).

### 6. Lane Centering & Corner Queuing
* **Exact Lane Centering**: Starting grid position `(25, 14)` derives pixel position `(104, 93)`, perfectly centering Pac-Man's 16-pixel sprite inside the 16-pixel corridors with 0.0 pixels of slack.
* **Intersection Turn Buffering**: Buffered turns are held until intersections are reached, allowing smooth continuous movement without getting stuck on corridor walls.
* **Immediate 180° Reversal**: Mid-corridor direction reversals take effect instantly without waiting for an intersection.

---

## Controls

The game supports multiple simultaneous control schemes:

| Action | Primary Keys | Alternate Keys | Classic ESDX | Gamepad / Joystick |
| :--- | :--- | :--- | :--- | :--- |
| **Move Up** | **Up Arrow** | **W** | **E** | **D-Pad Up / Left Stick Up** |
| **Move Down** | **Down Arrow** | **S** | **X** | **D-Pad Down / Left Stick Down** |
| **Move Left** | **Left Arrow** | **A** | **S** | **D-Pad Left / Left Stick Left** |
| **Move Right** | **Right Arrow** | **D** | **D** | **D-Pad Right / Left Stick Right** |
| **Start Game** | **Space** | **J** | — | **A / B / Start** |
| **Toggle Sound** | **Ctrl + S** | — | — | — |
| **Quit to Title / Exit** | **Escape** | — | — | — |

---

## Project Structure

```
pcman/
├── src/
│   ├── main.cpp            # Entry point & window lifecycle (Native + WebAssembly)
│   ├── game.hpp / .cpp     # Game loop, states, timing, collision & HUD
│   ├── game_data.hpp       # Reverse-engineered typed constexpr ROM tables
│   ├── rom_data.hpp        # ROM accessors & LevelConfig structures
│   ├── maze.hpp / .cpp     # 30-stride authentic collision map & dot logic
│   ├── entities.hpp / .cpp # Pac-Man & Ghost AI state machines
│   ├── cga_renderer.hpp    # CGA Mode 4 software rasterizer & CRT presentation
│   ├── pc_speaker.hpp      # 1-bit PC speaker square wave audio synthesizer
│   └── font_data.inl       # IBM PC BIOS 8x8 font bitmap data
├── tests/
│   └── test_pcman.cpp      # Automated unit test suite (8 test suites)
├── web/
│   └── shell.html          # Custom HTML5/WebGL shell template for WebAssembly
├── Makefile                # Cross-platform Makefile (macOS, Linux, MinGW, WebAssembly)
├── CMakeLists.txt          # Modern CMake build configuration (Native + Emscripten)
└── README.md               # Project documentation
```

---

## Building and Running

### Prerequisites

* A **C++20** compatible compiler:
  * **macOS**: Apple Clang (Xcode Command Line Tools: `xcode-select --install`)
  * **Linux**: GCC 11+ or Clang 13+
  * **Windows**: MinGW-w64 (GCC 11+) or MSVC 2019+
* **raylib** (v4.0 or newer):
  * **macOS**: `brew install raylib`
  * **Ubuntu/Debian**: `sudo apt install libraylib-dev`
  * **Arch Linux**: `sudo pacman -S raylib`
  * **Fedora**: `sudo dnf install raylib-devel`
* *(Optional for WebAssembly)*: **[Emscripten SDK](https://emscripten.org/)** (`emsdk`)

---

### Using `make` (Recommended for macOS & Linux)

Build the standalone binary:
```bash
make
```

Run the game:
```bash
./pcman
```

Run the automated test suite:
```bash
make test
```

Clean build artifacts:
```bash
make clean
```

---

### Building for WebAssembly (HTML5 / WebGL)

Compile into static WebAssembly/HTML5 artifacts (`index.html`, `index.js`, `index.wasm`):

```bash
# Using Makefile
make web

# Run a local web server to test
python3 -m http.server 8080 --directory build-web
```
Then navigate to `http://localhost:8080` in your web browser.

Alternatively, using CMake with Emscripten:
```bash
emcmake cmake -B build-web -S .
cmake --build build-web
```

---

### Using `cmake` (Native)

```bash
mkdir build && cd build
cmake ..
cmake --build .
./pcman
```


---

## Automated Test Suite

The test suite in [`tests/test_pcman.cpp`](file:///Users/ankur/src/pcman/tests/test_pcman.cpp) validates 8 critical subsystems against the original binary disassembly:

1. **ROM Data Integrity**: Verifies maze tiles, pellet counts (226 dots + 4 energizers = 230), sprite dimensions, and PIT audio tables.
2. **Maze Collision & Dot Eating**: Tests grid navigation, pellet clearance, and energizer consumption.
3. **Pac-Man Lane Centering & Cornering**: Tests exact coordinate math, wall collision avoidance, and instant 180° reversals.
4. **Ghost AI & Manhattan Collision**: Verifies starting coordinates, in-pen release thresholds, target roulette selection, and $|dY| + |dX| \le 2$ collision detection.
5. **IBM PC-XT Pacing & Timing**: Verifies 18.2065 Hz tick pacing, frightened half-speed movement, and dot consumption pauses.
6. **Vertical Tunnel Wrap**: Tests top-to-bottom and bottom-to-top tunnel wrapping and ghost half-speed tunnel navigation.
7. **Death Melting Sequence**: Verifies the exact 17-frame / 34-tick death animation progression.
8. **Extra Life Scoring**: Verifies single extra life award trigger at 10,000 points and round reset behavior.

Run tests anytime with:
```bash
make test
```

---

## Scoring & Rules

* **Pellet (Dot)**: 10 points
* **Energizer**: 50 points
* **Frightened Ghosts**: 200 $\rightarrow$ 400 $\rightarrow$ 800 $\rightarrow$ 1,600 points (per energizer period)
* **Fruit Bonuses**:
  * Cherry: 100 points
  * Strawberry: 300 points
  * Peach: 500 points
  * Apple: 700 points
  * Grapes: 1,000 points
  * Galaxian: 2,000 points
  * Bell: 3,000 points
  * Key: 5,000 points
* **Fruit Spawns**: Appear below the ghost pen when remaining dots reach 170 (0xAA) and 80 (0x50), remaining active for ~7.4 seconds.
* **Extra Life**: Awarded once upon reaching **10,000 points**.

---

## Credits & Historical Context

* **Original Game**: *PC-Man* (1982) by **Greg Kuperberg**, published by **Orion Software, Inc.**
* **Port / Recreation**: Modern C++20 re-implementation reverse-engineered from the original MS-DOS `.COM` executable.
