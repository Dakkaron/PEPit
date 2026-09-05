#ifndef __BLUETOOTH_HANDLER_H__
#define __BLUETOOTH_HANDLER_H__

#include <Arduino.h>

void initBluetooth();
void connectBluetooth();
bool isConnected();
void sendLine(String data);
String getLine();

#endif // __BLUETOOTH_HANDLER_H__