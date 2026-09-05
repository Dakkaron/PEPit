/*
 * PEPitCompat — Adafruit HX711 Pressure Sensor Shim (keyboard-mocked)
 * 
 * Replaces physical pressure sensor with keyboard input:
 * - No key pressed = 0 pressure (not blowing)
 * - Low-pressure key ('A' by default) = ~50% of target pressure
 * - Good-pressure key ('S' by default) = ~100% of target pressure
 * 
 * Keyboard keys are configurable via --low-pressure-key and --good-pressure-key CLI params.
 */

#ifndef ADAFRUIT_HX711_H
#define ADAFRUIT_HX711_H

#include <cstdint>

// HX711 channel/gain constants (from original library)
#define CHAN_A_GAIN_64 0

class Adafruit_HX711 {
public:
    // Constructor takes data and clock pin numbers (ignored on Linux)
    Adafruit_HX711(uint8_t dataPin, uint8_t clockPin);

    // Initialize sensor (no-op on Linux, keyboard input always ready)
    void begin();

    // Check if sensor is busy (always false on Linux — keyboard always ready)
    bool isBusy() { return false; }

    // Read raw ADC value from channel A
    // Returns calibrated raw value based on keyboard input and current target pressure
    long readChannelRaw(uint8_t gain = CHAN_A_GAIN_64);

    // Tare channel A with given raw value (stores tare offset)
    void tareA(long rawValue);

    // Read calibrated channel value (float, post-tare)
    // Returns value that, when divided by (PRESSURE_SENSOR_DIVISOR * targetPressure),
    // gives the correct pressure percentage based on keyboard input
    float readChannel(uint8_t gain = CHAN_A_GAIN_64);

    // Power down sensor (no-op on Linux)
    void powerDown(bool down = true);

    // Set the current target pressure for keyboard simulation
    // Called by the shim when targetPressure is known from game config
    static void setTargetPressure(int32_t pressure);

private:
    long tareOffset_;
};

#endif // ADAFRUIT_HX711_H
