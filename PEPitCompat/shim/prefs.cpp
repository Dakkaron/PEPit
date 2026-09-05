/*
 * PEPitCompat — Preferences Shim (file-based key-value storage)
 * 
 * Replaces ESP32 NVS flash with text files on disk.
 * Each namespace is a separate file in the data directory.
 * Format: key:type:value (one per line)
 */

#include "Preferences.h"
#include "SD_MMC.h"
#include "Arduino.h"  // For String class
#include <cstdio>
#include <cstring>
#include <cmath>

// ============================================================
// Base64 Encode/Decode (for binary blob storage)
// ============================================================

static const char base64Chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string Preferences::base64Encode(const uint8_t* data, size_t len) {
    std::string result;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t b3 = (i + 0 < len ? data[i + 0] : 0) << 16 |
                      (i + 1 < len ? data[i + 1] : 0) << 8 |
                      (i + 2 < len ? data[i + 2] : 0);
        result += base64Chars[(b3 >> 18) & 0x3F];
        result += base64Chars[(b3 >> 12) & 0x3F];
        result += (i + 1 < len ? base64Chars[(b3 >> 6) & 0x3F] : '=');
        result += (i + 2 < len ? base64Chars[b3 & 0x3F] : '=');
    }
    return result;
}

void Preferences::base64Decode(const std::string& input, uint8_t* output, size_t& outLen) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    
    size_t outPos = 0;
    for (size_t i = 0; i < input.size(); i += 4) {
        int b4[4] = {-1, -1, -1, -1};
        for (int j = 0; j < 4 && i + j < input.size(); j++) {
            b4[j] = val(input[i + j]);
        }
        
        uint32_t b3 = (b4[0] << 18) | (b4[1] << 12) | (b4[2] << 6) | b4[3];
        
        if (outPos < outLen) output[outPos++] = (b3 >> 16) & 0xFF;
        if (outPos < outLen && b4[2] != -1) output[outPos++] = (b3 >> 8) & 0xFF;
        if (outPos < outLen && b4[3] != -1) output[outPos++] = b3 & 0xFF;
    }
    outLen = outPos;
}

// ============================================================
// File I/O (simple text format: key:type:value)
// ============================================================

bool Preferences::loadFile() {
    std::string filePath = SD_MMC.getRootDir() + "prefs_" + namespace_ + ".dat";
    
    FILE* fp = fopen(filePath.c_str(), "r");
    if (!fp) return false;  // File doesn't exist, start with empty data
    
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
        // Remove trailing newline/carriage return
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
            line[--len] = '\0';
        }
        if (len == 0) continue;  // Skip empty lines
        
        // Parse key:type:value format
        char* colon1 = strchr(line, ':');
        if (!colon1) continue;
        
        *colon1 = '\0';
        char* typeStart = colon1 + 1;
        char* colon2 = strchr(typeStart, ':');
        if (!colon2) continue;
        
        *colon2 = '\0';
        char* valueStart = colon2 + 1;
        
        std::string key(line);
        std::string type(typeStart);
        std::string value(valueStart);
        
        Preferences::JsonValue val;
        val.type = type;
        val.data = value;

        data_[key] = val;
    }
    
    fclose(fp);
    return true;
}

void Preferences::saveFile() const {
    std::string filePath = SD_MMC.getRootDir() + "prefs_" + namespace_ + ".dat";
    
    FILE* fp = fopen(filePath.c_str(), "w");
    if (!fp) return;  // Can't write, skip silently
    
    for (const auto& [key, val] : data_) {
        fprintf(fp, "%s:%s:%s\n", key.c_str(), val.type.c_str(), val.data.c_str());
    }
    
    fclose(fp);
}

// ============================================================
// Preferences Class Implementation
// ============================================================

Preferences::Preferences() {}

bool Preferences::begin(const char* name, bool readonly) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    namespace_ = name;
    readonly_ = readonly;
    data_.clear();  // Clear old data before loading
    
    isOpen_ = loadFile();
    
    return isOpen_;
}

void Preferences::end() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!readonly_) {
        saveFile();
    }
    
    isOpen_ = false;
    namespace_.clear();
}

bool Preferences::isKey(const char* key) const {
    return data_.count(key) > 0;
}

// Helper to look up a value by key (thread-safe, returns empty if missing)
static Preferences::JsonValue lookup(const std::map<std::string, Preferences::JsonValue>& data, const char* key) {
    auto it = data.find(key);
    if (it != data.end()) return it->second;
    return Preferences::JsonValue();  // Empty value if key not found
}

// Getters (return default if key missing)
uint32_t Preferences::getUInt(const char* key, uint32_t def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return def;
    try { return std::stoul(val.data); } catch (...) { return def; }
}

int32_t Preferences::getInt(const char* key, int32_t def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return def;
    try { return std::stoi(val.data); } catch (...) { return def; }
}

uint16_t Preferences::getUShort(const char* key, uint16_t def) const {
    return static_cast<uint16_t>(getUInt(key, def));
}

int16_t Preferences::getShort(const char* key, int16_t def) const {
    return static_cast<int16_t>(getInt(key, def));
}

uint64_t Preferences::getULong64(const char* key, uint64_t def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return def;
    try { return std::stoull(val.data); } catch (...) { return def; }
}

int64_t Preferences::getLong64(const char* key, int64_t def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return def;
    try { return std::stoll(val.data); } catch (...) { return def; }
}

uint32_t Preferences::getULong(const char* key, uint32_t def) const {
    return getUInt(key, def);
}

