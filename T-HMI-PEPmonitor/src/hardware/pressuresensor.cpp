#include "hardware/pressuresensor.h"
#include <Adafruit_HX711.h>
#include "systemconfig.h"

static Adafruit_HX711 hx711 = Adafruit_HX711(HX7711_DATA_PIN, HX7711_CLOCK_PIN);

void initPressureSensor(String* errorMessage) {
  hx711.begin();
  Serial.print("Tareing air pressure sensor....");
  for (uint32_t i=0; i<=10000000; i++) {
    if (!hx711.isBusy()) {
      break;
    }
    if (i==10000000) {
      errorMessage->concat("Luftdruck-Sensor nicht gefunden!\n");
      return;
    }
  }
  for (uint8_t t=0; t<3; t++) {
    hx711.tareA(hx711.readChannelRaw(CHAN_A_GAIN_64));
    hx711.tareA(hx711.readChannelRaw(CHAN_A_GAIN_64));
  }
  Serial.print("done");

}

void readPressure(BlowData* blowData) {
  static float readings[PRESSURE_SENSOR_SMOOTHING_NUM_READINGS];
  static uint8_t readIndex = 0;
  static float total = 0;
  static uint8_t skips = 0;
  static float tare = 0;
  if (blowData->ms + 4999 < millis()) {
    for (int i=0;i<PRESSURE_SENSOR_SMOOTHING_NUM_READINGS;i++) {
      readings[i] = 0;
    }
  }
  if (systemConfig.simulateBlows) {
    uint32_t blowDuration = blowData->ms - blowData->blowStartMs;
    uint8_t isBlowing = (blowDuration) < (blowData->targetDurationMs+100) ||
                        (blowDuration) > (blowData->targetDurationMs+100+SIMULATE_BLOWS_PAUSE_DURATION);
    blowData->pressure = isBlowing ? 98 : 1; 
  } else {
    if (hx711.isBusy()) {
      return;
    }
    float sensorValue = (hx711.readChannel(CHAN_A_GAIN_64) / (PRESSURE_SENSOR_DIVISOR * blowData->targetPressure));
    if (systemConfig.debugLogBlowPressure) {
      Serial.print(F("Channel A (Gain 64): "));
      Serial.print(sensorValue);
      Serial.print(F(" / "));
    }
    if (blowData->negativePressure) {
      sensorValue = -sensorValue;
    }
    if (sensorValue >= -PRESSURE_SENSOR_CUTOFF_LIMIT && sensorValue <= PRESSURE_SENSOR_CUTOFF_LIMIT) {
      skips = 0;
      total = total - readings[readIndex];
      readings[readIndex] = sensorValue;
      total = total + readings[readIndex];
      readIndex = (readIndex + 1) % PRESSURE_SENSOR_SMOOTHING_NUM_READINGS;
      blowData->pressure = total / PRESSURE_SENSOR_SMOOTHING_NUM_READINGS;
      if (blowData->pressure < tare) {
        tare -= 0.001;
      } else if (blowData->pressure > tare && blowData->pressure <= tare + PRESSURE_TARE_TOLERANCE) {
        tare += 0.001;
      } else if (blowData->pressure > tare + PRESSURE_TARE_TOLERANCE) {
        float diff = blowData->pressure - tare;
        tare += 0.001 / (diff * diff);
      }
      blowData->pressure = _max(0, blowData->pressure-tare);
      if ((blowData->ms - blowData->blowStartMs) < blowData->targetDurationMs) {
        blowData->cumulativeError = abs(((int32_t)blowData->pressure - 100)) + ((blowData->cumulativeError * (PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR-1)) / PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR);
      }
    } else {
      skips++;
      if (skips > 10) {
        skips--;
        total = total - readings[readIndex];
        readings[readIndex] = 0;
        total = total + readings[readIndex];
        readIndex = (readIndex + 1) % PRESSURE_SENSOR_SMOOTHING_NUM_READINGS;
        blowData->pressure = total / PRESSURE_SENSOR_SMOOTHING_NUM_READINGS;
        if ((blowData->ms - blowData->blowStartMs) < blowData->targetDurationMs) {
          blowData->cumulativeError = abs(((int32_t)blowData->pressure - 100)) + ((blowData->cumulativeError * (PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR-1)) / PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR);
        }
      }
    }
  }
  if (systemConfig.debugLogBlowPressure) {
    Serial.println(blowData->pressure);
  }
}