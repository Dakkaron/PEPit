/*
 * PEPitCompat — TFT_eSPI SDL2 Processor Implementation
 * 
 * Replaces ESP32 SPI driver with direct pixel buffer writes to SDL2.
 * Implements pushBlock, pushPixels, DMA stubs, and SPI transaction stubs.
 */

#include "TFT_eSPI.h"  // Must come first — pulls in our processor header + class definitions
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <cstring>
#include <cstdint>

// ============================================================
// Global pixel buffer and position state
// ============================================================

uint16_t* sdl_tft_buffer = nullptr;       // 320x240 pixel buffer (RGB565, little-endian for SDL)
int32_t  sdl_tft_x = 0;                   // Current x position (auto-incremented by writeSDLPixel)
int32_t  sdl_tft_y = 0;                   // Current y position

// Window bounds — set by tracking CASET/PASET commands via tft_Write_* macros
int32_t  sdl_win_x = 0;                   // Window start x
int32_t  sdl_win_y = 0;                   // Window start y
int32_t  sdl_win_w = 320;                 // Window width
int32_t  sdl_win_h = 240;                 // Window height

// Command tracking state — used by tft_Write_* macros to capture window bounds
uint8_t  sdl_track_cmd = 0;               // Pending command (0x2A=CASET, 0x2B=PASET)
uint8_t  sdl_track_idx = 0;               // Data byte index (0 or 1 for 16-bit coord)
uint16_t  sdl_track_acc = 0;              // Accumulator for 16-bit value (bytes come separately)

// SDL handles (set from display.cpp via setSDLWindow)
static SDL_Window*    g_sdl_window   = nullptr;
static SDL_Renderer*  g_sdl_renderer = nullptr;
static SDL_Texture*   g_sdl_texture  = nullptr;

// ============================================================
// Buffer initialization (called from INIT_TFT_DATA_BUS macro in init())
// ============================================================

void initSDLBuffer(void) {
    if (!sdl_tft_buffer) {
        sdl_tft_buffer = new uint16_t[320 * 240]();
    }
    if (g_sdl_renderer && !g_sdl_texture) {
        g_sdl_texture = SDL_CreateTexture(
            g_sdl_renderer,
            SDL_PIXELFORMAT_RGB565,
            SDL_TEXTUREACCESS_STREAMING,
            320, 240
        );
    }
}

// ============================================================
// SDL window/renderer setup — extern "C" for display.cpp linkage
// ============================================================

extern "C" {

void setSDLWindow(SDL_Window* window, SDL_Renderer* renderer) {
    g_sdl_window = window;
    g_sdl_renderer = renderer;
}

// Flush buffer to SDL window (called by CS_H macro on endWrite)
void flushSDL(void) {
    if (!g_sdl_texture || !sdl_tft_buffer) return;

    SDL_UpdateTexture(g_sdl_texture, nullptr, sdl_tft_buffer, 320 * sizeof(uint16_t));
    SDL_RenderClear(g_sdl_renderer);
    SDL_RenderCopy(g_sdl_renderer, g_sdl_texture, nullptr, nullptr);
    SDL_RenderPresent(g_sdl_renderer);
}

// Alias for display.cpp — same as flushSDL
void updateSDL(void) {
    flushSDL();
}

} // extern "C"

// ============================================================
// pushBlock — write a block of identical pixels (used by fillRect, etc.)
// 
// Manually iterates with row wrapping at window boundary.
// ============================================================

void TFT_eSPI::pushBlock(uint16_t color, uint32_t len)
{
    if (!sdl_tft_buffer) return;
    
    sdl_tft_x = sdl_win_x;
    sdl_tft_y = sdl_win_y;
    
    while (len > 0) {
        if (sdl_tft_y < sdl_win_y || sdl_tft_y >= sdl_win_y + sdl_win_h) break;
        
        int32_t rowEnd = sdl_win_x + sdl_win_w;
        int32_t rowPixels = rowEnd - std::max(sdl_tft_x, sdl_win_x);
        
        if (rowPixels <= 0) {
            sdl_tft_x = sdl_win_x;
            sdl_tft_y++;
            continue;
        }
        
        uint32_t toWrite = std::min((uint32_t)rowPixels, len);
        
        for (uint32_t i = 0; i < toWrite; i++) {
            // App color constants are always LE (e.g., TFT_BLUE=0x001F).
            // Sprite fillRect overrides and writes to _img directly, never calls pushBlock.
            // SDL buffer expects LE, so write as-is.
            uint16_t writeColor = color;
            if (sdl_tft_y >= 0 && sdl_tft_y < 240 && sdl_tft_x >= 0 && sdl_tft_x < 320) {
                sdl_tft_buffer[sdl_tft_y * 320 + sdl_tft_x] = writeColor;
            }
            sdl_tft_x++;
        }
        len -= toWrite;
        
        if (len > 0) {
            sdl_tft_x = sdl_win_x;
            sdl_tft_y++;
        }
    }
}

