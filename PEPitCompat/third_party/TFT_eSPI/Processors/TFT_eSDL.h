/*
 * PEPitCompat — TFT_eSPI SDL2 Processor Header
 * 
 * Replaces ESP32 SPI processor with direct pixel buffer writes to SDL2.
 * All SPI transactions become no-ops; pixels go straight to an offscreen buffer.
 */

#ifndef TFT_eSDL_H
#define TFT_eSDL_H

#include <cstdint>
#include <algorithm>
#include <cstddef>

// ============================================================
// Forward declarations for our SDL backend
// ============================================================

extern uint16_t* sdl_tft_buffer;       // 320x240 pixel buffer (RGB565, big-endian)
extern int32_t  sdl_tft_x;             // Current x position in buffer
extern int32_t  sdl_tft_y;             // Current y position

// Window bounds — set by tracking CASET/PASET commands via tft_Write_* macros
extern int32_t  sdl_win_x;             // Window start x
extern int32_t  sdl_win_y;             // Window start y
extern int32_t  sdl_win_w;             // Window width
extern int32_t  sdl_win_h;             // Height

// Command tracking state
extern uint8_t   sdl_track_cmd;        // Pending command (0x2A=CASET, 0x2B=PASET)
extern uint8_t   sdl_track_idx;        // Data byte index (0 or 1 for 16-bit coord)
extern uint16_t  sdl_track_acc;        // Accumulator for byte-level tracking

#include <SDL2/SDL.h>

void initSDLBuffer(void);

// extern "C" for linkage with display.cpp
extern "C" {
    void setSDLWindow(SDL_Window* window, SDL_Renderer* renderer);
    void flushSDL(void);
    void updateSDL(void);
} // extern "C"

// Tracking: when writecommand(0x2A/0x2B) is called via tft_Write_16Raw,
// we set sdl_track_cmd. Then subsequent tft_Write_16 calls from writedata
// carry the coordinate bytes which we accumulate.
inline void sdl_track_cmd_raw(uint8_t c) {
    if (c == 0x2A) { sdl_track_cmd = 0x2A; sdl_track_idx = 0; }
    else if (c == 0x2B) { sdl_track_cmd = 0x2B; sdl_track_idx = 0; }
    else if (c == 0x2C) { sdl_track_cmd = 0x2C; sdl_track_idx = 0; } // RAMWR — next tft_Write_16 is a pixel
    else sdl_track_cmd = 0;
}

// Called by tft_Write_16 — accumulates bytes into 16-bit coords, then stores window bounds
inline void sdl_track_data16(uint8_t byte) {
    if (!sdl_track_cmd) return;
    // Accumulate 2 bytes into a 16-bit value (little-endian: low byte first)
    if (sdl_track_idx == 0) sdl_track_acc = byte;
    else {
        sdl_track_acc |= (byte << 8);
        if (sdl_track_cmd == 0x2A) { // CASET data
            if (!sdl_track_idx) { /* first byte of x0 */ }
        }
    }
}

// Simplified tracking: writedata sends each 16-bit coord as 2 separate tft_Write_16 calls.
// Each tft_Write_16 call sends the value as-is (the library splits it internally).
// Actually, writedata calls tft_Write_16 with the full 16-bit value.
inline void sdl_track16(uint16_t value) {
    if (sdl_track_cmd == 0x2A) { // CASET data — x coordinates
        if (sdl_track_idx == 0) { sdl_win_x = value; }
        else { sdl_win_w = value - sdl_win_x + 1; sdl_track_cmd = 0; }
        sdl_track_idx ^= 1;
    } else if (sdl_track_cmd == 0x2B) { // PASET data — y coordinates
        if (sdl_track_idx == 0) { sdl_win_y = value; }
        else { sdl_win_h = value - sdl_win_y + 1; sdl_track_cmd = 0; }
        sdl_track_idx ^= 1;
    }
}

// For 24-bit writes: command byte + data. Track if it's CASET/PASET.
inline void sdl_track24(uint32_t value) {
    uint8_t cmd = (value >> 16) & 0xFF;
    if (cmd == 0x2A) { sdl_track_cmd = 0x2A; sdl_track_idx = 0; }
    else if (cmd == 0x2B) { sdl_track_cmd = 0x2B; sdl_track_idx = 0; }
    else sdl_track_cmd = 0;
}

// ============================================================
// SPI transaction stubs — all no-ops for SDL backend
// ============================================================

inline void begin_tft_write(void) {}

// CS control — CS_H flushes pixel buffer to SDL window on endWrite/end_tft_write
#define CS_L   // No-op: SDL doesn't use chip select
#define CS_H   flushSDL()  // Flush pixel buffer to SDL on transaction end

