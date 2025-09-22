#include <Arduino.h>
#include "Adafruit_VL53L0X.h"
#include "bluetoothHandler.h"

#define LED_BT 2

#define I2C_SDA 19
#define I2C_SCL 23
#define XSHUT_PIN 26
#define SOFT_GND 5

#define TRIGGER_HEIGHT_LOW 220
#define TRIGGER_HEIGHT_HIGH 245
#define MINIMUM_JUMP_HEIGHT 0.15
#define MAXIMUM_JUMP_HEIGHT 2.0

float minJumpHeight = MINIMUM_JUMP_HEIGHT;
float maxJumpHeight = MAXIMUM_JUMP_HEIGHT;

Adafruit_VL53L0X lox = Adafruit_VL53L0X();

void setup() {
  // Serial
  Serial.begin(115200);
  sleep(2);
  Serial.println("VL53L0X Test active");

  initBluetooth();
  connectBluetooth();

  // TOF Sensor
  pinMode(XSHUT_PIN, OUTPUT);
  digitalWrite(XSHUT_PIN, HIGH);
  pinMode(SOFT_GND, OUTPUT);
  digitalWrite(SOFT_GND, LOW);
  Wire.begin(I2C_SDA, I2C_SCL);
  while (!lox.begin()) {
    Serial.println(F("Failed to boot VL53L0X"));
    Serial.println(F("retrying..."));
    Wire.begin(I2C_SDA, I2C_SCL);
    delay(1);
  }
  lox.startRangeContinuous();
}

uint32_t lastJumpStartMs = 0;
bool currentlyInAir = false;

void loop() {
  if (!isConnected()) {
    Serial.println("Attempting to connect Bluetooth...");
    connectBluetooth();
    if (!isConnected()) {
      Serial.println("Failed to connect Bluetooth");
      delay(2000);
    } else {
      Serial.println("Bluetooth connected");
    }
  }
  String line = getLine();
  if (!line.isEmpty()) {
    if (line.startsWith("MINJUMPHEIGHT ")) {
      line = line.substring(14);
      line.trim();
      minJumpHeight = line.toFloat();
      Serial.println("Set min jump height to " + String(minJumpHeight) + " m");
    } else if (line.startsWith("MAXJUMPHEIGHT ")) {
      line = line.substring(14);
      line.trim();
      maxJumpHeight = line.toFloat();
      Serial.println("Set max jump height to " + String(maxJumpHeight) + " m");
    }
  }
  if (lox.isRangeComplete()) {
    uint32_t readValue = lox.readRange();
    if (currentlyInAir && readValue < TRIGGER_HEIGHT_LOW) {
      currentlyInAir = false;
      sendLine("Landed");
      Serial.println("Landed");
      float airTime = (millis() - lastJumpStartMs) / 1000.0;
      float deltaV = airTime * 9.81 / 2.0;
      float averageV = deltaV / 2;
      float height = (averageV * airTime);
      if (height < minJumpHeight) {
        Serial.println("Not high enough!");
        sendLine("Not high enough!");
        return;
      } else if (height > maxJumpHeight) {
        Serial.println("Too high!");
        sendLine("Too high!");
        return;
      }
      String heightStr = String("Jump Height: ");
      sendLine(heightStr + ((int32_t)(height*1000.0))); // convert to mm for sending
      Serial.print("Jump height: ");
      Serial.print(height);
      Serial.println(" m");
    } else if (!currentlyInAir && readValue > TRIGGER_HEIGHT_HIGH) {
      currentlyInAir = true;
      lastJumpStartMs = millis();
      sendLine("Jumped");
      Serial.println("Jumped");
    }
  }
}
