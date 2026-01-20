#include "systemStateHandler.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/powerHandler.h"
#include "hardware/joystickHandler.h"

static uint32_t systemState = STATE_BOOTING;

void setSystemState(uint32_t state) {
  systemState = state;
}

uint32_t getSystemState() {
  return systemState;
}

void doSystemTasks() {
  drawSystemStats();
  buttonPwr.tick();
  buttonUsr.tick();
  joystickButtonTick();
}