#include "hardware/touchHandler.h"

#include <Arduino.h>
#include <vector>

#include "hardware/prefsHandler.h"
#include "pins.h"
#include "systemconfig.h"
#include "constants.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/powerHandler.h"


static touch_calibration_t calibration_data[4];
static std::vector<touch_calibration_t> raw_data;

XPT2046 touch = XPT2046(SPI, TOUCHSCREEN_CS_PIN, TOUCHSCREEN_IRQ_PIN);

static void writeTouchCalibration() {
  prefs.end();
  prefs.begin("touch");
  uint8_t* calibBytes = reinterpret_cast<uint8_t*>(&calibration_data);
  prefs.putBytes("calib", calibBytes, 16);
  prefs.end();
  applyGamePrefsNamespace();
}

void runTouchCalibration() {
  for (int32_t point=0;point<4;point++) {
    Serial.println(1);
    spr.fillSprite(TFT_BLACK);
    switch (point) {
    case 0:
      spr.drawCircle(0, 240, 10, TFT_WHITE);
      break;
    case 1:
      spr.drawCircle(0, 0, 10, TFT_WHITE);
      break;
    case 2:
      spr.drawCircle(320, 0, 10, TFT_WHITE);
      break;
    case 3:
      spr.drawCircle(320, 240, 10, TFT_WHITE);
      break;
    }
    Serial.println(2);
    spr.setTextSize(2);
    spr.drawString("Bitte den Kreis antippen", 5, 120, 2);
    spr.pushSpriteFast(0,0);

    Serial.println(3);
    while (!touch.pressed()) {
      vTaskDelay(1); // watchdog
    }
    uint8_t count = 0;
    while (touch.pressed() && count<100) {
      count++;
      uint16_t x = touch.RawX();
      uint16_t y = touch.RawY();
      Serial.print("X: ");
      Serial.print(x);
      Serial.print(", Y: ");
      Serial.println(y);
      raw_data.push_back((touch_calibration_t) {
        .rawX = x,
        .rawY = y
      });
    }
    while (touch.pressed()) {
      vTaskDelay(1); // watchdog
    }

    Serial.println(4);
    uint32_t tmpX = 0;
    uint32_t tmpY = 0;

    for (size_t i = 0; i < raw_data.size(); i++) {
      tmpX += raw_data[i].rawX;
      tmpY += raw_data[i].rawY;
    }
    Serial.println(5);
    calibration_data[point].rawX = tmpX / raw_data.size();
    calibration_data[point].rawY = tmpY / raw_data.size();
    raw_data.clear();
    Serial.println(6);
  }
  Serial.println(7);
  writeTouchCalibration();
  spr.fillSprite(TFT_BLACK);
  spr.drawString("Kalibrierung", 5, 120, 2);
  spr.drawString("abgeschlossen", 5, 160, 2);
  spr.pushSprite(0,0);
  Serial.println("## Calibration result");
  Serial.println(calibration_data[0].rawX);
  Serial.println(calibration_data[0].rawY);
  Serial.println(calibration_data[1].rawX);
  Serial.println(calibration_data[1].rawY);
  Serial.println(calibration_data[2].rawX);
  Serial.println(calibration_data[2].rawY);
  Serial.println(calibration_data[3].rawX);
  Serial.println(calibration_data[3].rawY);
  Serial.println(8);
  delay(3000);
  esp_restart();
}

static void readTouchCalibration() {
  prefs.end();
  prefs.begin("touch");
  if (prefs.isKey("calib")) {
    uint8_t* calibBytes = reinterpret_cast<uint8_t*>(&calibration_data);
    prefs.getBytes("calib", calibBytes, 16);
  } else {
    runTouchCalibration();
  }
  prefs.end();
  Serial.println("## CALIBRATION");
  Serial.println(calibration_data[0].rawX);
  Serial.println(calibration_data[0].rawY);
  Serial.println(calibration_data[1].rawX);
  Serial.println(calibration_data[1].rawY);
  Serial.println(calibration_data[2].rawX);
  Serial.println(calibration_data[2].rawY);
  Serial.println(calibration_data[3].rawX);
  Serial.println(calibration_data[3].rawY);
  applyGamePrefsNamespace();
}

void initTouch() {
  readTouchCalibration();
  SPI.begin(TOUCHSCREEN_SCLK_PIN, TOUCHSCREEN_MISO_PIN, TOUCHSCREEN_MOSI_PIN);
  touch.begin(240, 320);
  touch.setCal(calibration_data[0].rawX, calibration_data[2].rawX, calibration_data[0].rawY, calibration_data[2].rawY, 240, 320);
  touch.setRotation((SCREEN_ROTATION + 2) % 4);
  touch.setZThreshold(systemConfig.touchScreenZThreshold);
}

bool isTouchInZone(int16_t x, int16_t y, int16_t w, int16_t h) {
  //Serial.println("## Touch in zone");
  if (!touch.pressed()) {
    return false;
  }
  int16_t tx = touch.X();
  int16_t ty = touch.Y();
  //int16_t tz = touch.RawZ();
  /*Serial.print("Touch: ");
  Serial.print(x);
  Serial.print("/");
  Serial.print(y);
  Serial.print("/");
  Serial.print(w);
  Serial.print("/");
  Serial.print(h);
  Serial.print(" -> ");
  Serial.print(tx);
  Serial.print("/");
  Serial.print(ty);
  Serial.print("/");
  Serial.println(tz);*/
  return (tx>=x && tx<=x+w && ty>=y && ty<=y+h);
}
