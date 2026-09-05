#include <Arduino.h>
#define CONFIG_SPIRAM_SUPPORT 1

#include "systemconfig.h"
#include "constants.h"

#include <SD_MMC.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <OneButton.h>
#include "esp_log.h"
#include "esp_err.h"
#include "driver/ledc.h"
#include "esp_pm.h"

#include "hardware/gfxHandler.hpp"
#include "hardware/pressuresensor.h"
#include "hardware/touchHandler.h"
#include "hardware/wifiHandler.h"
#include "hardware/sdHandler.h"
#include "games/games.h"
#include "hardware/serialHandler.h"
#include "hardware/powerHandler.h"
#include "physioProtocolHandler.h"
#include "updateHandler.h"
#include "hardware/bluetoothHandler.h"
#include "systemStateHandler.h"
#include "hardware/joystickHandler.h"

#define LEDC_TIMER              LEDC_TIMER_0
#define LEDC_MODE               LEDC_LOW_SPEED_MODE
#define LEDC_OUTPUT_IO          BK_LIGHT_PIN // Define the output GPIO
#define LEDC_CHANNEL            LEDC_CHANNEL_0
#define LEDC_DUTY_RES           LEDC_TIMER_13_BIT // Set duty resolution to 13 bits
#define LEDC_DUTY               (4096) // Set duty to 50%. (2 ** 13) * 50% = 4096
#if CONFIG_PM_ENABLE
#define LEDC_CLK_SRC            LEDC_USE_RC_FAST_CLK // choose a clock source that can maintain during light sleep
#define LEDC_FREQUENCY          (400) // Frequency in Hertz. Set frequency at 400 Hz
#else
#define LEDC_CLK_SRC            LEDC_AUTO_CLK
#define LEDC_FREQUENCY          (4000) // Frequency in Hertz. Set frequency at 4 kHz
#endif

static void example_ledc_init(void) {
    // Prepare and then apply the LEDC PWM timer configuration
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = LEDC_MODE,
        .duty_resolution  = LEDC_DUTY_RES,
        .timer_num        = LEDC_TIMER,
        .freq_hz          = LEDC_FREQUENCY,  // Set output frequency at 4 kHz
        .clk_cfg          = LEDC_CLK_SRC,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    // Prepare and then apply the LEDC PWM channel configuration
    ledc_channel_config_t ledc_channel = {
        .gpio_num       = LEDC_OUTPUT_IO,
        .speed_mode     = LEDC_MODE,
        .channel        = LEDC_CHANNEL,
        .timer_sel      = LEDC_TIMER,
        .duty           = 0, // Set duty to 0%
        .hpoint         = 0,
#if CONFIG_PM_ENABLE
        .sleep_mode     = LEDC_SLEEP_MODE_KEEP_ALIVE,
#endif
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

/*
 * value: 0-8191
 */
void setBrightness(uint32_t value) {
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_MODE, LEDC_CHANNEL, value));
  // Update duty to apply the new value
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_MODE, LEDC_CHANNEL));
  //analogWrite(BK_LIGHT_PIN, value);
  /*if (value == 255) {
    digitalWrite(BK_LIGHT_PIN, 1);
  } else {
    analogWrite(BK_LIGHT_PIN, _max(value, 10));
  }*/
}

void setup() {
  esp_log_level_set("wifi", ESP_LOG_WARN);
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  pinMode(BK_LIGHT_PIN, OUTPUT);
  digitalWrite(BK_LIGHT_PIN, LOW);
  example_ledc_init();
  setBrightness(0);

  pinMode(M5STACK_JOYSTICK2_POWER_PIN, OUTPUT); // Activate joystick power
  digitalWrite(M5STACK_JOYSTICK2_POWER_PIN, HIGH);

  gpio_deep_sleep_hold_dis();
  gpio_hold_dis(GPIO_NUM_14);
  gpio_deep_sleep_hold_en();
  gpio_hold_en(GPIO_NUM_14); // Make sure PWR_ON_PIN stays active even in deep sleep

  esp_log_level_set("gpio", ESP_LOG_ERROR);

  Serial.print("PEPit Version '");
  Serial.print(VERSION);
  Serial.println("'");
  Serial.print("Initializing peripherals....");

  Serial.setRxBufferSize(10240);
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);

  getSystemUpdateAvailableStatus(); // Trigger connecting to Wifi
  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.setSwapBytes(false);
  tft.fillScreen(TFT_BLACK);

  initGfxHandler();
  tft.setTextSize(2);
  if (!isSkipSplashScreen()) {
    tft.drawString("Nicht", 10, 10);
    tft.drawString("blasen!", 10, 25);
  }
  String errorMessage;
  initPressureSensor(&errorMessage);
  buttonPwr.attachClick(power_off);
  buttonPwr.attachLongPressStop(power_off);
  buttonUsr.attachLongPressStop(runTouchCalibration);
  initTouch();

  checkFailWithMessage(errorMessage);

  randomSeed(analogRead(0));
  
  initSD(&errorMessage);
  checkFailWithMessage(errorMessage);
  
  if (!isSkipSplashScreen()) {
    loadBmp(&spr, "/gfx/splash.bmp");
    spr.pushSpriteFast(0,0);
  }
  checkForPrefsReset();
  initSystemConfig(&errorMessage);
  checkFailWithMessage(errorMessage);
  configTime(systemConfig.timezoneOffset, 0, "pool.ntp.org", "time.nist.gov");

  uint8_t defaultTextDatum = tft.getTextDatum();
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  if (!isSkipSplashScreen()) {
    tft.drawString("Version "+String(VERSION), 320, 2);
    tft.setTextDatum(defaultTextDatum);
    Serial.println(F("done"));
    delay(1000);
  }
  
  Serial.print("PEPit Version '");
  Serial.print(VERSION);
  Serial.println("' initialized");

  checkForAndRunUpdateFromSD(&errorMessage);
  checkFailWithMessage(errorMessage);

  Serial.print("Joystick present: ");
  Serial.println(isJoystickPresent());

  Serial.println("Connecting to trampoline...");
  connectToTrampoline(false);
  Serial.println("MARKER DONE");
  setSystemState(STATE_PROFILE_SELECTION);
  uint32_t requiredTaskTypes = runProfileSelection();
  if ((requiredTaskTypes & REQUIRED_TASK_TYPE_TRAMPOLINE) == 0) {
    Serial.print("Disabling Bluetooth...");
    disableBluetooth();
    Serial.println("done");
  }
  setSystemState(STATE_GAME_SELECTION);
  if (!systemConfig.manometerMode) {
    runGameSelection(requiredTaskTypes);
  }
  setSystemState(STATE_GAME_RUNNING);
  displayPhysioRotateScreen();
}

void loop() {
  handleSerial();
  handlePhysioTask();
  /*Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());*/
}
