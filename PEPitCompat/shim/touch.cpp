/*
 * PEPitCompat — Touch Controller Shim (SDL mouse → XPT2046 API)
 *
 * Replaces the original xpt2046.cpp with SDL mouse-based implementation.
 * Maps left mouse button to touch press, mouse position to calibrated coordinates.
 *
 * Supports simulated input via /tmp/pepit_touch file for headless testing:
 *   echo "CLICK 51 77" > /tmp/pepit_touch
 */

#include "xpt2046.h"
#include <SDL2/SDL.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <unistd.h>

// Inline map/constrain (Arduino templates fail type deduction on GCC)
static inline int iMap(int val, int fromLow, int fromHigh, int toLow, int toHigh) {
    return toLow + (val - fromLow) * (toHigh - toLow) / (fromHigh - fromLow);
}
static inline int iConstrain(int val, int lo, int hi) {
    return std::max(lo, std::min(val, hi));
}

// Global SDL window pointer (set by display shim)
extern SDL_Window* getSDLWindow();

// Global quit flag from arduino.cpp (set by signal handler or vTaskDelay)
extern volatile int32_t g_requestQuit;

#include <csignal>

// Static storage for mouse state (shared across all XPT2046 instances)
static int g_mouseX = -1;
static int g_mouseY = -1;
static bool g_mousePressed = false;
static bool g_latchedClick = false;

// Simulated touch input (for headless testing)
static int g_simX = -1;
static int g_simY = -1;
static bool g_simPending = false;  // Pending simulated click (cleared at next frame boundary)
static bool g_simActive = false;   // Active this frame (set when pending is consumed)

// Display dimensions for coordinate scaling (set by display shim)
static int g_displayW = 320;
static int g_displayH = 240;
static int g_scaleFactor = 1;

void setTouchDisplayParams(int w, int h, int scale) {
    g_displayW = w;
    g_displayH = h;
    g_scaleFactor = scale;
}

// Check for simulated input from test harness (file-based IPC)
static void checkSimulatedInput() {
    const char* path = "/tmp/pepit_touch";
    FILE* f = fopen(path, "r");
    if (!f) return;

    char buf[256] = {0};
    if (fgets(buf, sizeof(buf), f)) {
        int sx, sy;
        if (sscanf(buf, "CLICK %d %d", &sx, &sy) == 2) {
            g_simX = sx;
            g_simY = sy;
            g_simPending = true;  // Mark as pending for next frame
        }
    }
    fclose(f);

    // Clear the file so it can be used again
    f = fopen(path, "w");
    if (f) fclose(f);
}

// Update mouse state from SDL (called before any touch query)
static void updateMouseState() {
    // Frame boundary: activate pending simulated click, or clear active one
    if (g_simPending) {
        g_simActive = true;
        g_simPending = false;
    } else if (g_simActive) {
        // Simulated click was active last frame, no new pending - clear it
        g_simActive = false;
    }

    // Check for new simulated input commands
    checkSimulatedInput();

    SDL_Window* window = getSDLWindow();
    if (!window) return;

    // Drain ALL pending events so mouse state is current.
    SDL_Event evt;
    while (SDL_PollEvent(&evt)) {
        if (evt.type == SDL_MOUSEBUTTONDOWN && evt.button.button == SDL_BUTTON_LEFT) {
            g_latchedClick = true;
        } else if (evt.type == SDL_MOUSEBUTTONUP && evt.button.button == SDL_BUTTON_LEFT) {
            // Button released
        } else if (evt.type == SDL_MOUSEMOTION) {
            g_mouseX = evt.motion.x;
            g_mouseY = evt.motion.y;
        } else if (evt.type == SDL_QUIT) {
            // Forward quit event back to queue so main loop can handle it
            SDL_PushEvent(&evt);
        } else if (evt.type == SDL_WINDOWEVENT && evt.window.event == SDL_WINDOWEVENT_CLOSE) {
            // Forward close event back to queue so main loop can handle it
            SDL_PushEvent(&evt);
        }
    }

    // Check for ESC key press to request quit
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    if (keystate[SDL_SCANCODE_ESCAPE]) {
        g_requestQuit = 1;
    }

    // Get current mouse position and button state
    SDL_GetMouseState(&g_mouseX, &g_mouseY);
    g_mousePressed = (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(1)) != 0;
}

