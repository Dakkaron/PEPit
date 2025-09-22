#include "bluetoothHandler.h"
#include <NuSerial.hpp>
#include <NimBLEDevice.h>
#include "systemconfig.h"
#include "gfxHandler.hpp"

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth is not enabled! Please run `make menuconfig` to and enable it
#endif

#define DEVICE_NAME "PEPit-Tramopoline"
#define HOST_NAME "PEPit"

bool connectToTrampoline(bool blocking) {
  if (systemConfig.simulateTrampoline) {
    Serial.println("Simulating trampoline connection");
    return true;
  } else {
    if (NuSerial.isConnected()) {
      Serial.println("Already connected to trampoline");
      NuSerial.print("MINJUMPHEIGHT 0.3");
      NuSerial.print("MAXJUMPHEIGHT 2.0");
      return true;
    }
    Serial.printf("Connecting to BT device named \"%s\"\n", DEVICE_NAME);
    NimBLEDevice::init(HOST_NAME);
    NimBLEDevice::getAdvertising()->setName(HOST_NAME);
    //NimBLEDevice::setSecurityPasskey(123456);
    NuSerial.begin(115200);
    NimBLEServer *pServer = NimBLEDevice::getServer();
    if (blocking) {
      Serial.print("Waiting for trampoline connection");
      while (!NuSerial.isConnected()) {
        Serial.print(".");
        delay(500);
      }
      Serial.println(" connected.");
    } else {
      if (NuSerial.isConnected()) {
        Serial.println("Bluetooth connection established");
        NuSerial.print("MINJUMPHEIGHT 0.3");
        NuSerial.print("MAXJUMPHEIGHT 2.0");
      }
    }
  }
  return NuSerial.isConnected();
}

unsigned long simulateStartMs = 0;
static String btSerialIncoming = "";

void getJumpData(JumpData* jumpData, ProfileData* profileData, uint32_t currentTask) {
  if (!systemConfig.simulateTrampoline && !NuSerial.isConnected()) {
    Serial.println("Trampoline not connected!");
    displayFullscreenMessage("Warte auf Verbindung...\nTrampolinsensor einschalten!");
    connectToTrampoline(true);
  }
  if (systemConfig.simulateTrampoline) {
    if (simulateStartMs==0) {
      simulateStartMs = millis();
    }
    jumpData->ms = millis();
    jumpData->jumpCount = (jumpData->ms - simulateStartMs) / 1000;
    jumpData->currentlyJumping = true;
    jumpData->msLeft = simulateStartMs + jumpData->totalTime - jumpData->ms;
    jumpData->misses = 0;
  } else {
    while (NuSerial.available() && !btSerialIncoming.endsWith("\n")) {
      btSerialIncoming += (char)NuSerial.read();
    }

    jumpData->ms = millis();
    if (!jumpData->currentlyJumping && jumpData->jumpCount == 0) { // TODO: Implement lost time
      jumpData->startMs = jumpData->ms;
    }
    jumpData->msLeft = jumpData->totalTime - (jumpData->ms - jumpData->startMs);
    if (btSerialIncoming.indexOf("\n") >= 0) {
      btSerialIncoming.trim();
      Serial.print("BT received: ");
      Serial.println(btSerialIncoming);
      if (btSerialIncoming.startsWith("Jump Height: ")) {
        uint32_t jumpHeight = btSerialIncoming.substring(13).toInt();
        Serial.println("Jump detected!");
        jumpData->currentlyJumping = true;
        if (jumpHeight > profileData->taskMinStrength[currentTask]) {
          jumpData->jumpCount++;
        } else {
          Serial.println("Jump too low!");
          jumpData->misses++;
        }
        if (jumpHeight > profileData->taskTargetStrength[currentTask]) {
          jumpData->bonusJumpCount++;
        }
      } else if (btSerialIncoming.startsWith("Not high enough!")) {
        Serial.println("Jump too low!");
        jumpData->misses++;
      }
      btSerialIncoming = "";
    }
  }

  if (systemConfig.debugLogTrampoline) {
    Serial.println("New jump data:");
    Serial.print("jumpCount: ");
    Serial.println(jumpData->jumpCount);
    Serial.print("currentlyJumping: ");
    Serial.println(jumpData->currentlyJumping);
    Serial.print("msLeft: ");
    Serial.println(jumpData->msLeft);
    Serial.print("misses: ");
    Serial.println(jumpData->misses);
    Serial.print("highscore: ");
    Serial.println(jumpData->highscore);
    Serial.print("newHighscore: ");
    Serial.println(jumpData->newHighscore);
  }
}

void disableBluetooth() {
  if (NuSerial.isConnected()) {
    NuSerial.end();
  }
  NimBLEDevice::deinit(true);
}