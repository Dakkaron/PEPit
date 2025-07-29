#include <Arduino.h>
#define CONFIG_SPIRAM_SUPPORT 1

#include "systemconfig.h"
#include "constants.h"

#include <SD_MMC.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <OneButton.h>

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
  pinMode(PWR_ON_PIN, OUTPUT);
  digitalWrite(PWR_ON_PIN, HIGH);
  pinMode(BK_LIGHT_PIN, OUTPUT);
  digitalWrite(BK_LIGHT_PIN, HIGH);

  esp_log_level_set("gpio", ESP_LOG_ERROR);

  Serial.setRxBufferSize(10240);
  Serial.setTxBufferSize(1024);
  Serial.begin(115200);

  pinMode(PWR_EN_PIN, OUTPUT);
  digitalWrite(PWR_EN_PIN, HIGH);

  tft.init();
  tft.setRotation(SCREEN_ROTATION);
  tft.setSwapBytes(true);
  tft.fillScreen(TFT_BLACK);

  setBrightness(16);

  initGfxHandler();
  tft.setTextSize(2);
  tft.drawString("Nicht", 10, 10);
  tft.drawString("blasen!", 10, 25);
  String errorMessage;
  initPressureSensor(&errorMessage);
  checkFailWithMessage(errorMessage);

  initTouch();

  buttonPwr.attachClick(power_off);
  buttonPwr.attachLongPressStop(power_off);
  buttonUsr.attachLongPressStop(runTouchCalibration);

  randomSeed(analogRead(0));
  
  initSD(&errorMessage);
  checkFailWithMessage(errorMessage);
  checkForPrefsReset();
  initSystemConfig(&errorMessage);
  checkFailWithMessage(errorMessage);

  drawBmp("/gfx/splash.bmp", 0, 0);
  uint8_t defaultTextDatum = tft.getTextDatum();
  tft.setTextColor(TFT_BLACK);
  tft.setTextDatum(TR_DATUM);
  tft.drawString("Version "+String(VERSION), 320, 2);
  tft.setTextDatum(defaultTextDatum);
  delay(1000);
  Serial.print("PEPit Version '");
  Serial.print(VERSION);
  Serial.println("'");
  Serial.print("Initializing peripherals....");
  delay(1000);

  Serial.println(F("done"));
  
  Serial.print("PEPit Version '");
  Serial.print(VERSION);
  Serial.println("' initialized");

  checkForAndRunUpdateFromSD(&errorMessage);
  checkFailWithMessage(errorMessage);

  tft.setTextSize(1);
  tft.drawString("Warte auf WLAN-Verbindung...", 0, 230);

  uint32_t requiredTaskTypes = runProfileSelection();
  runGameSelection(requiredTaskTypes);
  displayPhysioRotateScreen();
}

void loop() {
  buttonPwr.tick();
  buttonUsr.tick();
  handleSerial();
  handlePhysioTask();
  /*Serial.print("Free heap: ");
  Serial.println(ESP.getFreeHeap());*/
}