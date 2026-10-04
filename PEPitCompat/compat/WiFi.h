/*
 * PEPitCompat — WiFi Shim (stubbed, no real network on Linux)
 * 
 * All WiFi operations are no-op stubs returning OK.
 * No WiFi available on Linux, so status indicates disconnected state.
 */

#ifndef WIFI_H
#define WIFI_H

#include <cstdint>
#include "Arduino.h"

// WiFi modes (from ESP-IDF)
#define WIFI_MODE_NULL 0x0
#define WIFI_MODE_STA  0x1
#define WIFI_MODE_AP   0x2
#define WIFI_MODE_APSTA 0x3

// Arduino WiFi mode aliases (used by original code)
#define WIFI_STA WIFI_MODE_STA
#define WIFI_AP  WIFI_MODE_AP
#define WIFI_APSTA WIFI_MODE_APSTA

// Arduino WiFi status codes
#define WL_NO_SHIELD    255
#define WL_IDLE_STATUS   0
#define WL_NO_SSID_AVAIL 1
#define WL_SCAN_COMPLETED 2
#define WL_CONNECTED     3
#define WL_CONNECT_FAILED 4
#define WL_CONNECTION_LOST 5
#define WL_DISCONNECTED  6

// WiFi connection states (used by wifiHandler)
#define WIFI_CONNECTION_SEARCHING 0
#define WIFI_CONNECTION_NOWIFI 1
#define WIFI_CONNECTION_OK 2

class WiFiClass {
public:
    int32_t scanNetworks() { return 0; }
    String SSID(int32_t networkIndex) { return ""; }
    void mode(uint8_t mode) {}
    bool begin(const char* ssid, const char* pass = nullptr) { return true; } // OK, but status stays disconnected
    bool begin(const String& ssid, const String& pass = String()) { return true; }
    uint8_t status() { return WL_NO_SSID_AVAIL; } // No WiFi available on Linux
    void disconnect(bool wifiphoff = false, bool aiphide = true) {}
    void sleep(bool enable) {}
};

extern WiFiClass WiFi;

#endif // WIFI_H
