/*
 * PEPitCompat — NimBLE Device Shim (stub, no real Bluetooth on Linux)
 * 
 * All BLE operations are no-op stubs returning OK.
 * Defines CONFIG_BT_ENABLED and CONFIG_BLUEDROID_ENABLED so the original code's
 * compile-time check doesn't trigger #error.
 */

#ifndef NIMBLEDEVICE_H
#define NIMBLEDEVICE_H

#include <cstdint>

// Required by bluetoothHandler.cpp compile-time check
#define CONFIG_BT_ENABLED 1
#define CONFIG_BLUEDROID_ENABLED 1

// Stub classes for NimBLE API (used by bluetoothHandler)
class NimBLEAdvertising {
public:
    void setName(const char* name) {} // OK, no-op
};

class NimBLEServer {
public:
    // Empty class — server operations are stubbed
};

class NimBLEDevice {
public:
    static void init(const char* name) {} // OK, no-op
    static NimBLEAdvertising* getAdvertising() {
        static NimBLEAdvertising adv;
        return &adv;
    }
    static void setSecurityPasskey(uint32_t passkey) {} // OK, no-op
    static NimBLEServer* getServer() {
        static NimBLEServer server;
        return &server;
    }
    static void deinit(bool reset) {} // OK, no-op
};

#endif // NIMBLEDEVICE_H
