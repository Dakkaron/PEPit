#ifndef __POWER_HANDLER_H__
#define __POWER_HANDLER_H__
#include <Arduino.h>
#include <OneButton.h>
#include "pins.h"

extern OneButton buttonPwr;
extern OneButton buttonUsr;

uint32_t readBatteryVoltage();
bool isSkipSplashScreen();
void deepSleepReset();
void restoreAfterDeepSleep();
void power_off();

#endif /*__POWER_HANDLER_H__*/