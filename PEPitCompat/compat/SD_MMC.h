/*
 * PEPitCompat — SD_MMC Shim (POSIX filesystem with configurable root directory)
 * 
 * Replaces ESP32 SD card operations with POSIX file I/O.
 * Root directory is configurable via --data-dir CLI parameter (default: ./data/).
 * 
 * Defined as a class with static inline members + global instance.
 * Original ESP32 code uses: SD_MMC.begin(), SD_MMC.open(), etc. (dot notation on object).
 * Our shim code uses: SD_MMC.getRootDir(), etc. (dot notation on object).
 */

#ifndef SD_MMC_H
#define SD_MMC_H

#include <cstdint>
#include <string>
#include "FS.h"
#include "Arduino.h"  // For String class (full definition, not just forward decl)

// SD_MMC class — mirrors ESP32 SD_MMC API
class SD_MMC_Class {
public:
    /**
     * Initialize filesystem with configurable root directory.
     */
    bool begin(const char* mountPath = "/sdcard", bool readOnly = false);

    /**
     * Clean up filesystem (no-op on Linux, kept for compatibility)
     */
    void end();

    /**
     * Set SD card pins (no-op on Linux, kept for compatibility)
     */
    void setPins(uint8_t sclk, uint8_t mosi, uint8_t miso);

    /**
     * Open a file for reading.
     */
    File open(const char* path) { return open(path, FILE_READ, false); }
    File open(const String& path) { return open(path.c_str()); }
    File open(const String& path, uint8_t mode, bool create = false) { return open(path.c_str(), mode, create); }

    /**
     * Open a file with specified mode.
     */
    File open(const char* path, uint8_t mode, bool create = false);

    /**
     * Check if a file or directory exists.
     */
    bool exists(const char* path);
    bool exists(const String& path) { return exists(path.c_str()); }

    /**
     * Remove a file.
     */
    bool remove(const char* path);
    bool remove(const String& path) { return remove(path.c_str()); }

    /**
     * Rename a file or directory.
     */
    bool rename(const char* oldPath, const char* newPath);
    bool rename(const String& oldPath, const char* newPath) { return rename(oldPath.c_str(), newPath); }
    bool rename(const String& oldPath, const String& newPath) { return rename(oldPath.c_str(), newPath.c_str()); }

    /**
     * Create a directory.
     */
    bool mkdir(const char* path);
    bool mkdir(const String& path) { return mkdir(path.c_str()); }

    /**
     * Remove an empty directory.
     */
    bool rmdir(const char* path);
    bool rmdir(const String& path) { return rmdir(path.c_str()); }

    /**
     * Get total card size in bytes (stubbed, returns 1GB on Linux).
     */
    uint64_t cardSize();

    /**
     * Set root directory from CLI parameter.
     */
    void setRootDir(const std::string& path);

    /**
     * Get root directory (accessor for prefs.cpp).
     */
    const std::string& getRootDir();

    // Internal state
    std::string rootDir_;             // Root directory path (from --data-dir)
    bool initialized_ = false;        // True if begin() was called successfully
};

// Global instance for object-style access: SD_MMC.begin(), SD_MMC.exists()
extern SD_MMC_Class SD_MMC;

#endif // SD_MMC_H