// ============================================================
// pushPixels — write a stream of pixel data (used by pushImage, etc.)
// ============================================================

void TFT_eSPI::pushPixels(const void* data_in, uint32_t len)
{
    if (!sdl_tft_buffer || !data_in) return;
    
    sdl_tft_x = sdl_win_x;
    sdl_tft_y = sdl_win_y;
    
    const uint16_t* data = (const uint16_t*)data_in;
    
    while (len > 0) {
        if (sdl_tft_y < sdl_win_y || sdl_tft_y >= sdl_win_y + sdl_win_h) {
            len = 0;
            break;
        }
        
        int32_t rowEnd = sdl_win_x + sdl_win_w;
        int32_t rowPixels = rowEnd - std::max(sdl_tft_x, sdl_win_x);
        
        if (rowPixels <= 0) {
            sdl_tft_x = sdl_win_x;
            sdl_tft_y++;
            continue;
        }
        
        uint32_t toWrite = std::min((uint32_t)rowPixels, len);
        
        for (uint32_t i = 0; i < toWrite; i++) {
            uint16_t pixel = !_swapBytes ? ((data[i] >> 8) | (data[i] << 8)) : data[i];
            if (sdl_tft_y >= 0 && sdl_tft_y < 240 && sdl_tft_x >= 0 && sdl_tft_x < 320) {
                sdl_tft_buffer[sdl_tft_y * 320 + sdl_tft_x] = pixel;
            }
            sdl_tft_x++;
        }
        data += toWrite;
        len -= toWrite;

        if (len > 0) {
            sdl_tft_x = sdl_win_x;
            sdl_tft_y++;
        }
    }
}

// ============================================================
// pushSwapBytePixels — write pixels with byte swapping (used by some sprite ops)
// ============================================================

void TFT_eSPI::pushSwapBytePixels(const void* data_in, uint32_t len)
{
    if (!sdl_tft_buffer || !data_in) return;

    sdl_tft_x = sdl_win_x;
    sdl_tft_y = sdl_win_y;

    const uint16_t* data = (const uint16_t*)data_in;

    while (len > 0) {
        if (sdl_tft_y < sdl_win_y || sdl_tft_y >= sdl_win_y + sdl_win_h) {
            len = 0;
            break;
        }

        int32_t rowEnd = sdl_win_x + sdl_win_w;
        int32_t rowPixels = rowEnd - std::max(sdl_tft_x, sdl_win_x);

        if (rowPixels <= 0) { sdl_tft_x = sdl_win_x; sdl_tft_y++; continue; }

        uint32_t toWrite = std::min((uint32_t)rowPixels, len);

        for (uint32_t i = 0; i < toWrite; i++) {
            uint16_t pixel = (data[i] >> 8) | (data[i] << 8);
            if (sdl_tft_y >= 0 && sdl_tft_y < 240 && sdl_tft_x >= 0 && sdl_tft_x < 320) {
                sdl_tft_buffer[sdl_tft_y * 320 + sdl_tft_x] = pixel;
            }
            sdl_tft_x++;
        }
        data += toWrite;
        len -= toWrite;

        if (len > 0) { sdl_tft_x = sdl_win_x; sdl_tft_y++; }
    }
}

// ============================================================
// DMA stubs — not used on Linux, must match header signatures
// ============================================================

bool TFT_eSPI::initDMA(bool ctrl_cs) { (void)ctrl_cs; return false; }
void TFT_eSPI::deInitDMA(void) {}
bool TFT_eSPI::dmaBusy(void) { return false; }
void TFT_eSPI::pushPixelsDMA(uint16_t* image, uint32_t len) { pushPixels(image, len); }
void TFT_eSPI::pushImageDMA(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* data, uint16_t* buffer) {
    (void)buffer; // Double-buffering not supported on SDL, just push directly
    pushImage(x, y, w, h, data);
}

// ============================================================
// readByte / busDir / gpioMode — parallel bus stubs (not used)
// ============================================================

uint8_t TFT_eSPI::readByte(void) { return 0; }
void TFT_eSPI::busDir(uint32_t mask, uint8_t writeMode) { (void)mask; (void)writeMode; }
void TFT_eSPI::gpioMode(uint8_t gpio, uint8_t mode) { (void)gpio; (void)mode; }
