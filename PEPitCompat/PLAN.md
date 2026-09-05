# PEPitCompat — Linux Compatibility Layer for PEPit

## Overview

PEPit is a handheld physiotherapy game console running on ESP32-S3 (LilyGo T-HMI board).
PEPitCompat is a Linux compatibility layer that allows the original ESP32 source code to run
unmodified on Linux for development and testing purposes.

## Goal

Run the original `T-HMI-PEPmonitor/src/` code on Linux **without any modifications** to the
original source files. All ESP32-specific dependencies are intercepted via shim headers in
`compat/`, with implementations in `shim/`.

## Directory Structure

```
PEPitCompat/
├── PLAN.md                     # This file — master implementation plan and status
├── ARCHITECTURE.md             # Technical architecture and design decisions
├── API_MAP.md                  # ESP32-to-Linux API mapping reference
├── CMakeLists.txt              # Build configuration (NOT YET CREATED)
├── compat/                     # Header-only shims (intercept angle-bracket includes)
│   ├── Arduino.h               # ✅ DONE — Arduino core API + ESP-IDF stubs + global instance declarations
│   ├── SD_MMC.h                # ✅ DONE — SD card filesystem -> POSIX (configurable root dir)
│   ├── FS.h                    # ✅ DONE — File class + fs::File namespace shim (fixed: added <cstdio>, <dirent.h>)
│   ├── SPI.h                   # ❌ NOT CREATED — empty stub (SPI not used on Linux)
│   ├── SD.h                    # ❌ NOT CREATED — empty stub
│   ├── TFT_eSPI.h              # ✅ DONE — Display -> SDL2 renderer (320x240, scalable)
│   ├── Preferences.h           # ✅ DONE — NVS flash storage -> JSON file on disk (fixed: added <map>)
│   ├── WiFi.h                  # ❌ NOT CREATED — WiFi stack -> simulated connection states
│   ├── HTTPClient.h            # ❌ NOT CREATED — HTTP client -> stub (returns errors)
│   ├── Update.h                # ❌ NOT CREATED — OTA firmware update -> stub (logs skip message)
│   ├── OneButton.h             # ❌ NOT CREATED — button debouncing -> no-op stub
│   ├── esp_log.h               # ❌ NOT CREATED — ESP-IDF logging -> printf-based colored output
│   ├── esp_rom_crc.h           # ❌ NOT CREATED — CRC32 calculation (needed by serialHandler)
│   ├── nvs_flash.h             # ❌ NOT CREATED — NVS flash -> stub (prefs use JSON instead)
│   ├── nvs.h                   # ❌ NOT CREATED — NVS API -> stub
│   ├── Wire.h                  # ❌ NOT CREATED — I2C bus (needed by joystickHandler)
│   ├── m5_unit_joystick2.hpp   # ❌ NOT CREATED — I2C joystick (stubbed, returns not present)
│   ├── Adafruit_HX711.h        # ❌ NOT CREATED — pressure sensor (replaced by keyboard)
│   ├── ESP32-targz.h           # ❌ NOT CREATED — firmware update extraction (stubbed)
│   ├── NuSerial.hpp            # ❌ NOT CREATED — Bluetooth UART stub
│   └── driver/rtc_io.h         # ❌ NOT CREATED — RTC GPIO -> stub (powerHandler uses this)
├── shim/                       # Implementation source files (.cpp)
│   ├── main.cpp                # ❌ NOT CREATED — Entry point: CLI parsing, SDL init, setup()/loop()
│   ├── display.cpp             # ✅ DONE — SDL2 window + pixel buffer for TFT_eSPI/TFT_eSprite (fixed: constructor syntax)
│   ├── touch.cpp               # ❌ NOT CREATED — SDL mouse events -> XPT2046 touch API
│   ├── filesystem.cpp          # ✅ DONE — POSIX file operations for SD_MMC shim
│   ├── prefs.cpp               # ✅ DONE — JSON-based key-value preferences storage
│   ├── sensor.cpp              # ❌ NOT CREATED — Keyboard-driven pressure sensor simulation
│   ├── bluetooth.cpp           # ❌ NOT CREATED — Bluetooth stub (simulation mode passthrough)
│   ├── arduino.cpp             # ✅ DONE — Time, Serial, String, map(), ESP-IDF stubs, getLocalTime()
│   └── thread.cpp              # ✅ DONE — FreeRTOS -> std::thread wrappers
├── src/                        # ✅ SYMLINK -> ../T-HMI-PEPmonitor/src/
├── third_party/                # ✅ Git submodules for external libraries (all cloned)
    │   ├── ArduinoJson/            # ✅ bblanchon/ArduinoJson (master, v7.x)
    │   ├── EspLuaEngine/           # ✅ luc-github/EspLuaEngine (includes bundled Lua 5.4.7)
    │   └── base64_arduino/         # ✅ Densaugeo/base64_arduino
└── data/                       # ✅ Default data directory (created with defaults)
    ├── gfx/                    # BMP graphics assets from SD card
    ├── games/                  # Game directories with gameconfig.ini, scripts, etc.
    ├── profiles.ini            # ✅ Profile definitions (1 test profile)
    └── systemConfig.ini        # ✅ System config (simulateTrampoline=true, simulateBlowing=true)
```

