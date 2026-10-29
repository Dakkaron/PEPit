/*
 * PEPitCompat — Shared Application Initialization (implementation)
 *
 * Extracted from shim/main.cpp so both the app and the test binary use the
 * identical SDL2/TTF initialization path.
 */

#include "appinit.h"
#include <SDL2/SDL_ttf.h>
#include <cstdio>

bool initCompatApp(SDL_Window** window, SDL_Renderer** renderer, int scale, bool fullscreen) {
    *window = nullptr;
    *renderer = nullptr;

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "Failed to initialize SDL video: %s\n", SDL_GetError());
        return false;
    }

    // Initialize SDL_ttf for text rendering
    if (TTF_Init() < 0) {
        fprintf(stderr, "Failed to initialize SDL_ttf: %s\n", TTF_GetError());
        SDL_Quit();
        return false;
    }

    // Calculate window dimensions (native display is 320x240)
    int w = 320 * scale;
    int h = 240 * scale;

    // Create window
    uint32_t flags = SDL_WINDOW_SHOWN;  // No RESIZABLE — fixed 320x240 display
    if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    *window = SDL_CreateWindow(
        "PEPitCompat",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        w, h,
        flags
    );

    if (!*window) {
        fprintf(stderr, "Failed to create SDL window: %s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    // Create renderer with software rendering (matches pixel buffer approach)
    *renderer = SDL_CreateRenderer(*window, -1, SDL_RENDERER_SOFTWARE);
    if (!*renderer) {
        fprintf(stderr, "Failed to create SDL renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(*window);
        *window = nullptr;
        TTF_Quit();
        SDL_Quit();
        return false;
    }

    return true;
}
