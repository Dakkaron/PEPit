/* PEPitCompat — ESP-IDF Error Shim (esp_err.h stub) */
#ifndef ESP_ERR_H
#define ESP_ERR_H

#include <cstdint>

typedef int32_t esp_err_t;

#define ESP_OK                0
#define ESP_FAIL              -1

/** Evaluate an esp_err_t expression; no-op on Linux (stub never aborts). */
#define ESP_ERROR_CHECK(x) do { (void)(x); } while(0)
#define ESP_ERR_NO_MEM        0x101
#define ESP_ERR_INVALID_ARG   0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND     0x105
#define ESP_ERR_NOT_SUPPORTED 0x108

#endif // ESP_ERR_H