// DC (Data/Command) control — no-op for SDL (we track via command values)
#define DC_C   // No-op: we detect commands from data, not a pin
#define DC_D   // No-op

// Bus mode — no-op for SDL (no bus switching)
#define SET_BUS_WRITE_MODE  // No-op
#define SET_BUS_READ_MODE   // No-op

// DMA busy check — no-op for SDL (no DMA)
#define DMA_BUSY_CHECK  // No-op

// SPI busy check — no-op for SDL
#define SPI_BUSY_CHECK  // No-op

// Reset control
inline void tft_init_8bit(void) {}
#define TFT_ESPI_CS_INIT()     // No CS pin needed
#define TFT_ESPI_CS_INIT_1()   // Extra CS pins not needed
#define TFT_ESPI_DC_INIT()     // No DC pin needed
#define TFT_ESPI_WR_INIT()     // No WR pin needed (parallel)
#define TFT_EINIT_MOSI()       // No SPI pins needed
#define TFT_EINIT_SCLK()
#define TFT_EINIT_RST()

// ============================================================
// SPI write functions — track commands, discard data (SDL writes directly)
// ============================================================

inline void tft_Write_16Raw(uint16_t data) { sdl_track_cmd_raw(data & 0xFF); }
inline void tft_Write_8Raw(uint8_t data) { sdl_track_cmd_raw(data); }

// tft_Write_8 — write single byte (used for commands)
inline void tft_Write_8(uint8_t data) { sdl_track_cmd_raw(data); }

// Use a small software buffer to batch writes (matches ESP32 pattern)
static uint16_t tft_write_buffer[48] __attribute__((aligned(4)));
static uint8_t  tft_write_index = 0;

inline void tft_Write_16(uint16_t data) {
    sdl_track16(data);  // Track for window state
    tft_write_buffer[tft_write_index++] = data;
    if (tft_write_index >= 48) tft_write_index = 0; // Soft flush (discard for SDL)
}

inline void tft_Write_32(uint32_t data) {
    tft_Write_16(data >> 8);
    tft_Write_16(data);
}

inline void tft_Write_24(uint32_t data) {
    sdl_track24(data);  // Track for window state (24-bit commands)
    tft_Write_16(data >> 8);
    tft_Write_16(data);
}

// tft_Write_32C — write command + first data word (used by setWindow: DC_C + cmd, then DC_D + data)
// Library calls: DC_C; tft_Write_32C(x0, x1) which sends 2x 16-bit coords as data
inline void tft_Write_32C(uint16_t param1, uint16_t param2) {
    sdl_track16(param1);
    sdl_track16(param2);
}

// tft_Write_32D — write single data value as 16-bit (used by drawPixel)
inline void tft_Write_32D(uint16_t value) {
    sdl_track16(value);
}

// tft_Write_16N — write single pixel color (used by pushColor)
inline void tft_Write_16N(uint16_t color) {
    (void)color; // Pixel written by pushBlock/pushPixels, not here
}

// ============================================================
// SPI read functions — return 0 (TFT reads not supported on SDL)
// ============================================================

inline uint8_t tft_Read_8(void) { return 0; }

// ============================================================
// Block write — for DMA / parallel (not used on SDL)
// ============================================================

inline void tft_Write_block(const uint8_t* /*data*/, uint32_t /*len*/) {}
inline void tft_Write_16n(uint16_t /*color*/, uint32_t /*len*/) {}
inline void tft_Write_16nk(uint16_t /*color*/, uint32_t /*len*/) {}
inline void tft_Write_8n(uint8_t /*data*/, uint32_t /*len*/) {}

// ============================================================
// Pixel write macro — writes directly to SDL buffer
// Called by library's drawPixel, fillRect (via pushBlock), etc.
// 
// The library's setWindow() sets internal addr_row/addr_col before drawing.
// pushBlock/pushPixels (in our .cpp) use sdl_win_* for row wrapping.
// This macro is used for single-pixel writes and auto-increments x.
// ============================================================

#define writeSDLPixel(color) \
    do { \
        if (sdl_tft_buffer && sdl_tft_y >= 0 && sdl_tft_y < 240) { \
            if (sdl_tft_x >= 0 && sdl_tft_x < 320) { \
                sdl_tft_buffer[sdl_tft_y * 320 + sdl_tft_x] = color; \
            } \
        } \
        sdl_tft_x++; \
    } while(0)

// ============================================================
// Processor ID and buffer init
// ============================================================

#define PROCESSOR_ID 0x5D2 // SDL2 processor
#define INIT_TFT_DATA_BUS initSDLBuffer()

// SPI instance alias — our stub SPIClass
extern class SPIClass SPI;
#define spi SPI

#endif // TFT_eSDL_H
