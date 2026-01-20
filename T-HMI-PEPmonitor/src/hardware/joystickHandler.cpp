#include "joystickHandler.h"

#include "Wire.h"
#include "m5_unit_joystick2.hpp"

static M5UnitJoystick2 joystick2;
static bool joystickInitialized = false;
static bool joystickPresent = false;

bool isJoystickPresent() {
  if (!joystickInitialized) {
    joystick2.begin(&Wire, JOYSTICK2_ADDR, 15, 16);
    joystick2.set_rgb_color(0x00ff00);
  }
  return joystickPresent;
}

void getJoystickXY(float* x, float* y) {
  if (isJoystickPresent()) {
    uint16_t adc_x;
    uint16_t adc_y;
    joystick2.get_joy_adc_16bits_value_xy(&adc_x, &adc_y);
    *y = -((((int32_t)adc_x) - 32768) * 0.000030518); //divide by 2^15
    *x = ((((int32_t)adc_y) - 32768) * 0.000030518); //divide by 2^15
  } else {
    *y = 0;
    *x = 0;
  }
}

bool getJoystickButton() {
  if (isJoystickPresent()) {
    return joystick2.get_button_value() == 0;
  } else {
    return false;
  }
}

bool setJoystickRGB(uint32_t rgb) {
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