/*
 * PEPitCompat — TFT_eSPI User Setup for SDL2/Linux target
 * 
 * This setup file configures the real TFT_eSPI library for our Linux/SDL2 bridge.
 * It is included by User_Setup_Select.h (which skips because USER_SETUP_LOADED is defined).
 */

#ifndef USER_SETUP_LOADED
#define USER_SETUP_LOADED

// ============================================================
// Display Driver: ST7789 (same as the LilyGo T-HMI)
// ============================================================
#define ST7789_DRIVER     // Enable ST7789 driver defines/init/rotation
#define TFT_WIDTH  240    // Display width in pixels
#define TFT_HEIGHT 320    // Display height in pixels

// Color order: BGR (matches the original T-HMI setup)
#define TFT_RGB_ORDER TFT_BGR

// No inversion on our SDL target (handled by code if needed)
#define TFT_INVERSION_OFF

// ============================================================
// DO NOT define TFT_PARALLEL_8_BIT — we use the SPI code path
// in Generic.c (pushBlock/pushPixels) but override with our
// custom processor macros to write to SDL buffer instead.
// ============================================================

// ============================================================
// Pins: not used on Linux (our processor overrides all I/O)
// Defined to satisfy library checks, values are irrelevant.
// ============================================================
#define TFT_CS   -1  // Chip select (not used — our processor writes to buffer)
#define TFT_DC   -1  // Data/Command pin (not used — our processor handles this)
#define TFT_RST  -1  // Reset pin (not used)

// SPI pins: not used on Linux, but must be defined for non-parallel mode
#define TFT_MOSI  -1
#define TFT_MISO  -1
#define TFT_SCLK  -1

// ============================================================
// Touch: not used (we have our own XPT2046 shim)
// ============================================================
// TOUCH_CS is intentionally NOT defined — disables Touch extension

// ============================================================
// Fonts: match the original T-HMI setup for pixel-perfect rendering
// ============================================================
#define LOAD_GLCD   // Font 1: Original Adafruit 5x8 font (~1820 bytes)
#define LOAD_FONT2  // Font 2: Small 16px high font (~3534 bytes)
#define LOAD_FONT4  // Font 4: Medium 26px high font (~5848 bytes)
#define LOAD_FONT6  // Font 6: Large 48px font (digits only)
#define LOAD_FONT7  // Font 7: 7-segment 48px font (digits only)
#define LOAD_FONT8  // Font 8: Large 75px font (digits only)
#define LOAD_GFXFF  // FreeFonts: Adafruit_GFX free fonts FF1-FF48 + custom fonts

// SMOOTH_FONT requires a filesystem (SPIFFS/LittleFS/SD) — not available on Linux
// The original code uses setFreeFont() with custom GFXfont structs, which works fine.

// ============================================================
// Our custom processor overrides all hardware I/O with SDL2 buffer writes
// ============================================================
#define PROCESSOR_INCLUDE "Processors/TFT_eSDL.h"

#endif // USER_SETUP_LOADED
