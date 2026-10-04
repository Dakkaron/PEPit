/*
 * PEPitCompat — OTA Update Shim (stubbed, no real firmware updates on Linux)
 * 
 * All operations are no-op stubs returning OK. No actual update performed.
 */

#ifndef UPDATE_H
#define UPDATE_H

#include <cstdint>
#include "Arduino.h"

// Update partition flags (from ESP32)
#define U_FLASH 0
#define U_SPIFFS 1

// Update error codes (from ESP32)
#define UPDATE_ERROR_OK 0
#define UPDATE_ERROR_ABORT 1

// Forward declaration for writeStream
class File;

// Update class stub — OTA not supported on Linux, all OK
class UpdateClass {
public:
    bool begin(size_t size, int command = 0) { return true; } // OK
    size_t write(uint8_t data) { return 1; } // OK, no-op
    size_t write(const uint8_t* data, size_t len) { return len; } // OK, no-op
    bool end() { return false; } // Returns false to prevent ESP.restart() after update
    size_t writeStream(File& stream) { return 0; } // OK, no-op
    const char* errorString() { return "Linux stub — no update performed"; }

    // Static callback for progress reporting (no-op)
    static void onProgress(void (*fn)(int, int)) {}

    // Error getter (always returns OK on Linux)
    int errorCode() { return UPDATE_ERROR_OK; }
};

extern UpdateClass Update;

#endif // UPDATE_H
