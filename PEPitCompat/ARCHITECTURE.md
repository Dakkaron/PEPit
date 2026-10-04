# PEPitCompat — Technical Architecture

## Design Philosophy

The original ESP32 source code must compile and run **without any modifications**. This is
achieved through a header-interception strategy: our `compat/` directory provides files with
the exact same names as ESP32 framework headers, so `#include <Arduino.h>` resolves to our
shim instead of the ESP-IDF Arduino core.

## Include Resolution Strategy

CMake's `target_include_directories` is ordered by priority:

```cmake
target_include_directories(pepit_compat PRIVATE
    ${CMAKE_SOURCE_DIR}/compat          # 1. Shims (highest priority)
    ${CMAKE_SOURCE_DIR}/shim            # 2. Shim implementation headers
    ${CMAKE_SOURCE_DIR}/third_party/ArduinoJson/src  # 3. ArduinoJson
    ${CMAKE_SOURCE_DIR}/third_party/EspLuaEngine/src # 4. EspLuaEngine + Lua 5.4.7
    ${CMAKE_SOURCE_DIR}/third_party/base64_arduino/src  # 5. base64
    ${CMAKE_SOURCE_DIR}/src             # 6. Original source code
)
```

**Submodule include paths:**
- `ArduinoJson/src/` — provides `<ArduinoJson.h>` (original code uses angle brackets)
- `EspLuaEngine/src/` — provides `"lua-5.4.7/src/lua.hpp"` and `EspLuaEngine.h`
- `base64_arduino/src/` — provides `"base64.hpp"` (original code uses quotes)
```

When the compiler sees `#include <Arduino.h>`, it checks each directory in order and uses
the first match. Our `compat/Arduino.h` is found first, so the original code gets our
Linux-compatible implementation.

## Shim Architecture

Each shim follows a consistent pattern:

### Header-Only Shims (compat/)
Headers in `compat/` provide the API surface that the original code expects. They may:
- Define classes with the same interface as ESP32 equivalents
- Provide function declarations implemented in `shim/*.cpp`
- Use preprocessor macros to replace ESP32-specific identifiers

### Implementation Files (shim/)
Implementation files in `shim/` contain the actual Linux-compatible code:
- Use standard C++17 and POSIX APIs
- Link against SDL2 for display and input
- Use standard library (std::thread, std::string, etc.)

### Data Flow Example: Display

```
Original code calls: tft.fillScreen(TFT_BLACK)
       ↓
compat/TFT_eSPI.h declares: class TFT_eSPI { void fillScreen(uint16_t color); }
       ↓
shim/display.cpp implements: fills SDL2 texture with the given 16-bit color value
       ↓
SDL2 renders to window (320x240 or 640x480 with --scale 2)
```

## Detailed Shim Specifications

### 1. Arduino.h (compat/Arduino.h + shim/arduino.cpp) ✅ DONE

**What the original code uses:**
- `millis()` — milliseconds since boot (uint32_t)
- `delay(ms)` — blocking delay in milliseconds
- `String` class — Arduino's dynamic string (case-sensitive, uses `equals()`, `concat()`, etc.)
- `Serial` object — UART output (println, print, write, available, read, flush)
- `pinMode(pin, mode)` / `digitalWrite(pin, value)` / `analogRead(pin)` — GPIO operations
- `_min(a,b)`, `_max(a,b)`, `constrain(val, min, max)` — math helpers
- `map(value, fromLow, fromHigh, toLow, toHigh)` — linear interpolation (used in xpt2046.cpp)
- `random(seed)`, `random(min, max)` — pseudo-random numbers
- `heap_caps_malloc(size, caps)` — PSRAM-aware allocation (maps to malloc)
- `heap_caps_realloc(ptr, size, caps)` — PSRAM-aware reallocation (maps to realloc)
- `MALLOC_CAP_SPIRAM` — memory capability flag
- `RTC_DATA_ATTR`, `DRAM_ATTR`, `IRAM_ATTR`, `FLASHMEM` — attribute macros
- `yield()` / `vTaskDelay(ms)` — task yielding (FreeRTOS)

