#include "joystickHandler.h"

#include "Wire.h"
#include "m5_unit_joystick2.hpp"

static M5UnitJoystick2 joystick2;
static bool joystickInitialized = false;
static bool joystickPresent = false;

bool isJoystickPresent() {
  if (!joystickInitialized) {
    joystickPresent = joystick2.begin(&Wire, JOYSTICK2_ADDR, 15, 16);
    joystick2.set_rgb_color(0x000000);
    joystickInitialized = true;
  }
  return joystickPresent;
}

void getJoystickXY(float* x, float* y) {
  static float joystickX = 0;
  static float joystickY = 0;
  if (isJoystickPresent()) {
    uint16_t adc_x;
    uint16_t adc_y;
    joystick2.get_joy_adc_16bits_value_xy(&adc_x, &adc_y);
    if (adc_x != 257 && adc_y != 257) {
      joystickX = ((((int32_t)adc_y) - 32768) * 0.000030518); //divide by 2^15
      joystickY = -((((int32_t)adc_x) - 32768) * 0.000030518); //divide by 2^15
    }
    if (abs(joystickX)<0.05) {
      joystickX = 0;
    }
    if (abs(joystickY)<0.05) {
      joystickY = 0;
    }
    *x = joystickX;
    *y = joystickY;
  } else {
    *x = 0;
    *y = 0;
  }
}

bool getJoystickButton() {
  if (isJoystickPresent()) {
    return joystick2.get_button_value() == 0;
  } else {
    return false;
  }
}

void setJoystickRGB(uint32_t rgb) {
  if (isJoystickPresent()) {
    joystick2.set_rgb_color(rgb);
  }
}

static bool lastJoystickButtonState = false;
static bool joystickClicked = false;
void joystickButtonTick() {
  if (isJoystickPresent()) {
    bool currentJoystickButtonState = getJoystickButton();
    if (!lastJoystickButtonState && currentJoystickButtonState) {
      joystickClicked = true;
    }
    lastJoystickButtonState = currentJoystickButtonState;
  }
}

bool joystickButtonClicked() {
  return joystickClicked;
}