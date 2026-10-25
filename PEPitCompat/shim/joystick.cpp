/*
 * PEPitCompat — Joystick Shim (keyboard-mocked)
 * 
 * Maps keyboard input to M5UnitJoystick2 API:
 * - Arrow keys / WASD → X/Y axis (16-bit ADC values, center=32768)
 * - Space / Enter → button press (returns 0 = pressed)
 */

#include "m5_unit_joystick2.hpp"
#include "Wire.h"
#include <SDL2/SDL.h>

// Input control processing — shim/touch.cpp (MCP /tmp/pepit_control + legacy /tmp/pepit_touch IPC)
extern void checkSimulatedInput();

// Joystick axis sensitivity: how far from center (0-32768 range)
static const uint16_t JOYSTICK_AXIS_MAX = 32768; // Full deflection from center

// MCP / control-file injected joystick override state
static bool g_joystickOverrideActive = false;
static uint16_t g_joystickOverrideAdcX = 32768;
static uint16_t g_joystickOverrideAdcY = 32768;
static bool g_joystickOverrideBtnPressed = false;

// MCP control-file joystick override setters
void setJoystickOverride(bool active, uint16_t adcX, uint16_t adcY, bool btnPressed) {
    g_joystickOverrideActive = active;
    g_joystickOverrideAdcX = adcX;
    g_joystickOverrideAdcY = adcY;
    g_joystickOverrideBtnPressed = btnPressed;
}

void clearJoystickOverride() {
    g_joystickOverrideActive = false;
}

bool M5UnitJoystick2::begin(TwoWire* wire, uint8_t addr, int sda, int scl) {
    fprintf(stderr, "[PEPitCompat] MARK_JOYSTICK_BEGIN\n");
    // On Linux, keyboard joystick is always "present"
    return true;
}

void M5UnitJoystick2::set_rgb_color(uint32_t rgb) {
    // No-op on Linux — no physical LED
}

void M5UnitJoystick2::get_joy_adc_16bits_value_xy(uint16_t* x, uint16_t* y) {
    // MCP / control-file injected joystick override takes precedence
    if (g_joystickOverrideActive) {
        if (x) *x = g_joystickOverrideAdcX;
        if (y) *y = g_joystickOverrideAdcY;
        return;
    }

    const Uint8* keystate = SDL_GetKeyboardState(nullptr);

    // Default: center position
    uint16_t adcX = 32768;
    uint16_t adcY = 32768;

    // NOTE: joystickHandler.cpp cross-wires axes:
    //   joystickX = (adc_y - 32768) * factor
    //   joystickY = -(adc_x - 32768) * factor
    // We compensate by mapping arrow keys to the opposite ADC axis.

    bool left = keystate[SDL_SCANCODE_LEFT] || keystate[SDL_SCANCODE_A];
    bool right = keystate[SDL_SCANCODE_RIGHT] || keystate[SDL_SCANCODE_D];
    bool up = keystate[SDL_SCANCODE_UP] || keystate[SDL_SCANCODE_W];
    bool down = keystate[SDL_SCANCODE_DOWN] || keystate[SDL_SCANCODE_S];

    // Left/Right → adcY (which joystickHandler maps to joystickX)
    if (left && !right) {
        adcY = 0; // → negative joystickX (left)
    } else if (right && !left) {
        adcY = 65535; // → positive joystickX (right)
    }

    // Up/Down → adcX (which joystickHandler maps to inverted joystickY)
    if (up && !down) {
        adcX = 65535; // → negative joystickY (up)
    } else if (down && !up) {
        adcX = 0; // → positive joystickY (down)
    }

    if (x) *x = adcX;
    if (y) *y = adcY;
}

uint16_t M5UnitJoystick2::get_button_value() {
    // Poll MCP / control input once per joystick tick (fires from doSystemTasks in every screen loop)
    checkSimulatedInput();

    // MCP / control-file injected joystick button override takes precedence
    if (g_joystickOverrideActive) {
        return g_joystickOverrideBtnPressed ? 0 : 1; // 0 = pressed, non-zero = released
    }

    const Uint8* keystate = SDL_GetKeyboardState(nullptr);
    
    // Space or Enter pressed → button value 0 (pressed)
    bool pressed = keystate[SDL_SCANCODE_SPACE] || keystate[SDL_SCANCODE_RETURN];
    return pressed ? 0 : 1; // 0 = pressed, non-zero = released
}
