#ifndef __BLUETOOTH_HANDLER_H__
#define __BLUETOOTH_HANDLER_H__

#include "Arduino.h"
#include "constants.h"

bool connectToTrampoline(bool blocking);
void getJumpData(JumpData* jumpData, ProfileData* profileData, uint32_t currentTask);
void disableBluetooth();

#endif /* __BLUETOOTH_HANDLER_H__ */