/*
 * PEPitCompat — Pressure Sensor Shim (keyboard-mocked)
 * 
 * Maps keyboard input to Adafruit_HX711 API:
 * - No key pressed = 0 pressure (not blowing)
 * - Low-pressure key ('A' by default) = ~50% of target pressure
 * - Good-pressure key ('S' by default) = ~100% of target pressure
 * 
 * Keyboard keys are configurable via global config (set from CLI params in main.cpp).
 */

#include "Adafruit_HX711.h"
#include <SDL2/SDL.h>
#include <cstdio>

// Global config for keyboard keys (set from CLI in main.cpp, defaults here)
static int lowPressureKey_ = SDL_SCANCODE_A;   // Default: 'A' key
static int goodPressureKey_ = SDL_SCANCODE_S;  // Default: 'S' key

// Current target pressure for calibration (set by shim when game config is loaded)
static int32_t g_targetPressure = 0;

// Whether we're in init phase (before first gameplay reading)
static bool g_sensorInitialized = false;

// CLI key configuration setters (called from main.cpp after parsing --low-pressure-key / --good-pressure-key)
void setPressureSensorKeys(int lowKey, int goodKey) {
    lowPressureKey_ = lowKey;
    goodPressureKey_ = goodKey;
}

Adafruit_HX711::Adafruit_HX711(uint8_t dataPin, uint8_t clockPin)
    : tareOffset_(0) {
    // Pins ignored on Linux — keyboard input used instead
}

void Adafruit_HX711::begin() {
    // No-op on Linux — keyboard input always available
}

long Adafruit_HX711::readChannelRaw(uint8_t gain) {
    const Uint8* keystate = SDL_GetKeyboardState(nullptr);

    if (!g_sensorInitialized) {
        // During init (tareA phase): return 0 so tare offset = 0
        return 0;
    }

    // After init: keyboard-driven raw values
    // We need readChannel = (raw - tareOffset) to produce correct pressure %
    // For 100%: readChannel should = targetPressure * 65 (PRESSURE_SENSOR_DIVISOR)
    // For 50%: readChannel should = targetPressure * 32.5
    // raw = tareOffset + desired_readChannel

    if (g_targetPressure <= 0) {
        // No target set yet, return tare so readChannel = 0
        return tareOffset_;
    }

    bool lowPressed = keystate[lowPressureKey_];
    bool goodPressed = keystate[goodPressureKey_];

    if (goodPressed) {
        // ~100% of target pressure: readChannel = targetPressure * 65
        long desiredReadChannel = (long)(g_targetPressure * 65);
        return tareOffset_ + desiredReadChannel;
    } else if (lowPressed) {
        // ~50% of target pressure: readChannel = targetPressure * 32.5
        long desiredReadChannel = (long)(g_targetPressure * 32.5);
        return tareOffset_ + desiredReadChannel;
    } else {
        // No key: 0 pressure, readChannel = 0
        return tareOffset_;
    }
}

void Adafruit_HX711::tareA(long rawValue) {
    tareOffset_ = rawValue;
    g_sensorInitialized = true; // After taring, switch to keyboard mode
}

float Adafruit_HX711::readChannel(uint8_t gain) {
    long raw = readChannelRaw(gain);
    return (float)(raw - tareOffset_);
}

void Adafruit_HX711::powerDown(bool down) {
    // No-op on Linux — keyboard always available
}

void Adafruit_HX711::setTargetPressure(int32_t pressure) {
    g_targetPressure = pressure;
}
