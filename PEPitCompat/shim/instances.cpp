/*
 * PEPitCompat — Global Instance Definitions & Stub Functions
 *
 * Defines global instances declared as 'extern' in compat headers.
 * Also implements stub functions referenced by original source code.
 */

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "WiFi.h"
#include "SPI.h"
#include "SD.h"
#include "NuSerial.hpp"
#include "Update.h"
#include "Wire.h"
#include "ESP32-targz.h"

// ============================================================
// Global instances (declared extern in compat headers)
// ============================================================

WiFiClass WiFi;
SPIClass SPI;
NuSerialClass NuSerial;
UpdateClass Update;
TwoWire Wire;
ESPClass ESP;

// ESP32-targz FS stub instance
FS tarGzFS;

// ============================================================
// ESP32-targz callback stubs (referenced by updateHandler)
// ============================================================

size_t targzTotalBytesFn(FS& fs) {
    return 0; // No meaningful total on Linux
}

size_t targzFreeBytesFn(FS& fs) {
    return 999999999; // Pretend plenty of space so unpacking doesn't fail
}

// ============================================================
// gfxHandler wrapper functions (7-arg versions for header compat)
// The original gfxHandler.hpp declares 7-arg drawButton/drawImageButton,
// but gfxHandler.cpp defines 8-arg versions (with textColor). These wrappers
// bridge the gap by providing the 7-arg signatures with default textColor.
// ============================================================

extern "C++" {
    void drawImageButton(TFT_eSprite* display, String path, int16_t x, int16_t y,
                         int16_t w, int16_t h, uint16_t color);
    void drawButton(TFT_eSprite* display, String text, int16_t x, int16_t y,
                    int16_t w, int16_t h, uint16_t color);
}

void drawImageButton(TFT_eSprite* display, String path, int16_t x, int16_t y,
                     int16_t w, int16_t h, uint16_t color) {
    // Draw a button with an image — stubbed on Linux (no BMP loading from SD)
    (void)display; (void)path; (void)x; (void)y; (void)w; (void)h; (void)color;
}

void drawButton(TFT_eSprite* display, String text, int16_t x, int16_t y,
                int16_t w, int16_t h, uint16_t color) {
    // Draw a text button — stubbed on Linux (basic rectangle + text)
    if (!display) return;
    display->fillRect(x, y, w, h, color);
    display->setTextDatum(CC_DATUM);
    display->setTextColor(TFT_WHITE);
    display->drawString(text, x + w / 2, y + h / 2);
}

// ============================================================
// ESP namespace (used by main.cpp: ESP.getFreeHeap(), ESP.restart())
// ============================================================

// ESP.restart() is already in arduino.cpp as esp_restart()
// ESP.getFreeHeap() needs to be available
