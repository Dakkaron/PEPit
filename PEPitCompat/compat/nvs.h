/* PEPitCompat — NVS API Shim (stub, prefs use JSON instead) */
#ifndef NVS_H
#define NVS_H

#include <cstdint>
#include <cstddef>

// NVS constants (used by prefsHandler)
#define NVS_DEFAULT_PART_NAME "nvs"
#define NVS_TYPE_ANY 0xFFFFFFFF

// Open mode flags
#define NVS_READWRITE 0
#define NVS_READONLY 1

// Stub types for NVS iteration (used by prefsHandler dump)
typedef void* nvs_handle;
typedef void* nvs_iterator_t;

enum nvs_type {
    NVS_TYPE_U8 = 0,
    NVS_TYPE_I8 = 1,
    NVS_TYPE_U16 = 2,
    NVS_TYPE_I16 = 3,
    NVS_TYPE_U32 = 4,
    NVS_TYPE_I32 = 5,
    NVS_TYPE_U64 = 6,
    NVS_TYPE_I64 = 7,
    NVS_TYPE_STR = 8,
    NVS_TYPE_BLOB = 9,
};

typedef nvs_type nvs_type_t;

typedef struct {
    const char* namespace_name;
    const char* key;
    nvs_type type;
} nvs_entry_info_t;

// All stubs — prefs use JSON file storage instead, all return OK (0)
inline int nvs_open(const char* namespace_name, uint32_t open_mode, nvs_handle* handle) {
    if (handle) *handle = nullptr; return 0;
}
inline int nvs_close(nvs_handle handle) { return 0; }
inline int nvs_get_u32(nvs_handle handle, const char* key, uint32_t* value) { if (value) *value = 0; return 0; }
inline int nvs_set_u32(nvs_handle handle, const char* key, uint32_t value) { return 0; }
inline int nvs_get_blob(nvs_handle handle, const char* key, void* value, size_t* length) { if (length) *length = 0; return 0; }
inline int nvs_set_blob(nvs_handle handle, const char* key, const void* value, size_t length) { return 0; }
inline int nvs_erase_key(nvs_handle handle, const char* key) { return 0; }
inline int nvs_erase_all(nvs_handle handle) { return 0; }

// NVS iteration stubs (used by prefs dump functionality)
// Signature matches ESP-IDF: takes output ref as 4th parameter
inline int nvs_entry_find(const char* first_namespace, const char* first_key, uint32_t find_mode, nvs_iterator_t* iterator) {
    if (iterator) *iterator = nullptr; return 0; // OK, but no entries
}
inline int nvs_entry_next(nvs_iterator_t* iterator) { if (iterator) *iterator = nullptr; return 1; } // Done
inline int nvs_entry_info(nvs_iterator_t iterator, nvs_entry_info_t* info) { return 1; } // Done
inline void nvs_release_iterator(nvs_iterator_t iterator) {}

#endif // NVS_H
