/*
 * PEPitCompat — Main Entry Point (Linux SDL2)
 * 
 * Replaces the Arduino framework's main() on ESP32.
 * Parses CLI arguments, initializes SDL2, then delegates to original setup()/loop().
 * 
 * Usage:
 *   pepit [--data-dir <path>] [--scale <N>] [--fullscreen]
 *         [--low-pressure-key <key>] [--good-pressure-key <key>]
 *         [--help]
 * 
 * Keyboard keys use SDL scancode names (e.g. A, S, LEFT, RIGHT, SPACE).
 * Defaults: low-pressure=A, good-pressure=S.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <csetjmp>
#include <csignal>
#include <cstdint>
#include <getopt.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <string>

// Auto-nav (menu fast-path): --profile/--game direct start, see shim/autonav.h
#include "autonav.h"

// Shim initialization functions (declared in respective shim files)
extern void initDisplay(SDL_Window** window, SDL_Renderer** renderer);
extern void initSignalHandlers(sigjmp_buf* jumpBuf);
extern void deinitSignalHandlers();

// Global quit flag from arduino.cpp (set by signal handler or vTaskDelay)
extern volatile int32_t g_requestQuit;
extern "C" { void updateSDL(void); }
extern void setTouchDisplayParams(int w, int h, int scale);
extern void setPressureSensorKeys(int lowKey, int goodKey);
extern void initArduino();

// Input control processing — shim/touch.cpp (MCP /tmp/pepit_control + legacy /tmp/pepit_touch IPC)
extern void checkSimulatedInput();
extern void setJoystickOverride(bool active, uint16_t adcX, uint16_t adcY, bool btnPressed);
extern void clearJoystickOverride();
extern void setPressureOverride(bool active, float value);
extern void clearPressureOverride();
extern bool captureScreenshot();
extern void writeShotMeta();

// SD_MMC root directory setter (declared in compat/SD_MMC.h)
#include "SD_MMC.h"

// Preferences shim (for touch calibration preloading)
#include "Preferences.h"

// Original source entry points (defined in T-HMI-PEPmonitor/src/main.cpp)
// These are C++ functions, so no extern "C" — let the linker resolve mangled names.
extern void setup();
extern void loop();

// ============================================================
// CLI Argument Parsing
// ============================================================

static const struct option cliOptions[] = {
    {"data-dir",          required_argument, 0, 'd'},
    {"scale",             required_argument, 0, 's'},
    {"fullscreen",        no_argument,       0, 'f'},
    {"low-pressure-key",  required_argument, 0, 'l'},
    {"good-pressure-key", required_argument, 0, 'g'},
    {"profile",           required_argument, 0, 'p'},
    {"game",              required_argument, 0, 'G'},
    {"help",              no_argument,       0, 'h'},
    {0, 0, 0, 0}
};

static void printUsage(const char* progName) {
    fprintf(stderr, "PEPitCompat — PEPit Linux Compatibility Layer\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Usage: %s [OPTIONS]\n", progName);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  -d, --data-dir <path>       Data directory (default: ./data/)\n");
    fprintf(stderr, "  -s, --scale <N>              Window scale factor (default: 2)\n");
    fprintf(stderr, "  -f, --fullscreen              Start in fullscreen mode\n");
    fprintf(stderr, "  -l, --low-pressure-key <key>  Keyboard key for low pressure (default: A)\n");
    fprintf(stderr, "  -g, --good-pressure-key <key> Keyboard key for good pressure (default: S)\n");
    fprintf(stderr, "  -p, --profile <name|id>       Drop straight in: select this profile on the\n");
    fprintf(stderr, "                                profile page (by profiles.ini `name`, or 0-based\n");
    fprintf(stderr, "                                slot index; index must be < 8) instead of waiting\n");
    fprintf(stderr, "                                for menu input\n");
    fprintf(stderr, "      --game <name|id>          Drop straight in: select this game on the game\n");
    fprintf(stderr, "                                page (by gameconfig.ini `name`, or 0-based slot\n");
    fprintf(stderr, "                                index; index must be < 8) instead of waiting for\n");
    fprintf(stderr, "                                menu input\n");
    fprintf(stderr, "  -h, --help                    Show this help message\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Keyboard keys use SDL scancode names:\n");
    fprintf(stderr, "  A, S, D, W, LEFT, RIGHT, UP, DOWN, SPACE, RETURN, etc.\n");
}

/** Convert SDL scancode name string to integer. Returns -1 on error. */
static int parseScancode(const char* name) {
    if (!name) return -1;
    
    // SDL_GetScancodeFromName takes a string and returns the scancode, or SDL_SCANCODE_UNKNOWN
    SDL_Scancode code = SDL_GetScancodeFromName(name);
    if (code == SDL_SCANCODE_UNKNOWN) {
        // Try parsing as a number directly
        char* endptr;
        long val = strtol(name, &endptr, 10);
        if (*endptr == '\0' && val >= 0 && val < SDL_SCANCODE_UNKNOWN) {
            return (int)val;
        }
        fprintf(stderr, "Warning: unknown scancode '%s', using default\n", name);
        return -1;
    }
    return (int)code;
}

// ============================================================
// SDL2 Initialization — see shim/appinit.cpp (shared with tests)
// ============================================================

#include "appinit.h"

