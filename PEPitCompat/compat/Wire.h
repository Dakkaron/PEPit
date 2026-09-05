/* PEPitCompat — Wire (I2C) Shim (stub, no real I2C on Linux) */
#ifndef WIRE_H
#define WIRE_H

#include <cstdint>
#include <cstddef>

class TwoWire {
public:
    void begin() {} // OK, no-op
    void begin(uint8_t addr) {} // OK, no-op
    void begin(int addr) {} // OK, no-op
    void end() {} // OK, no-op
    uint8_t requestFrom(uint8_t address, size_t quantity) { return 0; } // OK, no data
    uint8_t requestFrom(int address, size_t quantity) { return 0; } // OK, no data
    void beginTransmission(uint8_t address) {} // OK, no-op
    void beginTransmission(int address) {} // OK, no-op
    uint8_t endTransmission(bool stop = true) { return 0; } // OK
    void write(uint8_t data) {} // OK, no-op
    void write(const uint8_t* data, size_t quantity) {} // OK, no-op
    int available() { return 0; } // No data
    uint8_t read() { return 0; }
};

extern TwoWire Wire;

#endif // WIRE_H