**ESP-IDF APIs now in Arduino.h:**
- `esp_reset_reason_t` enum + `ESP_RST_SW`, `ESP_RST_USB`, etc. (used in powerHandler.cpp)
- `esp_restart()` — exits program on Linux (used in touchHandler.cpp)
- `esp_reset_reason()` — returns ESP_RST_POWERON on Linux (used in powerHandler.cpp)
- `esp_sleep_enable_ext0_wakeup()` — no-op stub (used in powerHandler.cpp)
- `esp_sleep_enable_timer_wakeup()` — no-op stub (used in powerHandler.cpp)
- `esp_deep_sleep_start()` — exits program on Linux (used in powerHandler.cpp)
- `gpio_get_level()` — returns 0 stub (used in powerHandler.cpp)
- `gpio_hold_en()`, `gpio_hold_dis()` — no-op stubs (used in main.cpp)
- `gpio_deep_sleep_hold_en()`, `gpio_deep_sleep_hold_dis()` — no-op stubs (used in main.cpp)
- `getLocalTime(&tm, offset)` — POSIX localtime_r wrapper (used in wifiHandler.cpp)

**Global instance declarations (in Arduino.h, defined elsewhere):**
- `extern WiFiClass WiFi;` — defined in compat/WiFi.h (NOT YET)
- `extern SPIClass SPI;` — defined in compat/SPI.h (NOT YET)
- `extern NuSerialClass NuSerial;` — defined in compat/NuSerial.hpp (NOT YET)
- `extern UpdateClass Update;` — defined in compat/Update.h (NOT YET)

**Linux implementation:**
- `millis()` → `std::chrono` since program start
- `delay(ms)` → `usleep(ms * 1000)`
- `String` → Full implementation wrapping std::string (equals, concat, indexOf, substring, etc.)
- `Serial` → Singleton that writes to stdout with `\n` line endings
- GPIO functions → No-op (logged as debug)
- `map()` → Template function for linear interpolation
- `random()` → `std::rand()` with proper seeding
- `heap_caps_malloc` → `malloc()` (ignore caps parameter)
- `heap_caps_realloc` → `realloc()` (ignore caps parameter, needed by gameLua.cpp)
- ESP-IDF stubs → No-op or std::exit(0) for restart/sleep
- `getLocalTime()` → POSIX localtime_r with optional timezone offset

**Compile definitions (CMakeLists.txt):**
- `LUA_USE_C89` — required by EspLuaEngine's bundled Lua 5.4
- `VERBOSE` — enables `log_v` verbose logging in EspLuaEngine

**Critical detail:** The Arduino `String` class has specific methods used throughout the code:
- `equals()`, `isEmpty()`, `length()`, `c_str()`
- `concat()`, `substring()`, `indexOf()`, `toInt()`, `trim()`, `toLowerCase()`
- `equalsIgnoreCase(const String&)`, `equalsIgnoreCase(const char*)` — case-insensitive comparison (uses strcasecmp)
- `setCharAt()`, `clear()`, constructor from C-string, int, uint32_t
- Operator overloads: `+`, `+=`, `==` (String, const char*), `!=` (String, const char*)

**Bugs fixed:**
- Added `operator==(const char*)` and `operator!=(const char*)` to resolve ambiguous overload errors when comparing String with string literals
- Added `equalsIgnoreCase` overloads (used by EspLuaEngine)

### 2. SD_MMC.h + FS.h (compat/ + shim/filesystem.cpp) ✅ DONE

**What the original code uses:**
- `SD_MMC.begin(path, mode)` — mount SD card
- `SD_MMC.open(path)` / `SD_MMC.open(path, mode, create)` — open file
- `SD_MMC.exists(path)`, `SD_MMC.remove(path)`, `SD_MMC.rename(old, new)`
- `SD_MMC.mkdir(path)`, `SD_MMC.rmdir(path)`
- `SD_MMC.cardSize()` — total card size in bytes
- `File` class: `read()`, `write()`, `available()`, `seek(offset)`, `size()`,
  `name()`, `isDirectory()`, `openNextFile()`, `close()`
- `fs::File` namespace alias

**Linux implementation:**
- Root directory configurable via `--data-dir` CLI parameter (default: `./data/`)
- `SD_MMC.begin()` → records the data directory path, returns true if it exists
- File operations use POSIX `fopen`/`fread`/`fwrite`/`fseek`/`ftell`
- Directory operations use `opendir`/`readdir`/`closedir`
- `File.name()` returns the basename of the path
- `File.openNextFile()` iterates directory entries via `readdir`

**Bugs fixed:**
- `compat/FS.h`: Added `<cstdio>` and `<dirent.h>` includes (references `FILE*`, `DIR*`)

### 3. TFT_eSPI.h (compat/ + shim/display.cpp) ✅ DONE