## CLI Parameters

| Flag | Default | Description |
|---|---|---|
| `--scale <1\|2>` | `1` | Display window scale factor (320x240 or 640x480) |
| `--data-dir <path>` | `./data/` | Root directory for SD card files (replaces /sdcard mount) |
| `--low-pressure-key <key>` | `A` | Key that simulates "too low" pressure (~50% of target) |
| `--good-pressure-key <key>` | `S` | Key that simulates "good/target" pressure (~100% of target) |

No key pressed = 0 pressure. Keys are SDL scancodes or single characters.

## Build & Run

```bash
cd PEPitCompat
git submodule update --init --recursive   # Pull ArduinoJson, EspLuaEngine, base64
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
./build/pepit_compat --scale 2 --data-dir /path/to/sdcard/contents
```

## Current Status (as of 2025-06-12 → BUILDING)

### Completed
- `compat/Arduino.h` — Full Arduino core API (String, Serial, GPIO stubs, time functions)
  - Added: `map()` template, `getLocalTime()`, `configTime()`, ESP-IDF reset/sleep/gpio stubs
  - Added: `ESP` class (restart, getFreeHeap, getFreePsram)
  - Added: `Serial.readBytes()`, `Serial.setTimeout()`
  - Added: String `startsWith()`, `endsWith()`, `operator+=(char)`
  - Added: String `operator==(const char*)` / `operator!=(const char*)` to fix ambiguous overload errors
  - Added: String `equalsIgnoreCase(const String&)` / `equalsIgnoreCase(const char*)` (uses strcasecmp)
  - Added: `heap_caps_realloc()` for Lua PSRAM allocator (used by gameLua.cpp)
  - Added: `<strings.h>` include for strcasecmp
  - Includes `esp_log.h` so EspLuaEngine gets `log_e`/`log_v` macros
- `compat/FS.h` — File class header (fixed: added `<cstdio>`, `<dirent.h>` includes)
- `compat/SD_MMC.h` — SD card namespace header (complete)
- `compat/TFT_eSPI.h` — Display driver shim header with color/datum constants (complete)
  - Added: `pushToSprite` overloads (3-arg no-mask, region-based with/without mask)
  - Added: `fillCircle()`, `drawFastVLine()` declarations to TFT_eSprite
  - Added: `fontHeight()` to both TFT_eSPI and TFT_eSprite
- `compat/Preferences.h` — Key-value storage header (fixed: added `<map>` include)
  - Added: `getString(key, default)` returning String (2-arg overload)
  - Added: `getFloat()` / `putFloat()` methods
- `compat/WiFi.h` — WiFi stubs (fixed: added WIFI_STA, correct connection states)
- `compat/HTTPClient.h` — HTTP stubs (fixed: added HTTPC_STRICT_FOLLOW_REDIRECTS, WiFiClient::readBytes)
- `compat/Update.h` — OTA stubs (fixed: added U_FLASH, writeStream; end() returns false to prevent restart)
- `compat/OneButton.h` — Button debouncing no-op stub (fixed: accepts nullptr callbacks)
- `compat/esp_log.h` — ESP-IDF logging → printf-based colored output + shorthand macros (`log_e`, `log_v`)
- `compat/esp_rom_crc.h` — CRC32 calculation (full implementation for serialHandler)
- `compat/nvs_flash.h` — NVS flash stubs (all no-op, return OK)
- `compat/nvs.h` — NVS API stubs (fixed: correct signatures, added NVS_DEFAULT_PART_NAME, NVS_TYPE_ANY)
- `compat/SPI.h` — SPI bus stub (no-op class + global instance)
- `compat/SD.h` — Empty stub for include compatibility
- `compat/Wire.h` — I2C bus stub (created, no-op class + global instance)
- `compat/NuSerial.hpp` — Bluetooth UART stub (isConnected returns false)
- `compat/NimBLEDevice.h` — BLE device stubs (fixed: added CONFIG_BT_ENABLED/BLUEDROID_ENABLED defines)
- `compat/esp32-hal.h` — ESP32 HAL stub (empty, for include compat)
- `compat/driver/rtc_io.h` — RTC GPIO stubs (all no-op, return OK)
- `compat/ESP32-targz.h` — Firmware update extraction stubs (non-functional, all return OK)
- `compat/m5_unit_joystick2.hpp` — I2C joystick mocked with keyboard input (arrow keys/WASD + Space/Enter)
  - Added: `JOYSTICK2_ADDR` constant (0x3E) for joystickHandler compatibility
