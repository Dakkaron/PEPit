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
#include <deque>

// Global config for keyboard keys (set from CLI in main.cpp, defaults here)
static int lowPressureKey_ = SDL_SCANCODE_A;   // Default: 'A' key
static int goodPressureKey_ = SDL_SCANCODE_S;  // Default: 'S' key

// Current target pressure for calibration (set by shim when game config is loaded)
static int32_t g_targetPressure = 0;

// Whether we're in init phase (before first gameplay reading)
static bool g_sensorInitialized = false;

// Test-only: scripted readChannel() values (see Adafruit_HX711::testQueueRead)
static std::deque<float> g_testReads;

// MCP / control-file injected pressure override (Phase 2: MCP server control channel)
// Value semantics: 0 = idle, ~32.5 = ~50% of target, ~65 = ~100% of target
static bool g_pressureOverrideActive = false;
static float g_pressureOverrideValue = 0.0f;

// CLI key configuration setters (called from main.cpp after parsing --low-pressure-key / --good-pressure-key)
void setPressureSensorKeys(int lowKey, int goodKey) {
    lowPressureKey_ = lowKey;
    goodPressureKey_ = goodKey;
}

// MCP control-file injected pressure override setters
void setPressureOverride(bool active, float value) {
    g_pressureOverrideActive = active;
    g_pressureOverrideValue = value;
}

void clearPressureOverride() {
    g_pressureOverrideActive = false;
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

    // The firmware computes percent = readChannel / (65 * targetPressure)
    // (PRESSURE_SENSOR_DIVISOR=65, see readPressure), i.e. a 100% blow must
    // yield readChannel = 100 * 65 * targetPressure.

    // MCP / control-file injected pressure takes precedence over keyboard state.
    // The value is the raw (post-tare) readChannel directly in sensor units —
    // NOT a percent of target. The firmware computes
    //   percent = readChannel / (65 * taskTargetStrength)
    // so callers (the MCP server) convert their percent-of-target scale here,
    // e.g. 100% of a 30 target = 195000. Deliberately independent of
    // g_targetPressure, which the unmodified firmware never sets.
    if (g_pressureOverrideActive) {
        return tareOffset_ + (long)g_pressureOverrideValue;
    }

    // After init: keyboard-driven raw values

    if (g_targetPressure <= 0) {
        // No target set yet, return tare so readChannel = 0
        return tareOffset_;
    }

    bool lowPressed = keystate[lowPressureKey_];
    bool goodPressed = keystate[goodPressureKey_];

    if (goodPressed) {
        // ~100% of target pressure
        long desiredReadChannel = (long)(100.0f * 65.0f * g_targetPressure);
        return tareOffset_ + desiredReadChannel;
    } else if (lowPressed) {
        // ~50% of target pressure
        long desiredReadChannel = (long)(100.0f * 32.5f * g_targetPressure);
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
    if (!g_testReads.empty()) {
        float v = g_testReads.front();
        g_testReads.pop_front();
        return v;
    }
    long raw = readChannelRaw(gain);
    return (float)(raw - tareOffset_);
}

void Adafruit_HX711::powerDown(bool down) {
    // No-op on Linux — keyboard always available
}

void Adafruit_HX711::setTargetPressure(int32_t pressure) {
    g_targetPressure = pressure;
}

void Adafruit_HX711::testQueueRead(float value) {
    g_testReads.push_back(value);
}

void Adafruit_HX711::testClearQueuedReads() {
    g_testReads.clear();
}

bool Adafruit_HX711::testHasQueuedReads() {
    return !g_testReads.empty();
}
