/*
 * PEPitCompat — Test Entry Point
 */

#define CATCH_CONFIG_RUNNER
#include "catch_amalgamated.hpp"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unistd.h>

#include "appinit.h"
#include "Arduino.h"
#include "SD_MMC.h"

// Shim entry points (declared in the respective shim files)
extern void initArduino();
extern void initDisplay(SDL_Window** window, SDL_Renderer** renderer);
extern void setTouchDisplayParams(int w, int h, int scale);
extern void setPressureSensorKeys(int lowKey, int goodKey);

int main(int argc, char* argv[]) {
    // Boot the shim exactly like the app (also sets the device-equivalent
    // TZ=UTC baseline the time tests rely on).
    initArduino();

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    if (!initCompatApp(&window, &renderer, /*scale*/ 1, /*fullscreen*/ false)) {
        return 2;
    }

    initDisplay(&window, &renderer);
    setTouchDisplayParams(320, 240, 1);
    setPressureSensorKeys(SDL_SCANCODE_A, SDL_SCANCODE_S);

    // Default filesystem root: a per-process temp directory, so tests never
    // touch the repository's data/ (which a running app instance may use).
    // PEPIT_DATA_DIR (set by ctest to a build-local dir) overrides it.
    // Individual tests override the root again via SD_MMC.setRootDir() per test.
    std::string root;
    const char* dataDir = getenv("PEPIT_DATA_DIR");
    if (dataDir && *dataDir) {
        root = dataDir;
    } else {
        root = (std::filesystem::temp_directory_path() /
                ("pepit_test_main_" + std::to_string(::getpid()))).generic_string();
    }
    if (!root.empty() && root.back() != '/') root += '/';
    std::filesystem::create_directories(root);
    SD_MMC.setRootDir(root);
    SD_MMC.setFactorySeeding(false);  // tests provide their own fixtures
    SD_MMC.begin(nullptr, false);
    // NOTE: no Preferences::preloadTouchCalibration() here — that would write
    // into whatever directory the root points at (kept app-specific).

    Catch::Session session;
    int cliResult = session.applyCommandLine(argc, argv);
    if (cliResult != 0) {
        return cliResult;  // --list / --help / bad arguments were requested
    }
    return session.run();
}
