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
  // No vTaskDelay here: the task watchdog only watches core 0's idle task
  // (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is off) and the Arduino loop
  // task is not wdt-subscribed, while every background task that shares CPU
  // (nimble_host, wifi, lwip, esp_timer) either lives on core 0 or preempts
  // this one at higher priority. The 1ms sleep just taxed every game frame.
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