/*
 * PEPitCompat — esp_heap_caps.h Shim
 *
 * heap_caps_malloc/heap_caps_realloc are provided by compat/Arduino.h as
 * plain malloc/realloc; the capability flags are accepted and ignored.
 */

#ifndef PEPIT_COMPAT_ESP_HEAP_CAPS_H
#define PEPIT_COMPAT_ESP_HEAP_CAPS_H

#include <Arduino.h>

#define MALLOC_CAP_INTERNAL  (1 << 11)
#define MALLOC_CAP_SPIRAM    (1 << 27)
#define MALLOC_CAP_8BIT      (1 << 1)
#define MALLOC_CAP_DEFAULT   (1 << 21)

#endif
