#ifndef __WIFI_HANDLER_H__
#define __WIFI_HANDLER_H__

#include "Arduino.h"
#include "constants.h"

#define WIFI_CONNECTION_SEARCHING 0
#define WIFI_CONNECTION_NOWIFI 1
#define WIFI_CONNECTION_OK 2

#define WIFI_RETRY_COUNT 10
#define TRAMPOLINE_RETRY_COUNT 3

typedef std::function<void(size_t, size_t)> THandlerFunction_Progress;

bool startFetchingNTPTime();
void getFormattedTime(String* ntpDateString, String* ntpTimeString, String* errorMessage);

void downloadFile(String url, String filename, String* errorMessage, THandlerFunction_Progress progressCallback);
String downloadFileToString(String url, String* errorMessage);

uint8_t getWifiStatus();

uint8_t startWifi();


#endif /* __WIFI_HANDLER_H__ */