#ifndef __UPDATE_HANDLER_H__
#define __UPDATE_HANDLER_H__

#include <Arduino.h>

#define FIRMWARE_UPDATE_CHECK_RUNNING 0
#define FIRMWARE_UPDATE_AVAILABLE 1
#define FIRMWARE_UPDATE_NOT_AVAILABLE 2

void downloadAndRunSystemUpdate(String* errorMessage);
void checkForAndRunUpdateFromSD(String* errorMessage);
uint8_t getSystemUpdateAvailableStatus();

#endif /* __UPDATE_HANDLER_H__ */