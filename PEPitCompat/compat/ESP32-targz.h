/*
 * PEPitCompat — ESP32-targz Shim (non-functional stubs)
 * 
 * Stub for firmware update TAR/TARGZ extraction. All operations are no-ops
 * returning OK. No actual unpacking performed on Linux.
 */

#ifndef ESP32_TARGZ_H
#define ESP32_TARGZ_H

#include <cstdint>
#include <cstddef>
#include "Arduino.h"
#include "FS.h"

// Forward declaration for FS-type objects used by ESP32-targz
class FS {
public:
    virtual bool begin() { return true; }
    virtual File open(const String& path, const String& mode = "r", bool create = false) { return File(); }
    virtual bool exists(const String& path) { return false; }
};

// TAR header structure (used by exclude filter callback)
namespace TAR {
    struct header_translated_t {
        char filename[100];
    };
}

// Callback function pointer types (used by ESP32-targz API)
typedef size_t (*targz_fs_total_bytes_fn)(FS& fs);
typedef size_t (*targz_fs_free_bytes_fn)(FS& fs);

// Stub FS instance for tarGzFS (used by updateHandler)
extern FS tarGzFS;

// Stub callback function pointers (referenced by updateHandler)
extern size_t targzTotalBytesFn(FS& fs);
extern size_t targzFreeBytesFn(FS& fs);

// BaseUnpacker with static logger callback
class BaseUnpacker {
public:
    static void targzPrintLoggerCallback(const char* msg) {} // No-op logger
};

// TarUnpacker stub — no actual TAR extraction on Linux
class TarUnpacker {
public:
    void haltOnError(bool val) {} // OK, no-op
    void setTarVerify(bool val) {} // OK, no-op
    void setupFSCallbacks(targz_fs_total_bytes_fn totalFn, targz_fs_free_bytes_fn freeFn) {} // OK, no-op
    void setTarProgressCallback(void (*fn)(unsigned char)) {} // OK, no-op
    void setTarStatusProgressCallback(void (*fn)(const char*, size_t, size_t)) {} // OK, no-op
    void setTarMessageCallback(void (*fn)(const char*)) {} // OK, no-op
    void setTarExcludeFilter(bool (*fn)(TAR::header_translated_t*)) {} // OK, no-op
    bool tarExpander(FS& srcFs, const char* srcFile, FS& dstFs, const char* dstDir) { return true; } // OK
    int tarGzGetError() { return 0; } // No error
};

// TarGzUnpacker stub — no actual TARGZ extraction on Linux
class TarGzUnpacker {
public:
    void haltOnError(bool val) {} // OK, no-op
    void setTarVerify(bool val) {} // OK, no-op
    void setupFSCallbacks(targz_fs_total_bytes_fn totalFn, targz_fs_free_bytes_fn freeFn) {} // OK, no-op
    void setGzProgressCallback(void (*fn)(unsigned char)) {} // OK, no-op
    void setTarProgressCallback(void (*fn)(unsigned char)) {} // OK, no-op
    void setLoggerCallback(void (*fn)(const char*)) {} // OK, no-op
    void setTarStatusProgressCallback(void (*fn)(const char*, size_t, size_t)) {} // OK, no-op
    void setTarMessageCallback(void (*fn)(const char*)) {} // OK, no-op
    void setTarExcludeFilter(bool (*fn)(TAR::header_translated_t*)) {} // OK, no-op
    bool tarGzExpander(FS& srcFs, const char* srcFile, FS& dstFs, const char* dstDir, void* param) { return true; } // OK
    int tarGzGetError() { return 0; } // No error
};

#endif // ESP32_TARGZ_H
