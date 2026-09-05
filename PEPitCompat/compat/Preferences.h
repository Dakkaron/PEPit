/*
 * PEPitCompat — Preferences Shim (JSON file-based key-value storage)
 * 
 * Replaces ESP32 NVS flash with JSON files on disk.
 * Each namespace is a separate JSON file in the data directory.
 */

#ifndef PREFERENCES_H
#define PREFERENCES_H

#include <cstdint>
#include <cstddef>
#include <map>
#include <mutex>
#include <string>

// Forward declare String (defined in Arduino.h)
class String;

class Preferences {
public:
    Preferences();
    
    // Open/close namespace (loads/saves JSON file)
    bool begin(const char* name, bool readonly = false);
    void end();
    
    // Check if key exists
    bool isKey(const char* key) const;
    
    // Getters (return default if key missing)
    uint32_t getUInt(const char* key, uint32_t def = 0) const;
    int32_t getInt(const char* key, int32_t def = 0) const;
    uint16_t getUShort(const char* key, uint16_t def = 0) const;
    int16_t getShort(const char* key, int16_t def = 0) const;
    uint64_t getULong64(const char* key, uint64_t def = 0) const;
    int64_t getLong64(const char* key, int64_t def = 0) const;
    uint32_t getULong(const char* key, uint32_t def = 0) const;
    int32_t getLong(const char* key, int32_t def = 0) const;
    uint8_t getUChar(const char* key, uint8_t def = 0) const;
    int8_t getChar(const char* key, int8_t def = 0) const;
    size_t getString(const char* key, char* buf, size_t maxlen) const;
    String getString(const char* key, const char* def = "") const;  // Returns String with default
    float getFloat(const char* key, float def = 0.0f) const;
    
    // Binary blob storage (base64-encoded in JSON)
    size_t getBytesLength(const char* key) const;
    size_t getBytes(const char* key, uint8_t* buf, size_t maxlen) const;
    
    // Setters (in-memory, saved on end())
    void putUInt(const char* key, uint32_t value);
    void putInt(const char* key, int32_t value);
    void putUShort(const char* key, uint16_t value);
    void putShort(const char* key, int16_t value);
    void putULong64(const char* key, uint64_t value);
    void putLong64(const char* key, int64_t value);
    void putUChar(const char* key, uint8_t value);
    void putChar(const char* key, int8_t value);
    void putFloat(const char* key, float value);
    void putString(const char* key, const char* value);
    void putBytes(const char* key, const uint8_t* buf, size_t len);
    
    // Namespace operations
    void clear(); // Remove all keys in current namespace
    bool remove(const char* key); // Remove specific key
    
    // System info (stubbed)
    uint32_t freeEntries() const { return 999; } // Not meaningful on Linux

public:
    // Preload spoofed touch calibration data (call before setup())
    static void preloadTouchCalibration();

    // Value storage (key -> {type, data})
    struct JsonValue {
        std::string type; // "uint", "int", "str", "blob" (base64)
        std::string data; // Raw string representation
    };

private:
    std::string namespace_;
    bool readonly_ = false;
    bool isOpen_ = false;

    std::mutex mutex_;
    std::map<std::string, JsonValue> data_;  // In-memory key-value store
    
    bool loadFile();
    void saveFile() const;
    
    // Base64 encode/decode for binary blobs
    static std::string base64Encode(const uint8_t* data, size_t len);
    static void base64Decode(const std::string& input, uint8_t* output, size_t& outLen);
};

#endif // PREFERENCES_H
