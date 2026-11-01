/*
 * PEPitCompat — ESP-IDF ADC Shim (esp_adc/adc_oneshot.h stub)
 *
 * Minimal Linux stub of the ESP32 ADC oneshot driver API used by the
 * original source (powerHandler). No real ADC hardware on Linux; values
 * are simulated via injected input where applicable.
 */

#ifndef ESP_ADC_ADC_ONESHOT_H
#define ESP_ADC_ADC_ONESHOT_H

#include <cstdint>
#include "esp_err.h"

// Stub driver types (opaque handles)
typedef void* adc_oneshot_unit_handle_t;
typedef void* adc_cali_handle_t;

// Init / channel config structs (ESP-IDF compatible layouts)
typedef struct {
    int unit_id;
    int ulp_mode;
} adc_oneshot_unit_init_cfg_t;

typedef struct {
    int atten;
    int bitwidth;
} adc_oneshot_chan_cfg_t;

// ADC units / channels / modes (stub enums with ESP-IDF-compatible values)
typedef enum {
    ADC_UNIT_1 = 1,
    ADC_UNIT_2 = 2,
} adc_unit_id_t;

typedef enum {
    ADC_ULP_MODE_DISABLE = 0,
} adc_ulp_mode_t;

typedef enum {
    ADC_CHANNEL_0 = 0,
    ADC_CHANNEL_1 = 1,
    ADC_CHANNEL_2 = 2,
    ADC_CHANNEL_3 = 3,
    ADC_CHANNEL_4 = 4,
    ADC_CHANNEL_5 = 5,
    ADC_CHANNEL_6 = 6,
    ADC_CHANNEL_7 = 7,
    ADC_CHANNEL_8 = 8,
    ADC_CHANNEL_9 = 9,
    ADC_CHANNEL_10 = 10,
    ADC_CHANNEL_11 = 11,
    ADC_CHANNEL_12 = 12,
    ADC_CHANNEL_13 = 13,
    ADC_CHANNEL_14 = 14,
    ADC_CHANNEL_15 = 15,
} adc_channel_t;

typedef enum {
    ADC_ATTEN_DB_0 = 0,
    ADC_ATTEN_DB_2 = 1,
    ADC_ATTEN_DB_6 = 2,
    ADC_ATTEN_DB_12 = 3,
} adc_atten_t;

typedef enum {
    ADC_BITWIDTH_12 = 12,
} adc_bitwidth_t;

// Unit lifecycle
inline int adc_oneshot_new_unit(const void* cfg, adc_oneshot_unit_handle_t* handle) {
    (void)cfg;
    *handle = nullptr;
    return ESP_OK;
}

// Channel config
inline int adc_oneshot_config_channel(adc_oneshot_unit_handle_t handle, adc_channel_t chan, const void* chan_cfg) {
    (void)handle;
    (void)chan;
    (void)chan_cfg;
    return ESP_OK;
}

// Blocking read
inline int adc_oneshot_read(adc_oneshot_unit_handle_t handle, adc_channel_t chan, int32_t* raw_value) {
    (void)handle;
    (void)chan;
    *raw_value = 0;
    return ESP_OK;
}

#endif // ESP_ADC_ADC_ONESHOT_H
