/*
 * PEPitCompat — Main Entry Point (Linux SDL2)
 * 
 * Replaces the Arduino framework's main() on ESP32.
 * Parses CLI arguments, initializes SDL2, then delegates to original setup()/loop().
 * 
 * Usage:
 *   pepit [--data-dir <path>] [--scale <N>] [--fullscreen]
 *         [--low-pressure-key <key>] [--good-pressure-key <key>]
 *         [--shot-key <key>] [--rec-key <key>] [--media-dir <path>]
 *         [--wip | --wip-games] [--help]
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
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>

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
extern void setScreenshotKey(int scancode);
extern void checkScreenshotKey();
extern void setRecordingKey(int scancode);
extern void setMediaDir(const char* dir);
extern void updateRecording();
extern void stopRecordingIfActive();
extern void notifyKeyDown(SDL_Event* evt);

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
    {"shot-key",          required_argument, 0, 'k'},
    {"rec-key",           required_argument, 0, 'r'},
    {"media-dir",         required_argument, 0, 'm'},
    {"profile",           required_argument, 0, 'p'},
    {"game",              required_argument, 0, 'G'},
    {"wip",               no_argument,       0, 'W'},
    {"wip-games",         no_argument,       0, 'w'},
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
    fprintf(stderr, "  -k, --shot-key <key>          Keyboard key that captures a screenshot to\n");
    fprintf(stderr, "                                <media-dir>/pepit_shot.raw (default: P)\n");
    fprintf(stderr, "  -r, --rec-key <key>           Keyboard key that starts/stops a gameplay\n");
    fprintf(stderr, "                                video recording (<media-dir>/pepit_rec_*.mp4).\n");
    fprintf(stderr, "                                While recording, the window title shows\n");
    fprintf(stderr, "                                \"(recording)\" (default: R)\n");
    fprintf(stderr, "  -m, --media-dir <path>        Output directory for screenshots and video\n");
    fprintf(stderr, "                                recordings (default: /tmp)\n");
    fprintf(stderr, "  -p, --profile <name|id>       Drop straight in: select this profile on the\n");
    fprintf(stderr, "                                profile page (by profiles.ini `name`, or 0-based\n");
    fprintf(stderr, "                                slot index; index must be < 8) instead of waiting\n");
    fprintf(stderr, "                                for menu input\n");
    fprintf(stderr, "      --game <name|id>          Drop straight in: select this game on the game\n");
    fprintf(stderr, "                                page (by gameconfig.ini `name`, or 0-based slot\n");
    fprintf(stderr, "                                index; index must be < 8) instead of waiting for\n");
    fprintf(stderr, "                                menu input\n");
    fprintf(stderr, "  -W, --wip                     Use the WIPSDCardContent/ staging directory as the\n");
    fprintf(stderr, "                                whole SD content root instead of --data-dir. Found\n");
    fprintf(stderr, "                                by searching the current directory and up to 4\n");
    fprintf(stderr, "                                parents. profiles.ini and prefs_*.dat are seeded\n");
    fprintf(stderr, "                                from the data dir when missing there.\n");
    fprintf(stderr, "  -w, --wip-games               Read /games from WIPSDCardContent/games while prefs,\n");
    fprintf(stderr, "                                profiles and system files stay in the data dir.\n");
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
// WIP staging directory (--wip / --wip-games)
// ============================================================

static bool isDirectory(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

/** Search cwd and up to 4 parent directories for a WIPSDCardContent directory. */
static std::string findWipContentDir() {
    char cwdBuf[PATH_MAX];
    if (!getcwd(cwdBuf, sizeof(cwdBuf))) return "";
    std::string dir = cwdBuf;
    for (int level = 0; level <= 4; ++level) {
        std::string candidate = dir + "/WIPSDCardContent";
        if (isDirectory(candidate)) return candidate + "/";
        size_t slash = dir.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        dir = dir.substr(0, slash);
    }
    return "";
}

static bool copyFileIfMissing(const std::string& src, const std::string& dst) {
    struct stat dstSt;
    if (stat(dst.c_str(), &dstSt) == 0) return false;  // existing WIP state always wins
    FILE* in = fopen(src.c_str(), "rb");
    if (!in) return false;
    FILE* out = fopen(dst.c_str(), "wb");
    if (!out) { fclose(in); return false; }
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return true;
}

/** Copy profiles.ini and prefs_*.dat into the WIP root so a --wip run starts
 * from the same runtime state as the normal data dir. Never overwrites. */
