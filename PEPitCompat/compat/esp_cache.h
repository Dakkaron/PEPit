/*
 * PEPitCompat — esp_cache.h shim
 *
 * Host memory is cache-coherent by construction, so cache maintenance is a
 * no-op. Flag values mirror IDF's esp_mm/esp_cache.h so engine code behaves
 * identically on both sides.
 */
#ifndef PEPIT_COMPAT_ESP_CACHE_H
#define PEPIT_COMPAT_ESP_CACHE_H

#include "esp_err.h"
#include <stddef.h>

#define ESP_CACHE_MSYNC_FLAG_INVALIDATE    0x01u
#define ESP_CACHE_MSYNC_FLAG_UNALIGNED     0x02u
#define ESP_CACHE_MSYNC_FLAG_DIR_C2M       0x04u
#define ESP_CACHE_MSYNC_FLAG_DIR_M2C       0x08u
#define ESP_CACHE_MSYNC_FLAG_TYPE_DATA     0x10u
#define ESP_CACHE_MSYNC_FLAG_TYPE_INST     0x20u

inline esp_err_t esp_cache_msync(void* addr, size_t len, uint32_t flags) {
  (void)addr; (void)len; (void)flags;
  return ESP_OK;
}

#endif
