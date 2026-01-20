#ifndef __JOYSTICK_HANDLER_H__
#define __JOYSTICK_HANDLER_H__

#include "Arduino.h"

bool isJoystickPresent();
void getJoystickXY(float* x, float* y); // x and y are assinged values betweeen -1 and 1
bool getJoystickButton();
bool setJoystickRGB(uint32_t rgb);
void joystickButtonTick();
bool joystickButtonClicked();

#endif /* __JOYSTICK_HANDLER_H__ */