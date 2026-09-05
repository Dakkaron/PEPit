/* PEPitCompat — User Setup for Linux/SDL2 */
/* Mirrors the original project's TFT_eSPI/User_Setup.h for ESP32 hardware. */

#ifndef USER_SETUP_H
#define USER_SETUP_H

#define USER_SETUP_INFO "PEPitCompat SDL2"

// Driver: original project uses ILI9341
#define ILI9341_DRIVER

// No hardware pins needed for SDL2 backend (processor header handles rendering)
#define TFT_MOSI  -1
#define TFT_MISO  -1
#define TFT_SCLK  -1
#define TFT_CS    -1
#define TFT_DC    -1
#define TFT_RST   -1

// Fonts (match original project)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF   // FreeFonts + custom fonts (MyFont.h uses GFXglyph/GFXfont)
#define SMOOTH_FONT

// SPI frequency (irrelevant for SDL2, but needed by library)
#define SPI_FREQUENCY  27000000
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

#endif // USER_SETUP_H