- `compat/Adafruit_HX711.h` — Pressure sensor mocked with keyboard input (configurable keys)
- `shim/arduino.cpp` — Time, Serial, String implementations + ESP-IDF stubs + configTime
- `shim/display.cpp` — SDL2 display/sprite implementation (fixed: constructor syntax, lambda capture in fillTriangle)
  - Implemented: `pushToSprite` (3-arg, region overloads), `fillCircle`, `drawFastVLine`, `fontHeight`
  - Implemented: cursor getters (`getCursorX`/`getCursorY`), `drawString` with font param
- `shim/filesystem.cpp` — POSIX file I/O implementation (complete)
- `shim/prefs.cpp` — Key-value preferences with base64 encoding (complete)
  - Added: `getString(key, default)` returning String, `getFloat()`/`putFloat()`
- `shim/thread.cpp` — FreeRTOS-to-std::thread mapping (complete)
- `shim/instances.cpp` — Global instance definitions + gfxHandler wrapper stubs (`drawButton`, `drawImageButton`)
- `shim/touch.cpp` — SDL mouse → XPT2046 touch API (replaces original xpt2046.cpp)
- `shim/sensor.cpp` — Keyboard-driven pressure sensor simulation (A/S keys by default)
- `shim/joystick.cpp` — Keyboard-driven joystick simulation (arrow keys/WASD + Space/Enter)
- `shim/main.cpp` — ✅ DONE — CLI parsing, SDL2 init, setup()/loop() entry point
- `CMakeLists.txt` — ✅ DONE — Build system with SDL2, Lua, include paths, compile definitions (`LUA_USE_C89`, `VERBOSE`)
- `src/` → ✅ SYMLINK to `../T-HMI-PEPmonitor/src/`
- `data/profiles.ini` — ✅ DONE (1 test profile with pepShort task)
- `data/systemConfig.ini` — ✅ DONE (simulateTrampoline=true, simulateBlowing=true)

### Remaining Tasks
1. ~~**Create `shim/main.cpp`~~ — ✅ DONE (CLI parsing, SDL init, setup()/loop() entry point)
2. ~~**Create `CMakeLists.txt`~~ — ✅ DONE (SDL2, Lua, include paths, compile definitions)
3. ~~**Create `src/` symlink~~ — ✅ DONE (points to `../T-HMI-PEPmonitor/src/`)
4. ~~**Create `data/` directory~~ — ✅ DONE (gfx/, games/, profiles.ini, systemConfig.ini)
5. ~~**Add git submodules**~~ — ✅ DONE (ArduinoJson, EspLuaEngine, base64_arduino)
6. ~~**Build and test compilation**~~ — ✅ DONE (clean build, zero errors)

## Implementation Phases (in order)

### Phase 1: Core Infrastructure ✅ DONE
- [x] Create `compat/Arduino.h` — Arduino core API shim (ESP-IDF stubs, ESP class, String, Serial)
- [x] Create `shim/arduino.cpp` — Time, Serial, String implementations (map(), getLocalTime(), configTime)
- [x] Create `shim/thread.cpp` — FreeRTOS -> std::thread wrappers

### Phase 2: Filesystem ✅ DONE
- [x] Create `compat/FS.h` + fix includes (`<cstdio>`, `<dirent.h>`)
- [x] Create `compat/SD_MMC.h` — SD card namespace wrapping filesystem shim
- [x] Create `shim/filesystem.cpp` — POSIX file operations

