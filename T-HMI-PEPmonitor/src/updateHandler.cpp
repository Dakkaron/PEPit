#include "updateHandler.h"
#include "Update.h"
#include "hardware/wifiHandler.h"
#include "hardware/sdHandler.h"
#include "hardware/gfxHandler.hpp"
#include "SD_MMC.h"
#include "systemStateHandler.h"

#define DEST_FS_USES_SD_MMC
#include <ESP32-targz.h>

static String systemUpdateReleaseUrl = "";
static bool systemUpdateWasChecked = false;
static bool systemUpdateCheckTriggered = false;
TaskHandle_t firmwareCheckTaskHandle;

static void fetchReleaseUrl() {
  while (systemUpdateCheckTriggered && !systemUpdateWasChecked && !(xTaskGetCurrentTaskHandle() == firmwareCheckTaskHandle)) {
    Serial.println("loop");
    delay(10);
  }
  if (systemUpdateWasChecked) {
    return;
  }
  if (getSystemState()>STATE_GAME_SELECTION) {
    Serial.println("Abandoning firmware update check because game is already running.");
    systemUpdateWasChecked = true;
    return;
  }
  Serial.println("Checking for firmware update");
  String ignoreMessage;
  String url = downloadFileToString(FIRMWARE_RELEASE_PATH_URL, &ignoreMessage);
  url.trim();
  Serial.println("Update URL: "+url);
  if (ignoreMessage.length() > 0) {
    Serial.println("Error checking for firmware update: "+ignoreMessage);
    systemUpdateWasChecked = true;
    return;
  }
  systemUpdateReleaseUrl = url.indexOf("/v"+String(VERSION)+"/")!=-1 ? "" : url;
  systemUpdateWasChecked = true;
  Serial.println("Result: "+systemUpdateReleaseUrl);
}

void firmwareCheckTask(void* parameter) {
  Serial.println("Checking for firmware update in task");
  startWifi();
  fetchReleaseUrl();
  vTaskDelete(NULL);
}

uint8_t getSystemUpdateAvailableStatus() {
  if (!systemUpdateCheckTriggered) {
    Serial.println("Starting system update check task");
    systemUpdateCheckTriggered = true;
    xTaskCreatePinnedToCore(firmwareCheckTask, "updateCheck", 10000, NULL, 1, &firmwareCheckTaskHandle, 0);
  }
  if (!systemUpdateWasChecked) {
    return FIRMWARE_UPDATE_CHECK_RUNNING;
  } else if (systemUpdateReleaseUrl.length() == 0) {
    return FIRMWARE_UPDATE_NOT_AVAILABLE;
  } else {
    return FIRMWARE_UPDATE_AVAILABLE;
  }
}

bool sdUpdateTarExcludeFilter( TAR::header_translated_t *proper ) {
  bool excluded = (strcmp(proper->filename, "profiles.ini") == 0) ||
                  (strcmp(proper->filename, "systemconfig.ini") == 0) ||
                  (strcmp(proper->filename, "PaxHeader") == 0);
  return excluded;
}

static void unpackTARAppUpdate(String* errorMessage) {
  static String currentFileName = "";
  static uint32_t nextUpdate = 0;
  currentFileName = "";
  nextUpdate = 0;
  tarGzFS.begin();
  TarUnpacker *TARUnpacker = new TarUnpacker();

  TARUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARUnpacker->setTarProgressCallback([](unsigned char percent) {
    uint32_t ms = millis();
    if (ms>nextUpdate) {
      displayFullscreenMessage("App-Update entpacken\n\n    "+currentFileName+"\n    "+String(percent)+"%");
      Serial.printf("Progress: %d%%\r", percent / 100);
      nextUpdate = ms + 200;
    }
  }); // prints the untarring progress for each individual file
  TARUnpacker->setTarStatusProgressCallback([](const char* name, size_t size, size_t total_unpacked) {
    Serial.printf("[TAR] %-64s %8d bytes - %8d Total bytes\n", name, size, total_unpacked );
    currentFileName = name;
  }); // print the filenames as they're expanded
  TARUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  TARUnpacker->setTarExcludeFilter(sdUpdateTarExcludeFilter);

  // using an intermediate file (default is /tmp/tmp.tar)
  if( !TARUnpacker->tarExpander(tarGzFS, (String("/")+SDCARD_CONTENT_FILE_NAME).c_str(), tarGzFS, "/") ) {
    Serial.printf("tarExpander+intermediate file failed with return code #%d\n", TARUnpacker->tarGzGetError() );
    errorMessage->concat("Fehler: Konnte App-Update nicht entpacken. Fehlercode: ");
    errorMessage->concat(TARUnpacker->tarGzGetError());
  }
}