**What the original code uses:**
- TFT_eSPI: constructor, init(), fillScreen, setRotation, setSwapBytes, writecommand,
  fillRect, drawRect, drawFastHLine, drawLine, drawCircle, pushImage (with/without mask),
  pushSprite/pushSpriteFast, text rendering (setTextSize, setCursor, print/println,
  drawString with datum alignment), setTextColor, setTextDatum, getTextDatum, setFreeFont,
  invertDisplay, width/height, getBuffer
- TFT_eSprite: constructor(TFT_eSPI*), setColorDepth, createSprite (with/without options),
  deleteSprite, created, width/height, fillSprite, pushSprite/pushSpriteFast,
  pushToSprite (with mask), getPointer/get16BitBuffer, frameBuffer, setSwapBytes,
  all drawing primitives (fillRect, drawLine, etc.), text rendering

**Linux implementation:**
- SDL2 window with 320x240 internal resolution, scaled by `--scale` factor
- Pixel buffer: `uint16_t[320*240]` for RGB565 format
- `TFT_eSPI` owns the SDL window, renderer, and main texture
- `TFT_eSprite` owns its own pixel buffer, allocated on demand
- Text rendering uses SDL_ttf (DejaVuSans font)

**Bugs fixed:**
- `shim/display.cpp:390`: Fixed TFT_eSprite constructor definition — was mangled
  `TFT_eSprite::TFT_eSPI* tft);` → corrected to `TFT_eSprite::TFT_eSprite(TFT_eSPI* tft) : tft_(tft) {}`
- `compat/TFT_eSPI.h`: Added 3-arg `pushToSprite(target, x, y)` overload (no mask) + region-based overloads
- `compat/TFT_eSPI.h`: Added `fillCircle()` and `drawFastVLine()` declarations to TFT_eSprite
- `compat/TFT_eSPI.h`: Added `fontHeight()` to both TFT_eSPI and TFT_eSprite
- `shim/display.cpp`: Implemented all new methods; fixed lambda capture bug in `fillTriangle` (`c` not captured)
- `shim/instances.cpp`: Added 7-arg wrapper stubs for `drawButton`/`drawImageButton` (header/definition mismatch)

### 4. Touch (XPT2046 replacement) ✅ DONE

**What the original code uses:**
- `XPT2046(SPI, cs_pin, irq_pin)` — constructor with SPI bus reference
- `begin(xres, yres)` — initialize with resolution (240x320 for rotated display)
- `pressed()` — returns true if touch is detected
- `RawX()`, `RawY()` — raw ADC values (0-4095)
- `X()`, `Y()` — calibrated coordinates (0-320, 0-240)
- `setCal(xmin, xmax, ymin, ymax, xres, yres)` — set calibration points
- `setRotation(rotation)` — set display rotation for coordinate mapping
- `setZThreshold(threshold)` — set touch sensitivity threshold

**Linux implementation (shim/touch.cpp):**
- Replaces original xpt2046.cpp — CMake must exclude original and include our shim
- SDL mouse button 1 (left click) = touch press
- Mouse position scaled from window coords to raw ADC range (0-4095)
- Calibration mapping applied via setCal() parameters (map raw → calibrated coords)
- Rotation affects X/Y coordinate swap for proper orientation

### 10. Joystick (keyboard-mocked) ✅ DONE

**What the original code uses:**
- `M5UnitJoystick2::begin(&Wire, addr, sda, scl)` — I2C initialization
- `set_rgb_color(rgb)` — set LED color
- `get_joy_adc_16bits_value_xy(&x, &y)` — get 16-bit ADC values (0-65535, center=32768)
- `get_button_value()` — returns 0 if pressed, non-zero if released

**Linux implementation (compat/m5_unit_joystick2.hpp + shim/joystick.cpp):**
- Added `JOYSTICK2_ADDR` constant (0x3E) for joystickHandler compatibility
- begin() always returns true (keyboard joystick is "present")
- Arrow keys / WASD mapped to X/Y axis deflection (full range: 0-65535)
- Space / Enter mapped to button press (returns 0 = pressed)
- set_rgb_color is no-op (no physical LED on Linux)

### 11. Pressure Sensor (keyboard-mocked) ✅ DONE

