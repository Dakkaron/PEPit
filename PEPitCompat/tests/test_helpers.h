/*
 * PEPitCompat — Test Helpers
 *
 * Small utilities shared between test files: per-test temp directories for
 * the SD_MMC shim (which maps the ESP32 SD card to a POSIX directory).
 */

#ifndef TEST_HELPERS_H
#define TEST_HELPERS_H

#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <string>

#include "Arduino.h"  // String class (shim)
#include "SD_MMC.h"   // SD_MMC shim

namespace pepit_test {

/**
 * Create a unique temp directory, point the SD_MMC shim at it, and begin.
 *
 * Safe to call multiple times per test file — each call replaces the previous
 * root and the directory is recreated fresh. Returns the directory path.
 */
inline std::filesystem::path makeSdRoot(const std::string& name) {
    static int counter = 0;
    counter++;
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
        ("pepit_test_" + std::to_string(::getpid()) + "_" + std::to_string(counter) + "_" + name);
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    std::string root = dir.generic_string();
    if (root.empty() || root.back() != '/') root += '/';
    SD_MMC.setRootDir(root);
    SD_MMC.begin(nullptr, false);
    return dir;
}

/**
 * Write a fixture file (relative to the SD root, i.e. what the app sees as
 * "/relPath"). Parent directories are created as needed.
 */
inline void writeSdFile(const std::filesystem::path& root, const std::string& relPath, const std::string& contents) {
    std::filesystem::path p = root / relPath;
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    std::ofstream f(p, std::ios::binary);
    f << contents;
}

/** Read a whole file from the SD root into a std::string ("" if missing). */
inline std::string readSdFile(const std::filesystem::path& root, const std::string& relPath) {
    std::ifstream f(root / relPath, std::ios::binary);
    if (!f) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    return s;
}

}  // namespace pepit_test

#endif  // TEST_HELPERS_H