static void seedWipState(const std::string& dataDir, const std::string& wipDir) {
    int copied = 0;
    if (copyFileIfMissing(dataDir + "profiles.ini", wipDir + "profiles.ini")) copied++;
    DIR* d = opendir(dataDir.c_str());
    if (d) {
        struct dirent* entry;
        while ((entry = readdir(d)) != nullptr) {
            std::string name = entry->d_name;
            if (name.rfind("prefs_", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".dat") == 0) {
                if (copyFileIfMissing(dataDir + name, wipDir + name)) copied++;
            }
        }
        closedir(d);
    }
    if (copied > 0) {
        printf("[PEPitCompat] Seeded %d runtime state file(s) from %s into %s\n",
               copied, dataDir.c_str(), wipDir.c_str());
    }
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
    int shotKey = SDL_SCANCODE_P;      // --shot-key: screenshot hotkey
    int recKey  = SDL_SCANCODE_R;      // --rec-key: start/stop gameplay video recording
    std::string mediaDir = "/tmp";     // --media-dir: screenshots + video recordings output dir
    const char* autoProfile = nullptr;  // --profile <name|id>
    const char* autoGame = nullptr;     // --game <name|id>
    bool wip = false;                   // --wip: whole SD root from WIPSDCardContent
    bool wipGames = false;              // --wip-games: only /games from WIPSDCardContent

    // Parse CLI arguments
    int opt;
    while ((opt = getopt_long(argc, argv, "d:s:fl:g:k:r:m:p:G:hwW", cliOptions, nullptr)) != -1) {
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
            case 'k': {
                int key = parseScancode(optarg);
                if (key >= 0) shotKey = key;
                break;
            }
            case 'r': {
                int key = parseScancode(optarg);
                if (key >= 0) recKey = key;
                break;
            }
            case 'm': {
                mediaDir = optarg;
                break;
            }
            case 'p':
                autoProfile = optarg;
                break;
            case 'G':
                autoGame = optarg;
                break;
            case 'w':
                wipGames = true;
                break;
            case 'W':
                wip = true;
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

    if (wip || wipGames) {
        std::string wipDir = findWipContentDir();
        if (wipDir.empty()) {
            fprintf(stderr, "Error: --wip/--wip-games need a WIPSDCardContent directory "
                            "(searched the current directory and up to 4 parents)\n");
            return 1;
        }
        if (wip) {
            seedWipState(dataDir, wipDir);
            dataDir = wipDir;
        }
        if (wipGames) {
            std::string gamesDir = wipDir + "games";
            if (!isDirectory(gamesDir)) {
                fprintf(stderr, "Error: --wip-games: %s does not exist\n", gamesDir.c_str());
                return 1;
            }
            SD_MMC.setGamesOverrideDir(gamesDir);
            printf("[PEPitCompat] WIP games override: %s\n", gamesDir.c_str());
        }
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
    
    // Configure screenshot hotkey
    setScreenshotKey(shotKey);

    // Configure gameplay recording hotkey
    setRecordingKey(recKey);

    // Configure screenshot/video output directory
    setMediaDir(mediaDir.c_str());
    
    // Configure filesystem root directory and create it if missing
    SD_MMC.setRootDir(dataDir);
    SD_MMC.begin(nullptr, false);

    // Preload spoofed touch calibration so the app skips calibration on first launch
    Preferences::preloadTouchCalibration();

    printf("[PEPitCompat] Data directory: %s\n", dataDir.c_str());
    printf("[PEPitCompat] Media directory (screenshots/videos): %s\n", mediaDir.c_str());
    printf("[PEPitCompat] Window scale: %dx\n", windowScale);
    printf("[PEPitCompat] Pressure keys: low=%s, good=%s\n",
           SDL_GetScancodeName(static_cast<SDL_Scancode>(lowPressureKey)),
           SDL_GetScancodeName(static_cast<SDL_Scancode>(goodPressureKey)));
    printf("[PEPitCompat] Starting application...\n");

    // Set up sigsetjmp BEFORE setup() so signals work even if setup() gets stuck
    sigjmp_buf quitJumpBuf;
    int jumpResult = sigsetjmp(quitJumpBuf, 1);
    if (jumpResult != 0) {
        // We jumped here from the signal handler — skip straight to cleanup.
        // The jmp_buf is spent: re-installing handlers/the periodic alarm here
        // would let a late signal longjmp main a second time and run the
        // cleanup below twice.
        printf("[PEPitCompat] Signal received, exiting...\n");
    }

    if (jumpResult == 0) {
        // Install signal handlers with jump buffer for immediate escape from tight loops
        initSignalHandlers(&quitJumpBuf);

        // Write initial shot metadata so status/screenshot have valid state before first SHOT
        // (must happen before setup() — setup() may block in the selection loops waiting for input)
        writeShotMeta();
        fprintf(stderr, "[PEPitCompat] PRE_SETUP_MARKER\n");

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
                notifyKeyDown(&event);  // hotkey edges (shim/display.cpp)
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

            // Screenshot hotkey (edge-latched, see shim/display.cpp)
            checkScreenshotKey();

            // Recording hotkey + video frame pump (edge-latched, see shim/display.cpp)
            updateRecording();

            // Check for quit request coming from the control file (QUIT command) or signal handler
            if (g_requestQuit) {
                running = false;
                break;
            }
        }
    }

    // Disable jump-based signal escapes before teardown: a late SIGINT/SIGTERM
    // must not longjmp out of the blocking recording transcode below and run
    // this cleanup twice.
    deinitSignalHandlers();

    // Finalize an in-progress gameplay recording before tearing down SDL
    stopRecordingIfActive();

    // Cleanup SDL2
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    TTF_Quit();
    SDL_Quit();
    
    printf("[PEPitCompat] Exiting cleanly\n");
    return 0;
}
