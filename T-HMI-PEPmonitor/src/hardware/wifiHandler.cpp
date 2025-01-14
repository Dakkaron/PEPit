#include "wifiHandler.h"
#include <WiFi.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include "sdHandler.h"

static HTTPClient http;
static String trampolineIp = "";
static uint8_t wifiStatus = CONNECTION_NOWIFI;

uint8_t getWifiStatus() {
  return wifiStatus;
}

uint8_t startWifi() {
  String ssid = "";
  String password = "";
  if (wifiStatus == CONNECTION_OK) {
    return wifiStatus;
  }
  for (uint32_t wifiNumber=0; wifiNumber<MAX_WIFI_NETWORKS; wifiNumber++) {
    Serial.println("Starting WIFI");
    WiFi.mode(WIFI_STA);
    switch (wifiNumber) {
      case 0:
        ssid = systemConfig.wifiSsid;
        password = systemConfig.wifiPassword;
        break;
      case 1:
        ssid = systemConfig.wifiSsid2;
        password = systemConfig.wifiPassword2;
        break;
      case 2:
        ssid = systemConfig.wifiSsid3;
        password = systemConfig.wifiPassword3;
        break;
    }
    trampolineIp = systemConfig.trampolineIp;
    Serial.println(ssid);
    Serial.println(password);
    Serial.println(trampolineIp);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WIFI");
    for (uint8_t i=0;i<WIFI_RETRY_COUNT;i++) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
      } else {
        wifiStatus = CONNECTION_OK;
        break;
      }
    }
    if (wifiStatus == CONNECTION_OK) {
      Serial.println("done");
      return wifiStatus;
    } else {
      Serial.println("Failed to connect to WIFI "+ssid);
    }
  }
  return wifiStatus;
}

uint8_t connectToTrampoline() {
  if (systemConfig.simulateTrampoline) {
    return CONNECTION_OK;
  } else {
    startWifi();
    Serial.println("done");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    Serial.print("Trampoline URL: ");
    Serial.println(("http://" + trampolineIp + "/resetCurrentSession"));

    Serial.print("Connecting to trampoline ");
    bool connectionFound = false;
    for (uint8_t i=0; i<CONNECTION_NOTRAMPOLINE; i++) {
      http.begin(("http://" + trampolineIp + "/resetCurrentSession").c_str());
      int httpResponseCode = http.GET();
      String result = http.getString();
      if (httpResponseCode == 200 && result == "Done") {
        Serial.println("done");
        connectionFound = true;
        break;
      } else {
        Serial.print(".");
      }
      if (!connectionFound) {
        Serial.println("Failed to connect to trampoline");
        Serial.println(httpResponseCode);
        Serial.println(result);
      }
    }
    if (connectionFound) {
      Serial.println("Connected to trampoline successfully");
    }
    return connectionFound ? CONNECTION_OK : CONNECTION_NOTRAMPOLINE;
  }
}

