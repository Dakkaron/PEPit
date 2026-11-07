# PEPitCompat — Linux Compatibility Layer for PEPit

PEPit is a handheld physiotherapy game console running on an ESP32-S3 (LilyGo T-HMI board).
PEPitCompat lets you run the **original ESP32 source code unmodified** on Linux for development
and testing, using SDL2 and POSIX shims to replace hardware dependencies.

## Quick Start

```bash
# 1. Install build dependencies (Debian/Ubuntu)
sudo apt install build-essential cmake libsdl2-dev libsdl2-ttf-dev

# 2. Clone the repos
git clone https://github.com/Dakkaron/PEPit.git
cd PEPit

# 3. Set up third-party libraries (no .gitmodules — manual clone required)
cd PEPitCompat/third_party
git clone https://github.com/bblanchon/ArduinoJson.git
git clone https://github.com/luc-github/EspLuaEngine.git EspLuaEngine
git clone https://github.com/Densaugeo/base64_arduino.git base64_arduino
git clone https://github.com/Bodmer/TFT_eSPI.git

# 4. Create the symlink to original source (requires sibling repo)
cd ../..
ln -s ../T-HMI-PEPmonitor/src src

# 5. Build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)

# 6. Run
./build/pepit_compat --scale 2
```

## CLI Options

| Flag | Default | Description |
|---|---|---|
| `-d, --data-dir <path>` | `./data/` | Root directory for SD card files |
| `-s, --scale <N>` | `2` | Window scale factor (1 = 320x240, 2 = 640x480) |
| `-f, --fullscreen` | off | Start in fullscreen mode |
| `-l, --low-pressure-key <key>` | `A` | Key simulating low pressure (~50% target) |
| `-g, --good-pressure-key <key>` | `S` | Key simulating target pressure (~100%) |
| `-k, --shot-key <key>` | `P` | Key that captures a screenshot to `<media-dir>/pepit_shot.raw` (+ `.json` metadata) |
| `-r, --rec-key <key>` | `R` | Key that starts/stops a gameplay video recording (`<media-dir>/pepit_rec_<timestamp>.mp4`); while recording, the window title shows "(recording)" |
| `-m, --media-dir <path>` | `/tmp` | Output directory for screenshots and video recordings (created if missing) |
| `-W, --wip` | off | Use the repo's `WIPSDCardContent/` (git-ignored staging dir for games in progress) as the whole SD content root; found by searching cwd and up to 4 parents. `profiles.ini` and `prefs_*.dat` are seeded from the data dir when missing |
| `-w, --wip-games` | off | Read `/games` from `WIPSDCardContent/games`; prefs, profiles and system files stay in the normal data dir |
| `-h, --help` | — | Show help message |

Keyboard keys use SDL scancode names: `A`, `S`, `LEFT`, `RIGHT`, `UP`, `DOWN`, `SPACE`, `RETURN`, etc.

Press **Escape** or **Ctrl+C** to exit at any time. Press **P** (configurable
via `--shot-key`) to save the current screen to `pepit_shot.raw` in the media
directory (`--media-dir`, default `/tmp`).

Gameplay video recording captures the TFT framebuffer (not the window), so it
also works headless — send `REC` to the control file (see the MCP section in
`shim/touch.cpp`) instead of pressing a key. Frames are piped to `ffmpeg`
(required on `PATH`) and saved as 30 fps H.264 MP4 files in the media
directory. Press the
rec key once to start, once more to stop and save; the window title gets
"(recording)" appended while a recording is active.

## Unit Tests

This project is also the native test harness for the firmware
(`T-HMI-PEPmonitor`): the same shims and original sources are linked into a
Catch2-based test binary (`tests/`), see `T-HMI-PEPmonitor/TESTING.md`.

```bash
cmake -B build
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure   # runs headless via SDL dummy driver
# or directly:
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./build/tests/pepit_tests
```

CI runs this on every push/PR that touches `T-HMI-PEPmonitor/` or `PEPitCompat/`.

