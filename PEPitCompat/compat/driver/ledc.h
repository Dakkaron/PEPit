/* PEPitCompat — LEDC PWM Shim (driver/ledc.h stub, no real backlight on Linux) */
#ifndef DRIVER_LEDC_H
#define DRIVER_LEDC_H

#include <cstdint>
#include "esp_err.h"

// LEDC speed mode (low/high peripheral sets)
typedef enum {
    LEDC_LOW_SPEED_MODE = 0,
    LEDC_HIGH_SPEED_MODE = 1,
} ledc_speed_mode_t;

// LEDC timer numbers
typedef enum {
    LEDC_TIMER_0 = 0,
    LEDC_TIMER_1 = 1,
    LEDC_TIMER_2 = 2,
    LEDC_TIMER_3 = 3,
} ledc_timer_t;

// LEDC channel numbers
typedef enum {
    LEDC_CHANNEL_0 = 0,
    LEDC_CHANNEL_1 = 1,
    LEDC_CHANNEL_2 = 2,
    LEDC_CHANNEL_3 = 3,
    LEDC_CHANNEL_4 = 4,
    LEDC_CHANNEL_5 = 5,
    LEDC_CHANNEL_6 = 6,
    LEDC_CHANNEL_7 = 7,
} ledc_channel_t;

// Duty-cycle resolution (timer bit width)
typedef enum {
    LEDC_TIMER_1_BIT = 0,
    LEDC_TIMER_2_BIT = 1,
    LEDC_TIMER_3_BIT = 2,
    LEDC_TIMER_4_BIT = 3,
    LEDC_TIMER_5_BIT = 4,
    LEDC_TIMER_6_BIT = 5,
    LEDC_TIMER_8_BIT = 7,
    LEDC_TIMER_10_BIT = 9,
    LEDC_TIMER_12_BIT = 11,
    LEDC_TIMER_13_BIT = 12,
    LEDC_TIMER_14_BIT = 13,
    LEDC_TIMER_15_BIT = 14,
} ledc_timer_bit_t;

// Clock source selection
typedef enum {
    LEDC_USE_APC_CLK = 0,
    LEDC_USE_RTC_CLK = 1,
    LEDC_USE_PLL_F16M_CLK = 2,
    LEDC_AUTO_CLK = 3,
    LEDC_USE_RC_FAST_CLK = 4,
} ledc_clk_src_t;

// Behavior during light sleep
typedef enum {
    LEDC_SLEEP_MODE_STOP = 0,
    LEDC_SLEEP_MODE_KEEP_ALIVE = 1,
} ledc_sleep_mode_t;

// Interrupt type (present in struct for completeness)
typedef enum {
    LEDC_INTR_DISABLE = 0,
    LEDC_INTR_DUTY_CHANGE = 1,
} ledc_intr_type_t;

// Timer configuration
typedef struct {
    ledc_speed_mode_t speed_mode;
    ledc_timer_bit_t duty_resolution;
    uint32_t timer_num;
    uint32_t freq_hz;
    ledc_clk_src_t clk_cfg;
} ledc_timer_config_t;

// Channel configuration
typedef struct {
    int gpio_num;
    ledc_speed_mode_t speed_mode;
    uint32_t channel;
    uint32_t timer_sel;
    uint32_t duty;
    uint32_t hpoint;
    ledc_intr_type_t intr_type;
    ledc_sleep_mode_t sleep_mode;
} ledc_channel_config_t;

// All stubs — no real PWM/backlight on Linux, all return ESP_OK
inline esp_err_t ledc_timer_config(ledc_timer_config_t* cfg) { (void)cfg; return ESP_OK; }
inline esp_err_t ledc_channel_config(ledc_channel_config_t* cfg) { (void)cfg; return ESP_OK; }
inline esp_err_t ledc_set_duty(ledc_speed_mode_t mode, uint32_t channel, uint32_t duty) { (void)mode; (void)channel; (void)duty; return ESP_OK; }
inline esp_err_t ledc_update_duty(ledc_speed_mode_t mode, uint32_t channel) { (void)mode; (void)channel; return ESP_OK; }

#endif // DRIVER_LEDC_H