### Phase 3: Display & Input ✅ DONE
- [x] Create `compat/TFT_eSPI.h` + `shim/display.cpp` — SDL2 display renderer (fixed constructor)
- [x] Create `shim/touch.cpp` — SDL mouse → XPT2046 touch API (replaces original xpt2046.cpp)
- [x] Create `compat/Adafruit_HX711.h` + `shim/sensor.cpp` — keyboard-driven pressure sensor
- [x] Create `compat/m5_unit_joystick2.hpp` + `shim/joystick.cpp` — keyboard-driven joystick

### Phase 4: Storage & Network ✅ DONE
- [x] Create `compat/Preferences.h` + `shim/prefs.cpp` — key-value store (key:type:value format)
- [x] Create `compat/WiFi.h`, `HTTPClient.h` — stubbed network (all return OK)
- [x] Create `compat/Update.h` — OTA stub (end() returns false to prevent ESP.restart())

### Phase 5: ESP-IDF & Library Stubs ✅ DONE
- [x] Create `compat/esp_log.h` — colored printf-based logging
- [x] Create `compat/esp_rom_crc.h` — CRC32 implementation (functional)
- [x] Create `compat/nvs_flash.h`, `nvs.h` — NVS stubs (correct signatures, all return OK)
- [x] Create `compat/SPI.h`, `SD.h`, `Wire.h` — bus stubs (no-op classes)
- [x] Create `compat/NuSerial.hpp`, `NimBLEDevice.h` — Bluetooth stubs (CONFIG defines included)
- [x] Create `compat/OneButton.h` — button debouncing no-op (accepts nullptr)
- [x] Create `compat/esp32-hal.h`, `driver/rtc_io.h` — HAL/RTC GPIO stubs
- [x] Create `compat/ESP32-targz.h` — firmware extraction stubs (non-functional, all OK)
- [x] Create `shim/instances.cpp` — global instance definitions + stub function implementations

### Phase 6: Build System & Integration ✅ DONE
- [x] Create `CMakeLists.txt` — standard CMake, finds SDL2 + Lua from EspLuaEngine
- [x] Create `shim/main.cpp` — CLI parsing, SDL init, setup()/loop() entry point
- [x] Create symlinks: `src/ -> ../T-HMI-PEPmonitor/src/`
- [x] Create `data/` directory with default profiles.ini and systemConfig.ini
- [x] Add git submodules: ArduinoJson, EspLuaEngine, base64_arduino

### Phase 7: Compilation Fixes ✅ DONE
- [x] Fix `String::operator==`/`!=` ambiguity — added `const char*` overloads
- [x] Fix `String::equalsIgnoreCase` — missing method, added both overloads
- [x] Fix `TFT_eSprite::pushToSprite` — added 3-arg + region overloads
- [x] Fix `TFT_eSprite::fontHeight` and `fillCircle` — missing methods
- [x] Fix `Preferences::getString(key, default)` — added 2-arg String return overload
- [x] Fix `Preferences::getFloat`/`putFloat` — missing methods
- [x] Fix `heap_caps_realloc` — missing from Arduino.h (needed by gameLua.cpp)
- [x] Fix `JOYSTICK2_ADDR` — missing constant in m5_unit_joystick2.hpp
- [x] Fix `log_e`/`log_v` — EspLuaEngine logging macros in esp_log.h
- [x] Fix `TFT_eSprite::fillTriangle` — missing implementation + lambda capture bug
- [x] Fix `drawButton`/`drawImageButton` 7-arg wrappers — header/definition mismatch
- [x] Clean build verified — zero compilation/linker errors

### Phase 8: Runtime Testing
- [ ] Test display output, keyboard input, file I/O

## Include Path Strategy (CMake)

Include paths are ordered by priority. Higher priority = checked first:

1. `compat/` — shims intercept `<Arduino.h>`, `<SD_MMC.h>`, `<TFT_eSPI.h>`, etc.
2. `shim/` — shim implementation headers (internal use)
3. `third_party/ArduinoJson/src/` — real ArduinoJson library
4. `third_party/EspLuaEngine/` — real EspLuaEngine + bundled Lua 5.4
5. `third_party/base64_arduino/` — real base64 library
6. `src/` — original source code (should compile without modification)

## What Works on Linux vs. What's Stubbed

