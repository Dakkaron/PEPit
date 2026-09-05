/*
 * PEPitCompat — Display Shim (SDL2 window management)
 * 
 * The real TFT_eSPI library handles all drawing. Our custom processor (TFT_eSDL)
 * writes to a shared pixel buffer. This file manages the SDL2 window/renderer/texture.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

// Forward declarations — these are implemented in our custom processor (TFT_eSDL.c)
extern "C" {
    void setSDLWindow(SDL_Window* window, SDL_Renderer* renderer);
    void updateSDL(void);
}

// ============================================================
// Global SDL2 state (used by main.cpp and our processor)
// ============================================================

static SDL_Window*    g_sdl_window   = nullptr;
static SDL_Renderer*  g_sdl_renderer = nullptr;

// Called from main.cpp after SDL init
void initDisplay(SDL_Window** window, SDL_Renderer** renderer) {
    g_sdl_window = *window;
    g_sdl_renderer = *renderer;
    
    // Pass SDL handles to our processor so it can create texture and render
    setSDLWindow(g_sdl_window, g_sdl_renderer);
}

// Get SDL window (used by touch shim for mouse coordinate mapping)
SDL_Window* getSDLWindow() {
    return g_sdl_window;
}

// Get SDL renderer (used for scaling queries)
SDL_Renderer* getSDLRenderer() {
    return g_sdl_renderer;
}

// Force a display refresh (e.g., after fillScreen on main display)
void displayRefresh() {
    updateSDL();
}