static void unpackTARGZAppUpdate(String* errorMessage) {
  static String currentFileName = "";
  static uint32_t nextUpdate = 0;
  currentFileName = "";
  nextUpdate = 0;
  tarGzFS.begin();
  TarGzUnpacker *TARGZUnpacker = new TarGzUnpacker();

  TARGZUnpacker->haltOnError( true ); // stop on fail (manual restart/reset required)
  TARGZUnpacker->setTarVerify( true ); // true = enables health checks but slows down the overall process
  TARGZUnpacker->setupFSCallbacks( targzTotalBytesFn, targzFreeBytesFn ); // prevent the partition from exploding, recommended
  TARGZUnpacker->setGzProgressCallback([](unsigned char percent) {
    uint32_t ms = millis();
    if (ms>nextUpdate) {
      displayFullscreenMessage("App-Update entpacken\n\n    "+currentFileName+"\n    "+String(percent)+"%");
      Serial.printf("Progress: %d%%\r", percent / 100);
      nextUpdate = ms + 200;
    }
  }); // targzNullProgressCallback or defaultProgressCallback
  TARGZUnpacker->setLoggerCallback( BaseUnpacker::targzPrintLoggerCallback  );    // gz log verbosity
  TARGZUnpacker->setTarProgressCallback([](unsigned char percent) {
    uint32_t ms = millis();
    if (ms>nextUpdate) {
      displayFullscreenMessage("App-Update entpacken\n\n    "+currentFileName+"\n    "+String(percent)+"%");
      Serial.printf("Progress: %d%%\r", percent / 100);
      nextUpdate = ms + 200;
    }
  });; // prints the untarring progress for each individual file
  TARGZUnpacker->setTarStatusProgressCallback([](const char* name, size_t size, size_t total_unpacked) {
    Serial.printf("[TAR] %-64s %8d bytes - %8d Total bytes\n", name, size, total_unpacked );
    currentFileName = name;
  }); // print the filenames as they're expanded
  TARGZUnpacker->setTarMessageCallback( BaseUnpacker::targzPrintLoggerCallback ); // tar log verbosity
  TARGZUnpacker->setTarExcludeFilter(sdUpdateTarExcludeFilter);

  // using an intermediate file (default is /tmp/tmp.tar)
  if( !TARGZUnpacker->tarGzExpander(tarGzFS, (String("/")+SDCARD_CONTENT_FILE_NAME).c_str(), tarGzFS, "/", nullptr) ) {
    Serial.printf("tarGzExpander+intermediate file failed with return code #%d\n", TARGZUnpacker->tarGzGetError() );
    errorMessage->concat("Fehler: Konnte App-Update nicht entpacken. Fehlercode: ");
    errorMessage->concat(TARGZUnpacker->tarGzGetError());
  }
}

