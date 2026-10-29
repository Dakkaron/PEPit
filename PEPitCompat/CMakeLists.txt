cmake_minimum_required(VERSION 3.16)
project(pepit_compat VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================
# Dependencies
# ============================================================

find_package(SDL2 REQUIRED)
find_package(SDL2_ttf REQUIRED)

# ============================================================
# Shim source files (our Linux implementations, minus app entry point)
# ============================================================

set(SHIM_LIB_SOURCES
    shim/arduino.cpp
    shim/display.cpp
    shim/filesystem.cpp
    shim/prefs.cpp
    shim/sensor.cpp
    shim/joystick.cpp
    shim/touch.cpp
    shim/thread.cpp
    shim/instances.cpp
    shim/appinit.cpp
)

# ============================================================
# Real TFT_eSPI library sources (with our custom SDL2 processor)
# ============================================================

# TFT_eSPI.cpp #include-s its own extensions (Sprite.cpp, Button.cpp, Touch.cpp, etc.)
# so they must NOT be listed here — only the main .cpp and our custom processor.
set(TFT_ESPI_SOURCES
    third_party/TFT_eSPI/TFT_eSPI.cpp
    third_party/TFT_eSPI/Processors/TFT_eSDL.cpp
)

# ============================================================
# Original PEPit source files (via symlink to T-HMI-PEPmonitor/src/)
# ============================================================

set(ORIGINAL_SOURCES
    src/constants.h
    src/main.cpp
    src/pins.h
    src/systemconfig.cpp
    src/systemconfig.h
    src/systemStateHandler.cpp
    src/systemStateHandler.h
    src/physioProtocolHandler.cpp
    src/physioProtocolHandler.h
    src/updateHandler.cpp
    src/updateHandler.h
    src/hardware/gfxHandler.cpp
    src/hardware/gfxHandler.hpp
    src/hardware/higherLevelGfxHandler.cpp
    src/hardware/higherLevelGfxHandler.hpp
    src/hardware/MyFont.h
    src/hardware/prefsHandler.cpp
    src/hardware/prefsHandler.h
    src/hardware/sdHandler.cpp
    src/hardware/sdHandler.h
    src/hardware/pressuresensor.cpp
    src/hardware/pressuresensor.h
    src/hardware/touchHandler.cpp
    src/hardware/touchHandler.h
    src/hardware/wifiHandler.cpp
    src/hardware/wifiHandler.h
    src/hardware/bluetoothHandler.cpp
    src/hardware/bluetoothHandler.h
    src/hardware/serialHandler.cpp
    src/hardware/serialHandler.h
    src/hardware/powerHandler.cpp
    src/hardware/powerHandler.h
    src/hardware/joystickHandler.cpp
    src/hardware/joystickHandler.h
    src/hardware/matrix.cpp
    src/hardware/matrix.h
    # NOTE: xpt2046.cpp is EXCLUDED — replaced by shim/touch.cpp
    src/hardware/xpt2046.h
    src/games/games.cpp
    src/games/games.h
    src/games/gameLua.cpp
    src/games/gameLua.h
    src/games/gameMonsterCatcher.cpp
    src/games/gameMonsterCatcher.h
    src/games/gameRacing.cpp
    src/games/gameRacing.h
    src/games/monsterDetails.cpp
    src/games/monsterDetails.h
)

# ============================================================
# Lua 5.4.7 source files (bundled in EspLuaEngine submodule)
# ============================================================

file(GLOB LUA_SOURCES_GLOB "${CMAKE_SOURCE_DIR}/third_party/EspLuaEngine/src/lua-5.4.7/src/*.c")
list(FILTER LUA_SOURCES_GLOB EXCLUDE REGEX "\\.c\\.disabled$")
set(LUA_SOURCES ${LUA_SOURCES_GLOB})

# ============================================================
# EspLuaEngine source (wraps Lua for Arduino-compatible API)
# ============================================================

set(ESPLUA_SOURCES
    third_party/EspLuaEngine/src/EspLuaEngine.cpp
)

# ============================================================
# Shared core library: shims + real TFT_eSPI + original sources + Lua
#
# Linked by both the app executable and the test executable.
# ============================================================

add_library(pepit_core STATIC
    ${SHIM_LIB_SOURCES}
    ${TFT_ESPI_SOURCES}
    ${ORIGINAL_SOURCES}
    ${LUA_SOURCES}
    ${ESPLUA_SOURCES}
)

target_include_directories(pepit_core PUBLIC
    ${CMAKE_SOURCE_DIR}/compat                                    # 1. Shims (highest priority — intercept ESP32 headers)
    ${CMAKE_SOURCE_DIR}/shim                                      # 2. Shim implementation headers (internal)
    ${CMAKE_SOURCE_DIR}/third_party/TFT_eSPI                     # 3. Real TFT_eSPI lib root (User_Setup_Select.h, Processors/, etc.)
    ${CMAKE_SOURCE_DIR}/third_party/ArduinoJson                   # 4. ArduinoJson root (provides <ArduinoJson.h>)
    ${CMAKE_SOURCE_DIR}/third_party/ArduinoJson/src               # 5. ArduinoJson internals (<ArduinoJson/Array/...>)
    ${CMAKE_SOURCE_DIR}/third_party/EspLuaEngine/src             # 6. EspLuaEngine + Lua (provides lua-5.4.7/src/lua.hpp)
    ${CMAKE_SOURCE_DIR}/third_party/EspLuaEngine/src/lua-5.4.7/src  # 7. Lua internal headers (lapi.h, lobject.h, etc.)
    ${CMAKE_SOURCE_DIR}/third_party/base64_arduino/src           # 8. base64.hpp
    ${CMAKE_SOURCE_DIR}/src/hardware                              # 9. Original hardware sources (xpt2046.h, etc.)
    ${CMAKE_SOURCE_DIR}/src/games                                  # 10. Original game sources
    ${CMAKE_SOURCE_DIR}/src                                       # 11. Original source code root (constants.h, pins.h)
)

target_compile_definitions(pepit_core PUBLIC
    LUA_USE_C89                          # Required by EspLuaEngine for C89-compatible Lua
    VERBOSE                              # Enable log_v (used by EspLuaEngine)
    PROCESSOR_INCLUDE="Processors/TFT_eSDL.h"  # Tell TFT_eSPI to use our SDL2 processor
    DISABLE_ALL_LIBRARY_WARNINGS         # Suppress hardware-specific warnings
)

target_link_libraries(pepit_core PUBLIC
    SDL2::SDL2
    SDL2_ttf::SDL2_ttf
    pthread                              # Required for std::thread and pthread_self()
)

# ============================================================
# App executable (original firmware + shims, entry point = shim/main.cpp)
# ============================================================

add_executable(pepit_compat
    shim/main.cpp
)

target_link_libraries(pepit_compat PRIVATE pepit_core)

# NOTE: the app's setup()/loop() call graph references every module, so all
# objects (and their global constructors) are pulled in from the archive.

# ============================================================
# Unit tests (Catch2, run on the host — see TESTING.md in the app repo)
# ============================================================

enable_testing()  # must be at the top level for ctest to collect subdirectory tests

add_subdirectory(tests)