static bool initSDL(SDL_Window** window, SDL_Renderer** renderer, int scale, bool fullscreen) {
    return initCompatApp(window, renderer, scale, fullscreen);
}

// ============================================================
// Main Entry Point
// ============================================================

int main(int argc, char* argv[]) {
    // Capture boot time immediately so millis() returns 0 from program start.
    initArduino();

    // Default configuration
    std::string dataDir = "./data/";
    int windowScale = 2;
    bool fullscreen = false;
    int lowPressureKey = SDL_SCANCODE_A;
    int goodPressureKey = SDL_SCANCODE_S;
    const char* autoProfile = nullptr;  // --profile <name|id>
    const char* autoGame = nullptr;     // --game <name|id>

    // Parse CLI arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "d:s:fl:g:p:G:h", cliOptions, nullptr)) != -1) {
        switch (opt) {
            case 'd':
                dataDir = optarg;
                break;
            case 's':
                windowScale = atoi(optarg);
                if (windowScale < 1) windowScale = 1;
                break;
            case 'f':
                fullscreen = true;
                break;
            case 'l': {
                int key = parseScancode(optarg);
                if (key >= 0) lowPressureKey = key;
                break;
            }
            case 'g': {
                int key = parseScancode(optarg);
                if (key >= 0) goodPressureKey = key;
                break;
            }
            case 'p':
                autoProfile = optarg;
                break;
            case 'G':
                autoGame = optarg;
                break;
            case 'h':
                printUsage(argv[0]);
                return 0;
            default:
                printUsage(argv[0]);
                return 1;
        }
    }
    
    // Ensure trailing slash on data directory
    if (!dataDir.empty() && dataDir.back() != '/') {
        dataDir += '/';
    }

    // Register auto-nav targets (menu fast-path). Names are resolved lazily
    // when each selection page appears, so this is safe to call before setup().
    autoNavConfigure(autoProfile, autoGame);

    // Initialize SDL2 (window + renderer)
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    
    if (!initSDL(&window, &renderer, windowScale, fullscreen)) {
        return 1;
    }
    
    // Pass SDL handles to display shim (for texture creation and rendering)
    initDisplay(&window, &renderer);
    
    // Configure touch shim with display dimensions and scale factor
    setTouchDisplayParams(320, 240, windowScale);
    
    // Configure pressure sensor keyboard keys
    setPressureSensorKeys(lowPressureKey, goodPressureKey);
    
    // Configure filesystem root directory and create it if missing
    SD_MMC.setRootDir(dataDir);
    SD_MMC.begin(nullptr, false);

    // Preload spoofed touch calibration so the app skips calibration on first launch
    Preferences::preloadTouchCalibration();

    printf("[PEPitCompat] Data directory: %s\n", dataDir.c_str());
    printf("[PEPitCompat] Window scale: %dx\n", windowScale);
    printf("[PEPitCompat] Pressure keys: low=%s, good=%s\n",
           SDL_GetScancodeName(static_cast<SDL_Scancode>(lowPressureKey)),
           SDL_GetScancodeName(static_cast<SDL_Scancode>(goodPressureKey)));
    printf("[PEPitCompat] Starting application...\n");

    // Set up sigsetjmp BEFORE setup() so signals work even if setup() gets stuck
    sigjmp_buf quitJumpBuf;
    int jumpResult = sigsetjmp(quitJumpBuf, 1);
    if (jumpResult != 0) {
        // We jumped here from the signal handler — quit immediately
        printf("[PEPitCompat] Signal received, exiting...\n");
    }

    // Install signal handlers with jump buffer for immediate escape from tight loops
    initSignalHandlers(&quitJumpBuf);

    // Write initial shot metadata so status/screenshot have valid state before first SHOT
    // (must happen before setup() — setup() may block in the selection loops waiting for input)
    writeShotMeta();
    fprintf(stderr, "[PEPitCompat] PRE_SETUP_MARKER\n");

    // If we jumped here from a signal during setup(), skip the main loop
    if (jumpResult == 0) {
        // Call original setup() — initializes all peripherals and runs UI flow
        setup();
        fprintf(stderr, "[PEPitCompat] POST_SETUP_MARKER\n");

        printf("[PEPitCompat] Entering main loop (press Ctrl+C to exit)\n");

        // Main event loop — pumps SDL events and calls original loop()
        bool running = true;
        while (running) {
            // Check for quit request from signal handler or vTaskDelay/yield
            if (g_requestQuit) {
                running = false;
                break;
            }

            // Pump SDL events each iteration (keeps joystick/sensor keyboard input responsive)
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_QUIT) {
                    running = false;
                    break;
                }
                if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_CLOSE) {
                    running = false;
                    break;
                }
                if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    break;
                }
            }

            if (!running) break;

            // Call original loop() — handles serial input and game logic
            loop();

            // Ensure display is flushed each frame (fallback if app doesn't call endWrite)
            updateSDL();

            // Process MCP / control input commands once per frame (/tmp/pepit_control, /tmp/pepit_touch)
            checkSimulatedInput();

            // Check for quit request coming from the control file (QUIT command) or signal handler
            if (g_requestQuit) {
                running = false;
                break;
            }
        }
    }

cleanup:
    // Cleanup SDL2
    deinitSignalHandlers(); // Disable jump buffer before cleanup
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    
    printf("[PEPitCompat] Exiting cleanly\n");
    return 0;
}
