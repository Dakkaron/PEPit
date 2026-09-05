/* PEPitCompat — RTC GPIO Shim (stub, no real hardware on Linux) */
#ifndef DRIVER_RTC_IO_H
#define DRIVER_RTC_IO_H

#include <cstdint>

// RTC GPIO direction modes (from ESP-IDF)
typedef enum {
    RTC_GPIO_MODE_INPUT_ONLY = 0,
    RTC_GPIO_MODE_OUTPUT_ONLY = 1,
    RTC_GPIO_MODE_INPUT_OUTPUT = 2,
} rtc_gpio_mode_t;

// All stubs — no real RTC GPIO on Linux
inline int rtc_gpio_init(uint32_t pin) { return 0; }
inline int rtc_gpio_set_direction(uint32_t pin, rtc_gpio_mode_t mode) { return 0; }
inline int rtc_gpio_pullup_dis(uint32_t pin) { return 0; }
inline int rtc_gpio_pulldown_dis(uint32_t pin) { return 0; }
inline int rtc_gpio_hold_dis(uint32_t pin) { return 0; }

// gpio_get_level: used in power_off() to wait for button release.
// Always return HIGH (1) — button is never pressed on Linux.
inline int gpio_get_level(uint32_t pin) { return 1; }

// rtc_gpio_get_level: read RTC GPIO level (power button). Always HIGH (1) on Linux.
inline int rtc_gpio_get_level(uint32_t pin) { (void)pin; return 1; }
inline void rtc_gpio_hold_dis_all() {}

#endif // DRIVER_RTC_IO_H
