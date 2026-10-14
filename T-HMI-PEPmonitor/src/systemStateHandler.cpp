#include "systemStateHandler.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/powerHandler.h"
#include "hardware/joystickHandler.h"
#include "hardware/serialHandler.h"
#include "updateHandler.h"

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
  vTaskDelay(1);
}

#ifdef INIT_MODE
void handleInitMode() {
  doSystemTasks();
  handleSerial();
  displayFullscreenMessage("INIT MODE, waiting for update...");
  if (getSystemUpdateAvailableStatus() == FIRMWARE_UPDATE_AVAILABLE) {
    String errorMessage;
    downloadAndRunSystemUpdate(&errorMessage, true);
    checkFailWithMessage(errorMessage);
  }
}
#endif