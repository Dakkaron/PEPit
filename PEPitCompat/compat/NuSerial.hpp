/*
 * PEPitCompat — NuSerial Shim (Bluetooth serial stub)
 * 
 * Stub class for NimBLE serial communication.
 * isConnected() always returns false on Linux (use simulateTrampoline mode).
 */

#ifndef NUSERIAL_HPP
#define NUSERIAL_HPP

#include <cstdint>

class NuSerialClass {
public:
    bool isConnected() { return false; } // Never connected on Linux
    void begin(uint32_t baud) {} // No-op stub
    int available() { return 0; } // No data available
    uint8_t read() { return 0; } // Returns null byte (never called when !isConnected)
    void print(const char* s) {} // No-op (simulation mode bypasses this)
    void end() {} // No-op stub
};

extern NuSerialClass NuSerial;

#endif // NUSERIAL_HPP
