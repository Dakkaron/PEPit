#include "systemStateHandler.h"

static uint32_t systemState = STATE_BOOTING;

void setSystemState(uint32_t state) {
  systemState = state;
}

uint32_t getSystemState() {
  return systemState;
}