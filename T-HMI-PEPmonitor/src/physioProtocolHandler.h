#ifndef __PHYSIO_PROTOCOL_HANDLER_H__
#define __PHYSIO_PROTOCOL_HANDLER_H__
#include <Arduino.h>
#include "constants.h"

uint32_t runProfileSelection();
void runGameSelection(uint32_t requiredTaskTypes);
void handlePhysioTask();
void displayPhysioRotateScreen();

void manometerAutoDirectionUpdate(float devMbar);
bool manometerAutoDirectionIsPep();
void manometerAutoDirectionReset();
float manometerDialMbar(const BlowData& blowData);

#endif