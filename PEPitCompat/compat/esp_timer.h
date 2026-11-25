/*
 * PEPitCompat — esp_timer.h shim
 *
 * esp_timer_get_time() returns microseconds since boot on ESP-IDF. The shim uses
 * the same steady clock that millis() is built on, so phase timings measured in
 * the firmware (engine3d perf counters) also work in the native test harness.
 */

#ifndef PEPIT_COMPAT_ESP_TIMER_H
#define PEPIT_COMPAT_ESP_TIMER_H

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

int64_t esp_timer_get_time(void);

#ifdef __cplusplus
}
#endif

#endif // PEPIT_COMPAT_ESP_TIMER_H
