#ifndef __SYSTEMCONFIG_H__
#define __SYSTEMCONFIG_H__

#include <Arduino.h>

struct SystemConfig {
  String wifiSsid;
  String wifiPassword;
  String wifiSsid2;
  String wifiPassword2;
  String wifiSsid3;
  String wifiPassword3;
  uint8_t touchScreenZThreshold;
  bool simulateTrampoline;
  bool simulateBlows;
  bool simulateInhalation;
  bool debugLogBlowPressure;
  bool debugLogTrampoline;
  bool logExecutions;
  int32_t timezoneOffset;
  bool leftHandMode;
};

extern SystemConfig systemConfig;

void initSystemConfig(String* errorMessage);

#endif /* __SYSTEMCONFIG_H__ */