void downloadAndRunSystemUpdate(String* errorMessage) {
  fetchReleaseUrl();
  if (systemUpdateReleaseUrl.equals("")) {
    Serial.println("No new firmware update available.");
    displayFullscreenMessage("Kein neues Firmware-Update\nverfügbar.");
    delay(5000);
    return;
  }
  displayFullscreenMessage("Firmware-Download\nwird initialisiert...");
  Serial.println("Downloading firmware update from " + systemUpdateReleaseUrl + FIRMWARE_FILE_NAME);
  downloadFile(systemUpdateReleaseUrl + FIRMWARE_FILE_NAME, String("/") + FIRMWARE_FILE_NAME, errorMessage, [](int progress, int total) {
    displayFullscreenMessage("Update herunterladen\n\n    "+String(progress / (total / 100))+"%");
    Serial.printf("Progress: %d%%\r", (progress / (total / 100)));
  });
  if (errorMessage->length() > 0) {
    return;
  }

  displayFullscreenMessage("App-Download\nwird initialisiert...");
  Serial.println("Downloading sdcard update from " + systemUpdateReleaseUrl + SDCARD_CONTENT_FILE_NAME);
  downloadFile(systemUpdateReleaseUrl + SDCARD_CONTENT_FILE_NAME, String("/") + SDCARD_CONTENT_FILE_NAME, errorMessage, [](int progress, int total) {
    displayFullscreenMessage("App-Update herunterladen\n\n    "+String(progress / (total / 100))+"%");
    Serial.printf("Progress: %d%%\r", (progress / (total / 100)));
  });
  if (errorMessage->length() > 0) {
    return;
  }

  Update.onProgress([](int progress, int total) {
    displayFullscreenMessage("Update installieren\n\n    "+String(progress / (total / 100))+"%");
    Serial.printf("Progress: %d%%\r", (progress / (total / 100)));
  });
  displayFullscreenMessage("Update wird\ninstalliert...");
  Serial.println("Starting update");
  File fileStream = SD_MMC.open("/firmware.bin");
  Update.begin(fileStream.size(), U_FLASH);
  Update.writeStream(fileStream);

  Update.onProgress([](int progress, int total) {
    displayFullscreenMessage("App-Update installieren\n\n    "+String(progress / (total / 100))+"%");
    Serial.printf("Progress: %d%%\r", (progress / (total / 100)));
  });

  displayFullscreenMessage("App-Update wird\ninstalliert...");
  Serial.println("Starting App update");
  if (String(SDCARD_CONTENT_FILE_NAME).endsWith(".gz")) {
    unpackTARGZAppUpdate(errorMessage);
  } else {
    unpackTARAppUpdate(errorMessage);
  }
  if (errorMessage->length() > 0) {
    return;
  }

  if (SD_MMC.exists("/FIRMWARE.CUR")) {
    SD_MMC.remove("/FIRMWARE.CUR");
  }
  if (SD_MMC.exists("/FIRMWARE.ERR")) {
    SD_MMC.remove("/FIRMWARE.ERR");
  }

  if (Update.end()) {
    SD_MMC.rename("/firmware.bin", "/FIRMWARE.CUR");
    displayFullscreenMessage("Update erfolgreich!\n\nWird neugestartet...");
    Serial.println("Update done");
    delay(2000);
    Serial.println("Resetting device");
    ESP.restart();
  } else {
    SD_MMC.rename("/firmware.bin", "/FIRMWARE.ERR");
    errorMessage->concat("Update failed: ");
    errorMessage->concat(Update.errorString());
    errorMessage->concat("\n");
    return;
  }
}

void checkForAndRunUpdateFromSD(String* errorMessage) {
  if (!SD_MMC.exists("/firmware.bin")) {
    Serial.println("No update found on SD card.");
    return;
  }
  Serial.println("Starting update from SD card.");
  tft.fillScreen(TFT_BLACK);
  displayFullscreenMessage("Firmware-Update auf\nSD-Karte gefunden.\nUpdate wird ausgeführt.");
  File fileStream = SD_MMC.open("/firmware.bin");
  Update.begin(fileStream.size(), U_FLASH);
  Update.writeStream(fileStream);

  Update.onProgress([](int progress, int total) {
    displayFullscreenMessage("Update installieren\n\n    "+String(progress / (total / 100))+"%");
    Serial.printf("Progress: %d%%\r", (progress / (total / 100)));
  });
  if (SD_MMC.exists("/FIRMWARE.CUR")) {
    SD_MMC.remove("/FIRMWARE.CUR");
  }
  if (SD_MMC.exists("/FIRMWARE.ERR")) {
    SD_MMC.remove("/FIRMWARE.ERR");
  }
  if (Update.end()) {
    SD_MMC.rename("/firmware.bin", "/FIRMWARE.CUR");
    displayFullscreenMessage("Update erfolgreich!\n\nWird neugestartet...");
    Serial.println("Update done");
    delay(2000);
    Serial.println("Resetting device");
    ESP.restart();
  } else {
    SD_MMC.rename("/firmware.bin", "/FIRMWARE.ERR");
    errorMessage->concat("Update failed: ");
    errorMessage->concat(Update.errorString());
    errorMessage->concat("\n");
    return;
  }
}