**What the original code uses:**
- `Adafruit_HX711(dataPin, clockPin)` — constructor with pin numbers
- `begin()` — initialize sensor
- `isBusy()` — check if sensor is ready (blocking on ESP32)
- `readChannelRaw(gain)` — read raw ADC value
- `tareA(rawValue)` — set tare offset for zero calibration
- `readChannel(gain)` — read calibrated value (post-tare)
- `powerDown(down)` — power management

**Linux implementation (compat/Adafruit_HX711.h + shim/sensor.cpp):**
- begin() is no-op; isBusy() always returns false (keyboard always ready)
- During init: readChannelRaw returns 0 so tare offset = 0
- After init (taring complete): keyboard-driven values based on target pressure
  - No key pressed: 0 pressure (not blowing)
  - Low-pressure key ('A' default): ~50% of target pressure
  - Good-pressure key ('S' default): ~100% of target pressure
- Keys configurable via --low-pressure-key / --good-pressure-key CLI params
- powerDown() is no-op (keyboard always available)

### 5. Preferences (compat/Preferences.h + shim/prefs.cpp) ✅ DONE

**What the original code uses:**
- `Preferences prefs` — global instance (or local)
- `prefs.begin(namespace)` / `prefs.begin(namespace, readonly)` — open namespace
- `prefs.end()` — close current namespace (saves if not readonly)
- `prefs.isKey(key)` — check if key exists
- Type-specific getters: getUInt, getInt, getUShort, getShort, getULong64, getLong64,
  getUChar, getChar, getString (to buffer), getBytesLength/getBytes (base64 blobs)
- Type-specific setters: putUInt, putInt, putUShort, putShort, putULong64, putLong64,
  putUChar, putChar, putString, putBytes (base64)
- `prefs.clear()` — remove all keys in current namespace
- `prefs.remove(key)` — remove specific key
- `prefs.freeEntries()` — returns 999 (stubbed)

**Linux implementation:**
- Text file per namespace: `<data_dir>/prefs_<namespace>.dat`
- File format: `key:type:value` (one per line) — NOT JSON as originally planned
- Binary blobs stored as base64 strings
- `begin()` loads the file into memory (std::map)
- `end()` writes memory back to disk

**Bugs fixed:**
- `compat/Preferences.h`: Added `<map>` include (uses `std::map<std::string, JsonValue>`)
- Added `getString(key, default)` returning String (2-arg overload) — used by prefsHandler
- Added `getFloat()` / `putFloat()` methods — used for float preferences storage

### 6. WiFi + HTTPClient (stubbed) ✅ DONE

**What the original code uses:**
- `WiFi.scanNetworks()` → returns 0 (no networks)
- `WiFi.SSID(i)` → empty string
- `WiFi.mode(WIFI_STA)` → no-op
- `WiFi.begin(ssid, password)` → OK (returns true), but status stays disconnected
- `WiFi.status()` → returns WL_NO_SSID_AVAIL (no WiFi available)
- `HTTPClient` class with `begin()`, `GET()`, `getSize()`, `getStreamPtr()`

**Linux implementation (compat/WiFi.h, HTTPClient.h):**
- WiFi: begin() returns OK, but status shows no connection (WL_NO_SSID_AVAIL)
- HTTPClient: GET returns 200 OK with empty stream (available=0, so download loops zero iterations)
- This allows the code flow to continue normally — downloads "succeed" with 0 bytes,
  update check completes with empty URL → FIRMWARE_UPDATE_NOT_AVAILABLE

### 7. OTA Update (stubbed) ✅ DONE

**What the original code uses:**
- `Update.begin(size, U_FLASH)` — start OTA update
- `Update.writeStream(file)` — write firmware from file stream
- `Update.end()` — finalize update (triggers ESP.restart() if true)
- `Update.onProgress(callback)` — progress reporting callback

**Linux implementation (compat/Update.h):**
- begin(), writeStream() return OK (no-op)
- end() returns FALSE to prevent ESP.restart() after simulated update
- onProgress() is no-op callback registration
- This ensures the program continues running after a simulated update attempt

### 7. FreeRTOS (shim/thread.cpp) ✅ DONE

**What the original code uses:**
- `xTaskCreatePinnedToCore(func, name, stack, param, priority, handle, core)`
- `vTaskDelay(ticks)` — delay in FreeRTOS ticks (1 tick = 1ms on ESP32)
- `vTaskDelete(handle)` — delete task (NULL = self)
- `xTaskGetCurrentTaskHandle()` — get current task handle

