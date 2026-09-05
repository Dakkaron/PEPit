/*
 * PEPitCompat — M5Unit Joystick2 Shim (keyboard-mocked)
 * 
 * Replaces I2C joystick with keyboard input:
 * - Arrow keys / WASD for X/Y axis movement
 * - Space / Enter for button press
 * - begin() always returns true (joystick "present" on Linux)
 */

#ifndef M5_UNIT_JOYSTICK2_HPP
#define M5_UNIT_JOYSTICK2_HPP

#include <cstdint>

// I2C address for M5Stack Unit Joystick 2 (0x3E)
#define JOYSTICK2_ADDR 0x3E

// Forward declaration
class TwoWire;

class M5UnitJoystick2 {
public:
    // begin(TwoWire* wire, uint8_t i2c_addr, int sda_pin, int scl_pin)
    // On Linux: always returns true (keyboard joystick is "present")
    bool begin(TwoWire* wire, uint8_t addr, int sda, int scl);

    // Set RGB LED color (no-op on Linux)
    void set_rgb_color(uint32_t rgb);

    // Get 16-bit ADC values for X and Y axis
    // Maps arrow keys / WASD to joystick positions (0-65535, center=32768)
    void get_joy_adc_16bits_value_xy(uint16_t* x, uint16_t* y);

    // Get button value: returns 0 if pressed, non-zero if released
    // Maps Space / Enter key to button press
    uint16_t get_button_value();
};

#endif // M5_UNIT_JOYSTICK2_HPP
