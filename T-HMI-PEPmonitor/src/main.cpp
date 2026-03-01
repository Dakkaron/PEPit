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

void setBrightness(uint8_t value) {
  static uint8_t _brightness = 0;

  if (_brightness == value) {
    return;
  }
  value = _min(16, value);
  if (value == 0) {
    digitalWrite(BK_LIGHT_PIN, 0);
    delay(3);
    _brightness = 0;
    return;
  }
  if (_brightness == 0) {
    digitalWrite(BK_LIGHT_PIN, 1);
    _brightness = SCREEN_BRIGHTNESS_STEPS;
    delayMicroseconds(30);
  }
  int from = SCREEN_BRIGHTNESS_STEPS - _brightness;
  int to = SCREEN_BRIGHTNESS_STEPS - value;
  int num = (SCREEN_BRIGHTNESS_STEPS + to - from) % SCREEN_BRIGHTNESS_STEPS;
  for (int i = 0; i < num; i++) {
    digitalWrite(BK_LIGHT_PIN, 0);
    digitalWrite(BK_LIGHT_PIN, 1);
  }
  _brightness = value;
}

void setup() {
  esp_log_level_set("wifi", ESP_LOG_WARN);
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  pinMode(BK_LIGHT_PIN, OUTPUT);
  digitalWrite(BK_LIGHT_PIN, LOW);

  pinMode(M5STACK_JOYSTICK2_POWER_PIN, OUTPUT); // Activate joystick power
  digitalWrite(M5STACK_JOYSTICK2_POWER_PIN, HIGH);

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
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  initGfxHandler();
  setBrightness(16);
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