**Linux implementation:**
- `xTaskCreatePinnedToCore` → `std::thread(func, param)` (ignore core pinning)
- `vTaskDelay(ms)` → `usleep(ticks * 1000)` (in arduino.cpp)
- `vTaskDelete(NULL)` → `std::exit(0)` or thread return
- Task handles are `pthread_t` cast to `void*`

### 8. ESP-IDF Stubs ✅ DONE

**In Arduino.h:**
- `esp_reset_reason_t` enum + `ESP_RST_SW`, `ESP_RST_USB`, etc.
- `esp_restart()` → std::exit(0)
- `esp_reset_reason()` → returns ESP_RST_POWERON
- `esp_sleep_enable_ext0_wakeup()`, `esp_sleep_enable_timer_wakeup()` → no-op, return OK
- `esp_deep_sleep_start()` → std::exit(0)
- `gpio_get_level()`, `gpio_hold_en()`, `gpio_hold_dis()` → no-op
- `gpio_deep_sleep_hold_en()`, `gpio_deep_sleep_hold_dis()` → no-op

**Separate headers (all stubs return OK):**
- `esp_log.h` — ESP_LOG_ERROR/WARN/INFO/DEBUG/VERBOSE macros → colored printf output + shorthand macros (`log_e`, `log_v`) for EspLuaEngine
- `esp_rom_crc.h` — CRC32 calculation (functional implementation for serialHandler)
- `nvs_flash.h` / `nvs.h` — NVS flash stubs (all return OK, correct signatures)
- `driver/rtc_io.h` — RTC GPIO stubs (all return OK)
- `esp32-hal.h` — ESP32 HAL stub (empty, for include compat)

### 9. Other Stubs ✅ DONE

- `SPI.h` — SPIClass stub + global SPI instance (no-op methods)
- `SD.h` — Empty stub for include compatibility (SD_MMC is actual interface used)
- `OneButton.h` — No-op class (accepts nullptr callbacks, all methods do nothing)
- `NuSerial.hpp` — Bluetooth UART stub (isConnected returns false, all methods no-op)
- `NimBLEDevice.h` — BLE device stubs + CONFIG_BT_ENABLED/BLUEDROID_ENABLED defines
- `Wire.h` — I2C bus stub (TwoWire class + global Wire instance, all no-op)
- `ESP32-targz.h` — Firmware extraction stubs (TarUnpacker/TarGzUnpacker classes, all OK)
- `ESP` class (in Arduino.h) — restart(), getFreeHeap(), getFreePsram()

## Threading Model

The original code uses FreeRTOS tasks for:
1. Firmware update check (background task) — replaced with std::thread
2. Watchdog yields (`vTaskDelay(1)` in tight loops) — replaced with usleep

On Linux, the main loop runs in the main thread. Background tasks (firmware check)
run in separate std::threads. The `vTaskDelay(1)` calls become very short sleeps
to prevent CPU spinning.

## Exit Behavior

- `power_off()` / `deepSleepReset()` → calls `std::exit(0)` (graceful program exit)
- `ESP.restart()` / `esp_restart()` → calls `std::exit(0)` (same as power off on Linux)
- `esp_deep_sleep_start()` → calls `std::exit(0)`

## Configuration Flow

1. User runs: `./pepit_compat --scale 2 --data-dir /path/to/sd/`
2. `shim/main.cpp` parses CLI arguments, stores in global config struct
3. SDL2 window created with appropriate scale factor
4. Filesystem shim records data directory path (or uses `./data/` default)
5. Original `setup()` is called — initializes all subsystems through shims (source via symlink)
6. Original `loop()` runs in a while(true) loop until exit is requested

**Default data directory:** When `--data-dir` is not specified, uses `./data/` which contains
default profiles.ini and systemConfig.ini for immediate testing. The `src/` symlink ensures
the original T-HMI-PEPmonitor source compiles without modification.

## Known Limitations (By Design)

- No real WiFi connectivity — all network operations return errors
- No OTA updates — stubbed, logs skip message
- No NTP time synchronization — returns "N/A" for time queries (getLocalTime works but no NTP)
- No physical button support — OneButton stubs do nothing
- No real pressure sensor — keyboard-driven simulation only
- No Bluetooth trampoline connection — must use simulateTrampoline mode (enabled by default)
- No deep sleep / power management — program exits instead

These limitations are acceptable for development purposes. The game logic, UI rendering,
file I/O, and state management all work correctly on Linux. Default data files in `data/`
enable immediate testing without needing an SD card image.