### Fully Implemented
- **Display**: SDL2 window with 320x240 pixel buffer, all TFT_eSPI/TFT_eSprite methods
- **Filesystem**: Full POSIX file I/O with configurable root directory
- **Touch input**: SDL mouse events mapped to touch coordinates (left click = press)
- **Pressure sensor**: Keyboard-driven simulation ('A' = ~50%, 'S' = ~100% of target)
- **Joystick**: Keyboard-driven simulation (arrow keys/WASD = axis, Space/Enter = button)
- **Preferences**: File-based persistent key-value storage (key:type:value format)
- **Serial output**: Redirected to stdout with proper formatting

### Stubbed (No Real Functionality, All Return OK)
- **WiFi**: No WiFi available; begin() returns OK but status shows disconnected
- **HTTP/OTA**: GET returns 200 OK with empty stream; Update.end() returns false (prevents restart)
- **NTP time**: POSIX localtime_r (works, but no real NTP sync)
- **Bluetooth/Trampoline**: isConnected() returns false; simulation mode in original code handles this
- **Physical buttons (OneButton)**: No-op stubs (accept nullptr callbacks)
- **Deep sleep / power off**: Exits the program gracefully via std::exit(0)
- **RTC GPIO**: No-op stubs, all return OK
- **I2C (Wire)**: No-op stubs for joystick handler (joystick uses keyboard instead)
- **NVS flash**: No-op stubs (prefs use file-based storage instead)

## Original Code Analysis Summary

The original codebase has these key files that our shims must support:

- `src/main.cpp` — Entry point with setup()/loop(), uses Arduino.h, SD_MMC, TFT_eSPI,
  OneButton, esp_log, and all hardware handlers
- `src/constants.h` — Platform-independent constants (screen dimensions, task types, etc.)
- `src/pins.h` — GPIO pin definitions (stubbed on Linux)
- `src/systemconfig.cpp/h` — Config loaded from SD card INI file
- `src/physioProtocolHandler.cpp/h` — Main game logic (profile selection, game running)
- `src/systemStateHandler.cpp/h` — State machine management
- `src/updateHandler.cpp/h` — OTA updates (stubbed on Linux)
- `src/hardware/gfxHandler.cpp/hpp` — Heavy use of TFT_eSPI, TFT_eSprite, SD_MMC
- `src/hardware/touchHandler.cpp` — XPT2046 touch controller (replaced by SDL mouse)
- `src/hardware/sdHandler.cpp` — SD card I/O, INI parsing (uses SD_MMC)
- `src/hardware/pressuresensor.cpp` — HX711 sensor (replaced by keyboard input)
- `src/hardware/powerHandler.cpp` — Physical buttons, deep sleep (stubbed)
- `src/hardware/wifiHandler.cpp` — WiFi + HTTP (stubbed)
- `src/hardware/bluetoothHandler.cpp` — BLE trampoline sensor (stubbed, uses simulation)
- `src/hardware/serialHandler.cpp` — Serial commands (uses SD_MMC, esp_rom_crc)
- `src/hardware/prefsHandler.cpp` — NVS preferences (replaced by JSON)
- `src/hardware/joystickHandler.cpp` — I2C joystick (stubbed, returns not present)
- `src/hardware/xpt2046.cpp/h` — Touch controller class (replaced by SDL mouse)
- `src/games/*.cpp` — Game logic (uses gfx, SD, prefs — all shimmed)

## External Dependencies

| Library | Source | Purpose | Notes |
|---|---|---|---|
| ArduinoJson 7.x | bblanchon/ArduinoJson | JSON parsing for prefs | Git submodule (master) ✅ |
| EspLuaEngine | luc-github/EspLuaEngine | Lua scripting for games | Includes bundled Lua 5.4.7 ✅ |
| base64_arduino | Densaugeo/base64_arduino | Base64 encoding for prefs dump | Git submodule ✅ |
| SDL2 | System package (libsdl2-dev) | Display rendering and input | CMake find_package |
| OneButton | mathertel/OneButton | Button debouncing | Stubbed, not needed as submodule |

## Notes for Future Implementation

- The `lib/TFT_eSPI/` directory from the original project is NOT used. We provide our own
  `TFT_eSPI.h` in compat/ that implements the needed classes from scratch using SDL2.
- EspLuaEngine's bundled Lua 5.4 is used directly — no system Lua library needed.
- The `data/` directory mirrors the SD card filesystem structure from the device. Default files
  (profiles.ini, systemConfig.ini) are provided for immediate testing.
- The `src/` symlink points to `../T-HMI-PEPmonitor/src/` — changes to the original source
  are automatically picked up by the compat build.
