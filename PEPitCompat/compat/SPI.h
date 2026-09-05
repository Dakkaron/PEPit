/* PEPitCompat — SPI Shim (stub, not used on Linux) */
#ifndef SPI_H
#define SPI_H

#include <cstdint>
#include <cstddef>

class SPIClass {
public:
    void begin() {}
    void begin(int sclk, int miso, int mosi) {}  // ESP32 variant with pin args
    void begin(int sclk, int miso, int mosi, int ss) {}  // ESP32 variant with SS pin
    void end() {}
    uint8_t transfer(uint8_t data) { return 0; }
    void transfer(uint8_t* buf, size_t count) {}
    uint16_t transfer16(uint16_t data) { return 0; }
    void pins(int sclk, int miso, int mosi, int ss) {}
    void setHwCs(bool) {}
    void setFrequency(uint32_t) {}
    
    struct Transaction { uint32_t dummy; };
    static constexpr Transaction SPISettings(uint32_t clock, uint8_t bitOrder, uint8_t dataMode) { return {}; }
    void beginTransaction(Transaction) {}
    void endTransaction() {}
};

extern SPIClass SPI;

#endif // SPI_H
