/*
 * PEPitCompat — Shared Application Initialization
 *
 * SDL2/TTF window+renderer setup, shared between the app entry point
 * (shim/main.cpp) and the test binary (tests/catch_main.cpp).
 */

#ifndef APPINIT_H
#define APPINIT_H

#include <SDL2/SDL.h>

/**
 * Initialize SDL video + TTF and create the window/renderer.
 *
 * Works headless: set SDL_VIDEODRIVER=dummy (e.g. in CI) to run without a
 * display server.
 *
 * Returns true on success; on failure *window/*renderer are nullptr.
 */
bool initCompatApp(SDL_Window** window, SDL_Renderer** renderer, int scale, bool fullscreen);

#endif // APPINIT_H