## What's Shimmed

| Feature | Implementation |
|---|---|
| Display (TFT_eSPI) | SDL2 window with 320x240 pixel buffer |
| Touch (XPT2046) | SDL mouse events → touch coordinates |
| Pressure sensor (HX711) | Keyboard-driven simulation (configurable keys) |
| Joystick (I2C) | Keyboard-driven (arrow keys/WASD + Space/Enter) |
| Filesystem (SD_MMC) | Full POSIX file I/O with configurable root dir |
| Preferences (NVS) | File-based key-value storage |
| Serial output | Redirected to stdout |

| Feature | Stubbed (no real functionality) |
|---|---|
| WiFi / HTTP | Returns OK but shows disconnected |
| Bluetooth (BLE) | `isConnected()` returns false; simulation mode handles this |
| OTA updates | Stubbed; `end()` returns false to prevent restart |
| Deep sleep / power off | Exits program via `std::exit(0)` |

## Project Structure

```
PEPitCompat/
├── CMakeLists.txt          # Build configuration
├── compat/                 # Header-only shims (intercept ESP32 includes)
│   ├── Arduino.h           # Arduino core API + ESP-IDF stubs
│   ├── TFT_eSPI.h          # Display → SDL2 renderer
│   ├── SD_MMC.h            # SD card → POSIX filesystem
│   └── ...                 # WiFi, BLE, NVS, I2C stubs
├── shim/                   # Implementation source files (.cpp)
│   ├── main.cpp            # Entry point: CLI, SDL init, setup()/loop()
│   ├── display.cpp         # SDL2 window + pixel buffer
│   ├── filesystem.cpp      # POSIX file operations
│   └── ...                 # Touch, sensor, joystick, prefs, etc.
├── src/                    # → symlink to ../T-HMI-PEPmonitor/src/
├── third_party/            # External libraries (manual git clones)
│   ├── ArduinoJson/        # JSON parsing for prefs
│   ├── EspLuaEngine/       # Lua scripting (includes Lua 5.4.7)
│   ├── base64_arduino/     # Base64 encoding for prefs dump
│   └── TFT_eSPI/           # Display library (custom SDL2 processor)
├── data/                   # Default SD card contents for testing
│   ├── profiles.ini        # Test profile definitions
│   ├── systemConfig.ini    # System config (simulation enabled)
│   ├── gfx/                # BMP graphics assets
│   └── games/              # Game directories with configs and scripts
├── PLAN.md                 # Detailed implementation plan and status
└── ARCHITECTURE.md         # Technical architecture reference
```

## Important Notes

- **`src/` is a symlink** to `../T-HMI-PEPmonitor/src/`. You must clone the sibling
  `T-HMI-PEPmonitor` repo in the same parent directory, or adjust the symlink target.
- **No `.gitmodules`** — third-party libraries must be cloned manually (see Quick Start above).
- **TFT_eSPI** requires a custom `Processors/TFT_eSDL.cpp` file provided by this project.
  After cloning Bodmer's upstream repo, copy `third_party/TFT_eSPI/Processors/TFT_eSDL.cpp`
  and `TFT_eSDL.h` from this project into the cloned repo, or use the pre-configured copy.
- The `data/` directory is committed with default config files for immediate testing.

## System Dependencies

| Package | Purpose |
|---|---|
| `build-essential` (gcc, g++, make) | C++17 compiler and build tools |
| `cmake` (≥ 3.16) | Build system |
| `libsdl2-dev` | Display rendering and input handling |
| `libsdl2-ttf-dev` | Text rendering (TrueType fonts) |

On other distributions:
- **Fedora/RHEL**: `gcc-c++ cmake SDL2-devel SDL2_ttf-devel`
- **Arch Linux**: `base-devel cmake sdl2 sdl2_ttf`

## License

See the parent [PEPit](https://github.com/Dakkaron/PEPit) repository for licensing details.
