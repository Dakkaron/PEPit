#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__
#include <Arduino.h>

#define VERSION "6.1"

#define LAST_BLOW_SUCCEEDED 0B00000001
#define LAST_BLOW_FAILED    0B00000010
#define NEW_BLOW            0B00000100

#define PRESSURE_BAR_WIDTH 180
#define PRESSURE_BAR_HEIGHT 10
#define PRESSURE_BAR_X 1
#define PRESSURE_BAR_Y 190

#define PRESSURE_SENSOR_DIVISOR 65L
#define PRESSURE_SENSOR_CUTOFF_LIMIT 500
#define PRESSURE_SENSOR_SMOOTHING_NUM_READINGS 10
#define PRESSURE_SENSOR_MAX_SKIPS 10;

#define PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR 16

#define SCREEN_BRIGHTNESS_STEPS 16
#define SCREEN_ROTATION 3
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define HX7711_DATA_PIN 17
#define HX7711_CLOCK_PIN 18

#define SIMULATE_BLOWS_SHORT_BLOW_DURATION 1000
#define SIMULATE_BLOWS_PAUSE_DURATION 5000

#define BATTERY_LOW_WARNING_VOLTAGE 3400
#define BATTERY_LOW_SHUTDOWN_VOLTAGE 3300

#define SENSOR_MODE_PEPS 1
#define SENSOR_MODE_TRAMPOLINE 2

#define PROGRESS_MENU_SELECTION_ID 32767
#define SYSTEM_UPDATE_SELECTION_ID 32766
#define EXECUTION_LIST_SELECTION_ID 32765

#define PROFILE_TASK_TYPE_SHORTBLOWS 1
#define PROFILE_TASK_TYPE_LONGBLOWS 2
#define PROFILE_TASK_TYPE_EQUALBLOWS 3
#define PROFILE_TASK_TYPE_TRAMPOLINE 4
#define PROFILE_TASK_TYPE_INHALATION 5
#define PROFILE_TASK_TYPE_INHALATIONPEP 6

#define INHALATION_TASK_WARN_TIMEOUT 10000L
#define INHALATION_TASK_END_TIMEOUT 15000L

#define MAX_WIFI_NETWORKS 3
#define FILE_DOWNLOAD_CHUNK_SIZE 1024*10
#define FILE_DOWNLOAD_TO_STRING_MAX_SIZE 1024
#define FIRMWARE_RELEASE_PATH_URL "https://raw.githubusercontent.com/Dakkaron/T-HMI-PEPmonitor/refs/heads/main/path_to_latest_release"
#define LAST_RELEASE_URL_FILE_PATH "/lastReleaseUrl.txt"
#define FIRMWARE_FILE_NAME "firmware.bin"
#define SDCARD_CONTENT_FILE_NAME "SDCardContent.tar"

#define GAME_SELECTION_POWEROFF_TIMEOUT 3L*60L*1000L // Power off device if it spends >3 minutes on game selection screen
#define WIN_SCREEN_TIMEOUT 3L*60L*1000L

#define REQUIRED_TASK_TYPE_SHORTBLOWS       0b00000001
#define REQUIRED_TASK_TYPE_LONGBLOWS        0b00000010
#define REQUIRED_TASK_TYPE_EQUALBLOWS       0b00000100
#define REQUIRED_TASK_TYPE_TRAMPOLINE       0b00001000
#define REQUIRED_TASK_TYPE_INHALATION       0b00010000
#define REQUIRED_TASK_TYPE_INHALATIONPEP    0b00100000
#define REQUIRED_TASK_TYPE_PROGRESSION_MENU 0b01000000

struct GameConfig {
  String name;
  String templateName;
  String prefsNamespace;
  String pepShortScriptPath;
  String pepLongScriptPath;
  String pepEqualScriptPath;
  String inhalationPepScriptPath;
  String inhalationScriptPath;
  String trampolineScriptPath;
  String progressionMenuScriptPath;
  String winScreenScriptPath;
};

struct ProfileData {
  String name;
  String imagePath;
  uint8_t cycles;
  uint8_t tasks;
  uint8_t taskType[10];
  uint8_t taskRepetitions[10];
  String taskChangeImagePath[10];
  String taskChangeMessage[10];
  uint32_t taskTime[10];
  uint32_t taskMinStrength[10];
  bool taskNegativeStrength[10];
  uint32_t taskTargetStrength[10];
  uint32_t taskTime2[10];
  uint32_t taskMinStrength2[10];
  bool taskNegativeStrength2[10];
  uint32_t taskTargetStrength2[10];
};


struct BlowData {
  bool currentlyBlowing = false;
  unsigned long ms = 0;
  unsigned long blowStartMs = 0;
  unsigned long blowEndMs = 0;
  unsigned long targetDurationMs = 0;
  unsigned long totalTimeSpentBreathing = 0;
  unsigned long taskStartMs;
  int32_t breathingScore = 0;
  uint8_t cycleNumber = 0;
  uint8_t totalCycleNumber = 0;
  uint8_t taskNumber = 0;
  uint8_t totalTaskNumber = 0;
  uint8_t blowCount = 0;
  uint8_t totalBlowCount = 0;
  int32_t pressure = 0;
  bool negativePressure = false;
  int32_t peakPressure = 0;
  int32_t minPressure = 0;
  int32_t targetPressure = 0;
  uint32_t cumulativeError = 0;
  uint8_t fails = 0;
  uint32_t taskType;
  uint8_t lastBlowStatus = 0;
  uint32_t totalLongBlowRepetitions = 0;
  uint32_t totalEqualBlowRepetitions = 0;
  uint32_t totalShortBlowRepetitions = 0;
};


struct JumpData {
  unsigned long ms = 0;
  unsigned long startMs = 0;
  int32_t msLeft = 0;
  uint8_t cycleNumber = 0;
  uint8_t totalCycleNumber = 0;
  uint8_t taskNumber = 0;
  uint8_t totalTaskNumber = 0;
  uint16_t jumpCount = 0;
  uint16_t bonusJumpCount = 0;
  bool currentlyJumping = false;
  int32_t totalTime = 0;
  uint16_t misses = 0;
  uint16_t highscore = 0;
  bool newHighscore = false;
};

#endif /* __CONSTANTS_H__ */