int32_t Preferences::getLong(const char* key, int32_t def) const {
    return getInt(key, def);
}

uint8_t Preferences::getUChar(const char* key, uint8_t def) const {
    return static_cast<uint8_t>(getUInt(key, def));
}

int8_t Preferences::getChar(const char* key, int8_t def) const {
    return static_cast<int8_t>(getInt(key, def));
}

size_t Preferences::getString(const char* key, char* buf, size_t maxlen) const {
    auto val = lookup(data_, key);
    if (val.data.empty() || !buf || maxlen == 0) return 0;

    size_t len = val.data.size();
    if (len >= maxlen) len = maxlen - 1;
    memcpy(buf, val.data.c_str(), len);
    buf[len] = '\0';

    return len;
}

String Preferences::getString(const char* key, const char* def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return String(def ? def : "");
    return String(val.data.c_str());
}

float Preferences::getFloat(const char* key, float def) const {
    auto val = lookup(data_, key);
    if (val.data.empty()) return def;
    try { return std::stof(val.data); } catch (...) { return def; }
}

size_t Preferences::getBytesLength(const char* key) const {
    auto val = lookup(data_, key);
    if (val.type != "blob" || val.data.empty()) return 0;
    
    // Calculate decoded length from base64 string
    size_t b64len = val.data.size();
    size_t padding = 0;
    if (b64len > 0 && val.data[b64len-1] == '=') padding++;
    if (b64len > 1 && val.data[b64len-2] == '=') padding++;
    
    return (b64len * 3 / 4) - padding;
}

size_t Preferences::getBytes(const char* key, uint8_t* buf, size_t maxlen) const {
    auto val = lookup(data_, key);
    if (val.type != "blob" || val.data.empty() || !buf) return 0;
    
    size_t outLen = maxlen;
    base64Decode(val.data, buf, outLen);
    
    return outLen;
}

// Setters (persist immediately to disk)
void Preferences::putUInt(const char* key, uint32_t value) {
    Preferences::JsonValue val;
    val.type = "uint";
    val.data = std::to_string(value);
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putInt(const char* key, int32_t value) {
    Preferences::JsonValue val;
    val.type = "int";
    val.data = std::to_string(value);
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putUShort(const char* key, uint16_t value) {
    putUInt(key, value);  // Store as uint for simplicity, saveFile called in putUInt
}

void Preferences::putShort(const char* key, int16_t value) {
    putInt(key, value);   // Store as int for simplicity, saveFile called in putInt
}

void Preferences::putULong64(const char* key, uint64_t value) {
    Preferences::JsonValue val;
    val.type = "ulong64";
    val.data = std::to_string(value);
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putLong64(const char* key, int64_t value) {
    Preferences::JsonValue val;
    val.type = "long64";
    val.data = std::to_string(value);
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putUChar(const char* key, uint8_t value) {
    putUInt(key, value);  // Store as uint for simplicity, saveFile called in putUInt
}

void Preferences::putChar(const char* key, int8_t value) {
    putInt(key, value);   // Store as int for simplicity, saveFile called in putInt
}

void Preferences::putFloat(const char* key, float value) {
    Preferences::JsonValue val;
    val.type = "float";
    char buf[32];
    snprintf(buf, sizeof(buf), "%.6f", value);
    val.data = buf;
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putString(const char* key, const char* value) {
    Preferences::JsonValue val;
    val.type = "str";
    val.data = value ? value : "";
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::putBytes(const char* key, const uint8_t* buf, size_t len) {
    Preferences::JsonValue val;
    val.type = "blob";
    val.data = base64Encode(buf, len);
    data_[key] = val;
    const_cast<Preferences*>(this)->saveFile();
}

void Preferences::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    data_.clear();
    saveFile();
}

bool Preferences::remove(const char* key) {
    std::lock_guard<std::mutex> lock(mutex_);
    bool removed = data_.erase(key) > 0;
    if (removed) saveFile();
    return removed;
}

// Preload spoofed touch calibration data so the app skips calibration on first launch.
// Writes a "touch" key (16 bytes = 4 corners × 2 uint16_t) into the "system" namespace.
// Must be called after SD_MMC.setRootDir() but before setup().
void Preferences::preloadTouchCalibration() {
    // Calibration data: 4 corners, each with rawX and rawY (uint16_t).
    // Values map screen corners to the 0–4095 ADC range:
    //   (0,240) bottom-left  → raw(100, 3900)
    //   (0,0)   top-left     → raw(100, 100)
    //   (320,0) top-right    → raw(3900, 100)
    //   (320,240) bottom-right → raw(3900, 3900)
    uint8_t calibData[16];
    uint16_t* corners = reinterpret_cast<uint16_t*>(calibData);
    corners[0 * 2 + 0] = 100;   corners[0 * 2 + 1] = 3900;  // bottom-left
    corners[1 * 2 + 0] = 100;   corners[1 * 2 + 1] = 100;   // top-left
    corners[2 * 2 + 0] = 3900;  corners[2 * 2 + 1] = 100;   // top-right
    corners[3 * 2 + 0] = 3900;  corners[3 * 2 + 1] = 3900;  // bottom-right

    Preferences tempPrefs;
    tempPrefs.begin("system");
    if (tempPrefs.isKey("touch")) {
        // Already calibrated from a previous session, no need to overwrite.
        tempPrefs.end();
        return;
    }
    tempPrefs.putBytes("touch", calibData, sizeof(calibData));
    tempPrefs.end();
}
