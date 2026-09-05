/* PEPitCompat — NVS Flash Shim (stub, prefs use JSON instead) */
#ifndef NVS_FLASH_H
#define NVS_FLASH_H

#include <cstdint>

// Stub — all functions are no-op (prefs use JSON file storage instead)
inline int nvs_flash_init() { return 0; }
inline int nvs_flash_erase() { return 0; }
inline int nvs_flash_init_partition(const char* name) { return 0; }

#endif // NVS_FLASH_H
