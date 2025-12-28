#ifndef SYSTEM_STATE_HANDLER_H
#define SYSTEM_STATE_HANDLER_H

#include "Arduino.h"

#define STATE_BOOTING 0
#define STATE_PROFILE_SELECTION 1
#define STATE_GAME_SELECTION 2
#define STATE_GAME_RUNNING 3
#define STATE_GAME_ENDING 4
#define STATE_WIN_SCREEN 5

void setSystemState(uint32_t state);
uint32_t getSystemState();


#endif // SYSTEM_STATE_HANDLER_H