/*
 * PEPitCompat — ESP-IDF ADC Shim (esp_adc/adc_cali.h stub)
 *
 * Minimal Linux stub of the ESP32 ADC calibration (curve fitting) API used by
 * the original source (powerHandler). No real ADC hardware on Linux.
 */

#ifndef ESP_ADC_ADC_CALI_H
#define ESP_ADC_ADC_CALI_H

#include <cstdint>
#include "esp_adc/adc_oneshot.h"

// Stub calibration handle
typedef void* adc_cali_handle_t;

typedef struct {
    int unit_id;
    int chan;
    int atten;
    int bitwidth;
} adc_cali_curve_fitting_config_t;

// Calibration scheme creation (stub — no hardware, no fitting needed)
inline int adc_cali_create_scheme_curve_fitting(const adc_cali_curve_fitting_config_t* config, adc_cali_handle_t* cali_handle) {
    (void)config;
    *cali_handle = nullptr;
    return ESP_OK;
}

// Raw ADC value -> voltage conversion (stub — returns a nominal battery voltage on Linux)
// NOTE: accepts an int* millivolts out-param to match the original source usage.
inline int adc_cali_raw_to_voltage(adc_cali_handle_t cali, int32_t raw_value, int* voltage) {
    (void)cali;
    (void)raw_value;
    if (voltage) {
        // The firmware computes readBatteryVoltage() = voltage * 2 + 100 (voltage
        // divider). We report 2000 mV at the ADC => readBatteryVoltage() = 4100 mV:
        //   - above the low-battery warning (3400) and shutdown (3300) thresholds
        //   - below 4200 mV, so power_off()'s "while (voltage > 4200)" charging
        //     display loop terminates and the emulated device can deep-sleep/exit
        //   - below 4250, so the status bar shows "full" instead of "charging"
        *voltage = 2000;
    }
    return ESP_OK;
}

#endif // ESP_ADC_ADC_CALI_H
