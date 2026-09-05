/*
 * PEPitCompat — HTTPClient Shim (stubbed, no real network on Linux)
 * 
 * All operations are no-op stubs returning OK. No data available on Linux.
 */

#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <cstdint>
#include <cstddef>
#include "Arduino.h"

// HTTP methods (from ArduinoHttpClient)
#define HTTP_GET 1
#define HTTP_POST 2

// Redirect mode (from HTTPClient)
#define HTTPC_DISABLE_FOLLOW_REDIRECTS 0
#define HTTPC_STRICT_FOLLOW_REDIRECTS 1

// WiFiClient stub (used by HTTPClient internally)
class WiFiClient {
public:
    bool connect(const char* host, uint16_t port) { return true; } // OK
    void stop() {}
    int available() { return 0; } // No data
    uint8_t read() { return 0; }
    size_t read(uint8_t* buf, size_t size) { return 0; }
    size_t readBytes(char* buffer, size_t length) { return 0; } // Used by downloadFile
    size_t readBytes(uint8_t* buffer, size_t length) { return 0; } // Overload for uint8_t
    size_t write(uint8_t data) { return 1; } // OK
    bool connected() { return false; }
};

// HTTPClient stub class — returns OK, no actual data
class HTTPClient {
    String url_;
    WiFiClient stream_;
public:
    void begin(const String& url) { url_ = url; } // OK, stores URL
    int GET() { return 200; } // OK — no error, but stream is empty
    int POST(const String& payload) { return 200; } // OK
    int32_t getSize() { return 0; } // No content, but no error
    WiFiClient* getStreamPtr() { return &stream_; } // Returns empty stream (available=0)
    void end() {} // OK, no-op cleanup
    void setFollowRedirects(int mode) {} // OK, no-op
};

#endif // HTTPCLIENT_H