JsonDocument jsonDocument;
unsigned long simulateStartMs = 0;
void getJumpData(JumpData* jumpData) {
  if (systemConfig.simulateTrampoline) {
    if (simulateStartMs==0) {
      simulateStartMs = millis();
    }
    jumpData->ms = millis();
    jumpData->jumpCount = (jumpData->ms - simulateStartMs) / 1000;
    jumpData->currentlyJumping = true;
    jumpData->msLeft = simulateStartMs + jumpData->totalTime - jumpData->ms;
    jumpData->misses = 0;
    jumpData->highscore = 250;
    jumpData->newHighscore = jumpData->jumpCount > 250;
    jumpData->lastReadSuccessful = true;
  } else {
    http.begin(("http://" + trampolineIp + "/jumpReport").c_str());
    int httpResponseCode = http.GET();
    String result = http.getString();

    if (httpResponseCode != 200) {
      Serial.print("getJumpReport(): HTTP ERROR ");
      Serial.println(httpResponseCode);
      Serial.println(result);
      jumpData->lastReadSuccessful = false;
      return;
    }
    deserializeJson(jsonDocument, result);
    jumpData->ms = millis();
    jumpData->jumpCount = jsonDocument["jumpCount"];
    jumpData->currentlyJumping = jsonDocument["currentlyJumping"];
    jumpData->msLeft = jsonDocument["msLeft"];
    jumpData->misses = jsonDocument["misses"];
    jumpData->highscore = jsonDocument["highscore"];
    jumpData->newHighscore = jsonDocument["newHighscore"];
    jumpData->lastReadSuccessful = true;
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

bool startFetchingNTPTime() {
  uint32_t connectionStatus = startWifi();
  if (connectionStatus == CONNECTION_NOWIFI) {
    Serial.println("Failed to start NTP because of missing WIFI connection");
    return false;
  }
  Serial.println("Starting NTP");
  configTime(systemConfig.timezoneOffset, 0, "pool.ntp.org", "time.nist.gov");
  return true;
}

static String leftPad(String s, uint16_t len, String c) {
  while (s.length()<len) {
    s = c + s;
  }
  return s;
}

void getNTPTime(String* ntpDateString, String* ntpTimeString, String* errorMessage) {
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time from NTP");
    errorMessage->concat("Konnte Zeit nicht per NTP abrufen.\n");
    errorMessage->concat("Wahrscheinlich WLAN-Verbindungsproblem.\n");
    *ntpDateString = "N/A";
    *ntpTimeString = "N/A";
  }
  *ntpDateString = String(timeinfo.tm_year+1900) + "-" + String(timeinfo.tm_mon) + "-" + String(timeinfo.tm_mday);
  *ntpTimeString = leftPad(String(timeinfo.tm_hour), 2, "0") + ":" + leftPad(String(timeinfo.tm_min), 2, "0");
}

void downloadFile(String url, String filename, String* errorMessage, THandlerFunction_Progress progressCallback) {
  // Get file stream from internet
  uint32_t connectionStatus = startWifi();
  if (connectionStatus == CONNECTION_NOWIFI) {
    errorMessage->concat("Failed to start NTP because of missing WIFI connection");
    return;
  }

  HTTPClient httpClient;
  httpClient.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  Serial.println("downloadFile("+url+", "+filename+")");
  httpClient.begin(url);
  int httpCode = httpClient.GET();
  if (httpCode != 200) {
    errorMessage->concat("HTTP error: "+String(httpCode));
    return;
  }
  WiFiClient* stream = httpClient.getStreamPtr();

  // Download data and write into SD card
  size_t downloadedDataSize = 0;
  const size_t FILE_SIZE = httpClient.getSize();
  uint8_t* fileBuffer = new uint8_t[FILE_DOWNLOAD_CHUNK_SIZE+1];
  File file = SD_MMC.open(filename, FILE_WRITE, true);
  Serial.println("Starting download");
  while (downloadedDataSize < FILE_SIZE) {
    size_t availableDataSize = stream->available();
    if (availableDataSize > 0) {
      uint32_t bytesToRead = _min(availableDataSize, FILE_DOWNLOAD_CHUNK_SIZE);
      Serial.println("Reading "+String(bytesToRead)+" bytes");
      stream->readBytes(fileBuffer, bytesToRead);
      file.write(fileBuffer, bytesToRead);
      downloadedDataSize += bytesToRead;
      Serial.println(String(downloadedDataSize)+" / "+String(FILE_SIZE));
      if (progressCallback) {
        progressCallback(downloadedDataSize, FILE_SIZE);
      }
    }
  }
  Serial.println("Downloaded file");
  delete[] (fileBuffer);
}

String downloadFileToString(String url, String* errorMessage) {
  uint32_t connectionStatus = startWifi();
  if (connectionStatus == CONNECTION_NOWIFI) {
    errorMessage->concat("Failed to start NTP because of missing WIFI connection");
    return "";
  }

  // Get file stream from internet
  HTTPClient httpClient;
  Serial.println("downloadFileToString()");
  httpClient.begin(url);
  int httpCode = httpClient.GET();
  if (httpCode != 200) {
    errorMessage->concat("HTTP error: "+String(httpCode));
    return "";
  }
  WiFiClient* stream = httpClient.getStreamPtr();

  // Download data and write into SD card
  size_t downloadedDataSize = 0;
  const size_t FILE_SIZE = httpClient.getSize();
  uint8_t* fileBuffer = new uint8_t[FILE_DOWNLOAD_TO_STRING_MAX_SIZE+1];
  String output = "";
  Serial.println("Starting download to string");
  while (downloadedDataSize < _min(FILE_SIZE, FILE_DOWNLOAD_TO_STRING_MAX_SIZE)) {
    size_t availableDataSize = stream->available();
    if (availableDataSize > 0) {
      uint32_t bytesToRead = _min(availableDataSize, FILE_DOWNLOAD_CHUNK_SIZE);
      bytesToRead = _min(bytesToRead, FILE_DOWNLOAD_TO_STRING_MAX_SIZE-downloadedDataSize);
      Serial.println("Reading "+String(bytesToRead)+" bytes");
      stream->readBytes(fileBuffer, bytesToRead);
      fileBuffer[bytesToRead] = 0;
      output.concat((char*)fileBuffer);
      downloadedDataSize += bytesToRead;
      Serial.println(String(downloadedDataSize)+" / "+String(FILE_SIZE));
    }
  }
  Serial.println("Downloaded file to string");
  delete[] (fileBuffer);
  return output;
}