#ifndef __PRESSURESENSOR_H__
#define __PRESSURESENSOR_H__

#include <Arduino.h>
#include "constants.h"

void initPressureSensor(String* errorMessage);
void readPressure(BlowData* blowData);

#endif /*PRESSURESENSOR*/