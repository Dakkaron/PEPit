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

static void configurePowerButtonWakeup();

static SemaphoreHandle_t poweroffMutex = xSemaphoreCreateMutex();

uint32_t readBatteryVoltage() {
    return (analogRead(BAT_ADC_PIN) * 162505) / 100000;
}

bool isSkipSplashScreen() {
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.printf("[power] reset reason=%d\n", reason);
    return skipSplashScreen || reason == ESP_RST_SW || reason == ESP_RST_USB;
}

void deepSleepReset() {
    if (!xSemaphoreTake(poweroffMutex, 0)) {
        return;
    }
    Serial.println("[power] deepSleepReset() start");
    skipSplashScreen = true;
    tft.writecommand(0x10);
    powerDownPressureSensor(true);
    digitalWrite(M5STACK_JOYSTICK2_POWER_PIN, LOW);
    SD_MMC.end();

    delay(50);

    Serial.printf("[power] rtc level before sleep=%d\n", rtc_gpio_get_level(GPIO_NUM_21));
    configurePowerButtonWakeup();

    delay(100);
    for (uint8_t i = 0; i < 10; ++i) {
        int level = rtc_gpio_get_level(GPIO_NUM_21);
        Serial.printf("[power] settle sample %u level=%d\n", i + 1, level);
        if (level == 0) {
            break;
        }
        delay(10);
    }

    esp_sleep_enable_timer_wakeup(1000); // Set wakeup timer as wakeup source
    Serial.println("[power] entering deep sleep");
    esp_deep_sleep_start();
}

static void configurePowerButtonWakeup() {
    rtc_gpio_init(GPIO_NUM_21);
    rtc_gpio_set_direction(GPIO_NUM_21, RTC_GPIO_MODE_INPUT_ONLY);
    rtc_gpio_pullup_dis(GPIO_NUM_21);
    rtc_gpio_pulldown_dis(GPIO_NUM_21);
    esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    esp_sleep_enable_ext0_wakeup(GPIO_NUM_21, 1);
}

static bool waitForPowerButtonRelease() {
    Serial.println("[power] waiting for power button release");
    uint32_t startMs = millis();
    uint8_t lowCount = 0;

    while (millis() - startMs < 1500) {
        int level = rtc_gpio_get_level(GPIO_NUM_21);
        if (level == 0) {
            lowCount++;
            if (lowCount >= 3) {
                Serial.printf("[power] button released after %lu ms\n", millis() - startMs);
                return true;
            }
        } else {
            lowCount = 0;
        }
        delay(10);
    }

    Serial.println("[power] button release timeout; proceeding with sleep");
    return false;
}

void power_off() {
    if (!xSemaphoreTake(poweroffMutex, 0)) {
        return;
    }
    Serial.println("[power] power_off() start");
    buttonPwr.attachClick(nullptr);
    buttonPwr.attachLongPressStop(nullptr);
    buttonUsr.attachClick(nullptr);
    buttonUsr.attachLongPressStop(nullptr);
    
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
    delay(50);

    Serial.println("Deep sleep starting");
    tft.writecommand(0x10);
    powerDownPressureSensor(true);
    digitalWrite(M5STACK_JOYSTICK2_POWER_PIN, LOW);
    SD_MMC.end();
    delay(100);

    gpio_deep_sleep_hold_dis();
    gpio_hold_dis(GPIO_NUM_14);
    rtc_gpio_hold_dis(GPIO_NUM_21);

    configurePowerButtonWakeup();
    waitForPowerButtonRelease();
    delay(100);
    gpio_hold_en(GPIO_NUM_14);
    gpio_deep_sleep_hold_en();
    Serial.println("[power] about to enter deep sleep");
    esp_deep_sleep_start();
}