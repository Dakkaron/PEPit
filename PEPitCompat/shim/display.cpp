/*
 * PEPitCompat — Display Shim (SDL2 window management)
 * 
 * The real TFT_eSPI library handles all drawing. Our custom processor (TFT_eSDL)
 * writes to a shared pixel buffer. This file manages the SDL2 window/renderer/texture.
 */

#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>

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
    fprintf(stderr, "[PEPitCompat] MARK_DISPLAY_INIT\n");
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

// ============================================================
// Screenshot capture (for MCP server / headless testing)
// ============================================================

// Framebuffer buffer (RGB565) owned by our custom processor (TFT_eSDL.cpp)
extern uint16_t* sdl_tft_buffer;

// System state getter from the original source (systemStateHandler)
extern uint32_t getSystemState();

// Write the shot metadata file (timestamp, system state, dimensions) — without framebuffer capture.
// Called at startup and from captureScreenshot.
void writeShotMeta() {
    time_t now = time(nullptr);
    char meta[256];
    snprintf(meta, sizeof(meta),
             "{\"timestamp\":%ld,\"systemState\":%u,\"width\":320,\"height\":240}",
             (long)now, (unsigned)getSystemState());
    // Write via temp file + rename so readers never see a partial JSON.
    FILE* mf = fopen("/tmp/pepit_shot.json.tmp", "wb");
    if (mf) {
        fwrite(meta, 1, strlen(meta), mf);
        fclose(mf);
        rename("/tmp/pepit_shot.json.tmp", "/tmp/pepit_shot.json");
    }
}

// Capture the current framebuffer as raw RGB888 (320x240) + metadata file.
// MCP server reads /tmp/pepit_shot.raw and /tmp/pepit_shot.json.
bool captureScreenshot() {
    if (!sdl_tft_buffer) {
        return false;
    }

    // Convert RGB565 -> RGB888 into a full-frame buffer, then publish it with a
    // single write + atomic rename so concurrent readers never see a
    // partially written frame.
    static uint8_t* frame = nullptr;
    if (!frame) {
        frame = (uint8_t*)malloc(320 * 240 * 3);
    }
    if (!frame) {
        return false;
    }
    uint8_t* out = frame;
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            uint16_t px = sdl_tft_buffer[y * 320 + x];
            uint32_t r5 = (px >> 11) & 0x1F;
            uint32_t g5 = (px >> 5) & 0x3F;
            uint32_t b5 = px & 0x1F;
            out[0] = (uint8_t)((r5 << 3) | (r5 >> 2));
            out[1] = (uint8_t)((g5 << 2) | (g5 >> 4));
            out[2] = (uint8_t)((b5 << 3) | (b5 >> 2));
            out += 3;
        }
    }

    FILE* f = fopen("/tmp/pepit_shot.raw.tmp", "wb");
    if (!f) {
        return false;
    }
    bool ok = (fwrite(frame, 1, 320 * 240 * 3, f) == 320 * 240 * 3);
    fclose(f);
    if (!ok) {
        unlink("/tmp/pepit_shot.raw.tmp");
        return false;
    }
    rename("/tmp/pepit_shot.raw.tmp", "/tmp/pepit_shot.raw");

    // Write metadata file (timestamp, system state, dimensions)
    writeShotMeta();

    return true;
}
