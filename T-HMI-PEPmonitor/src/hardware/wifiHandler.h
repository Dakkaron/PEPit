#ifndef __WIFI_HANDLER_H__
#define __WIFI_HANDLER_H__

#include "Arduino.h"
#include "constants.h"

#define CONNECTION_NOWIFI 0
#define CONNECTION_OK 1
#define CONNECTION_NOTRAMPOLINE 2

#define WIFI_RETRY_COUNT 10
#define TRAMPOLINE_RETRY_COUNT 3

typedef std::function<void(size_t, size_t)> THandlerFunction_Progress;

bool startFetchingNTPTime();
void getNTPTime(String* ntpDateString, String* ntpTimeString, String* errorMessage);

void downloadFile(String url, String filename, String* errorMessage, THandlerFunction_Progress progressCallback);
String downloadFileToString(String url, String* errorMessage);

uint8_t getWifiStatus();

uint8_t startWifi();


#endif /* __WIFI_HANDLER_H__ */