boolean XPT2046::pressed(void) {
    updateMouseState();
    // Simulated input takes priority (for headless testing)
    if (g_simActive) return true;
    bool result = g_mousePressed || g_latchedClick;
    if (g_latchedClick && !g_mousePressed) {
        g_latchedClick = false;
    }
    return result;
}

// Helper: map mouse window coords directly to screen coords (1:1 mapping)
static uint16_t mouseToScreenX() {
    // Use actual window dimensions, not assumed scale factor (handles WM resizing)
    int windowW = g_displayW * g_scaleFactor;
    SDL_Window* window = getSDLWindow();
    if (window) {
        int actualW, actualH;
        SDL_GetWindowSize(window, &actualW, &actualH);
        if (actualW > 0) windowW = actualW;
    }
    if (windowW <= 0) windowW = g_displayW;
    int sx = iMap(g_mouseX, 0, windowW, 0, g_displayW);
    return (uint16_t)iConstrain(sx, 0, g_displayW);
}

static uint16_t mouseToScreenY() {
    // Use actual window dimensions, not assumed scale factor (handles WM resizing)
    int windowH = g_displayH * g_scaleFactor;
    SDL_Window* window = getSDLWindow();
    if (window) {
        int actualW, actualH;
        SDL_GetWindowSize(window, &actualW, &actualH);
        if (actualH > 0) windowH = actualH;
    }
    if (windowH <= 0) windowH = g_displayH;
    int sy = iMap(g_mouseY, 0, windowH, 0, g_displayH);
    return (uint16_t)iConstrain(sy, 0, g_displayH);
}

XPT2046::XPT2046(SPIClass &spi, byte cs, uint8_t tirq)
    : _spi(&spi), _cs(cs), _tirq(tirq),
      _xraw(0), _yraw(0), _zraw(0), _xcoord(0), _ycoord(0), _zThreshold(0) {
    _hmin = 0; _hmax = 4095; _vmin = 0; _vmax = 4095;
    _hres = 320; _vres = 240;
    _hmin_logic = 0; _hmax_logic = 320;
    _vmin_logic = 0; _vmax_logic = 240;
}

void XPT2046::begin(uint16_t xres, uint16_t yres) {
    _hres = xres;
    _vres = yres;
}

uint16_t XPT2046::RawX(void) {
    updateMouseState();
    if (g_simActive) return (uint16_t)g_simX;
    _xraw = mouseToScreenX();
    return _xraw;
}

uint16_t XPT2046::RawY(void) {
    updateMouseState();
    if (g_simActive) return (uint16_t)g_simY;
    _yraw = mouseToScreenY();
    return _yraw;
}

uint16_t XPT2046::RawZ(void) {
    updateMouseState();
    _zraw = (g_mousePressed || g_simActive) ? 4095 : 0;
    return _zraw;
}

uint16_t XPT2046::X(void) {
    updateMouseState();
    if (g_simActive) return (uint16_t)g_simX;
    return mouseToScreenX();
}

uint16_t XPT2046::Y(void) {
    updateMouseState();
    if (g_simActive) return (uint16_t)g_simY;
    return mouseToScreenY();
}

// No-ops: we do direct 1:1 mouse-to-screen mapping, ignore calibration/rotation
void XPT2046::setCal(uint16_t xmin, uint16_t xmax, uint16_t ymin, uint16_t ymax, uint16_t xres, uint16_t yres) {
    (void)xmin; (void)xmax; (void)ymin; (void)ymax; (void)xres; (void)yres;
}

void XPT2046::setRotation(byte rotation) {
    (void)rotation;
}

void XPT2046::setZThreshold(int16_t threshold) {
    _zThreshold = threshold;
}
