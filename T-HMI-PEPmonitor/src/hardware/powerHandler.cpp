#include "powerHandler.h"
#include "gfxHandler.hpp"
#include "serialHandler.h"
#include "systemStateHandler.h"
#include "pressuresensor.h"
#include <SD_MMC.h>
#include "driver/rtc_io.h"

OneButton buttonPwr = OneButton(BUTTON2_PIN, false, false);
OneButton buttonUsr = OneButton(BUTTON1_PIN, false, false);

static RTC_DATA_ATTR bool skipSplashScreen;

uint32_t readBatteryVoltage() {
    return (analogRead(BAT_ADC_PIN) * 162505) / 100000;
}

bool isSkipSplashScreen() {
    esp_reset_reason_t reason = esp_reset_reason();
    return skipSplashScreen || reason == ESP_RST_SW || reason == ESP_RST_USB;
}

void deepSleepReset() {
    skipSplashScreen = true;
    esp_sleep_enable_timer_wakeup(1);
    esp_deep_sleep_start();
}

void power_off() {
    Serial.println("power_off");
    
    uint32_t lastMs = millis();
    uint32_t ms = millis();
    tft.fillScreen(TFT_BLACK);
    spr.frameBuffer(1);
    spr.fillSprite(TFT_GREEN);
    spr.frameBuffer(2);
    spr.fillSprite(TFT_BLACK);

    while (readBatteryVoltage()>4200) { // Charging display
        spr.fillSprite(TFT_BLACK);
        doSystemTasks();
        lastMs = ms;
        ms = millis();
        spr.pushSpriteFast(0,0);
        handleSerial();
        vTaskDelay(1); // watchdog
    }

    while (gpio_get_level(GPIO_NUM_21)) {
        // Wait for power button to be released
    }

    Serial.println("Deep sleep starting");
    tft.writecommand(0x10);
    powerDownPressureSensor(true);
    digitalWrite(M5STACK_JOYSTICK2_POWER_PIN, LOW);
    SD_MMC.end();
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 1); // Set power button as wakeup source
    rtc_gpio_pullup_dis(GPIO_NUM_21);
    rtc_gpio_pulldown_dis(GPIO_NUM_21);
    esp_deep_sleep_start();
}