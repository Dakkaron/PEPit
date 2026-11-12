#ifndef __CONSTANTS_H__
#define __CONSTANTS_H__
#include <Arduino.h>

#define VERSION "10.7"

//#define INIT_MODE
#ifdef INIT_MODE
  #include "initModeConfig.h"
  #undef VERSION
  #define VERSION "0.0"
#endif

#define LAST_BLOW_SUCCEEDED 0B00000001
#define LAST_BLOW_FAILED    0B00000010
#define NEW_BLOW            0B00000100

#define PRESSURE_BAR_WIDTH 180
#define PRESSURE_BAR_HEIGHT 10
#define PRESSURE_BAR_X 1
#define PRESSURE_BAR_Y 190

// Auto-tare runs in physical signed mbar: raw counts / (PRESSURE_SENSOR_DIVISOR * 1000)
// with the natural sensor sign (push positive, suction negative; flip SIGN if inverted).
#define PRESSURE_SENSOR_DIVISOR 65L
#define PRESSURE_SENSOR_SIGN 1.0f
#define PRESSURE_MBAR_CUTOFF 60.0f
#define PRESSURE_MA_READINGS 5
#define PRESSURE_SPIKE_JUMP 5.0f
#define PRESSURE_SPIKE_CONFIRM 4
#define PRESSURE_TARE_BUCKET_MS 500
#define PRESSURE_TARE_BUCKETS 120
#define PRESSURE_MIN_BUCKET_SAMPLES 4
#define PRESSURE_TRANS_RANGE 2.5f
#define PRESSURE_CLUSTER_SPAN 0.7f
#define PRESSURE_IDLE_TOLERANCE 1.8f
#define PRESSURE_IDLE_STEP 0.3f
#define PRESSURE_TREND_MAX 0.6f
#define PRESSURE_CAND_MAX_AGE 40
#define PRESSURE_ANCHOR_FOLLOW_MAX 0.7f
#define PRESSURE_ANCHOR_RATE 0.01f
#define PRESSURE_ANCHOR_TAKE 40
#define PRESSURE_ANCHOR_CONFIRM 24
#define PRESSURE_ANCHOR_LIMIT 1.5f
#define PRESSURE_ANCHOR_MATCH 1.0f
#define PRESSURE_SUSPECT_ADJ 2
#define PRESSURE_CHASE_REFILL 0.025f
#define PRESSURE_CHASE_CAP 0.5f
#define PRESSURE_REC_REFILL 0.1f
#define PRESSURE_REC_CAP 2.0f
#define PRESSURE_CROSS_TOL 0.6f
#define PRESSURE_CROSS_STEP 0.4f
#define PRESSURE_XSCAN_MAX 4
// Display-only calibration of the manometer dial: physical pressure in mbar is
//   compensatedPercent * targetStrength / PRESSURE_MBAR_CALIBRATION
// Measured on real hardware against a physical manometer (5->25, 10->49, 15->75
// at targetStrength 200), i.e. ~65000 HX711 counts per mbar at gain 64.
// Deliberately NOT applied to PRESSURE_SENSOR_DIVISOR: the profile targetStrength
// values are empirically tuned to the raw percent-of-target scaling, and changing
// the divisor would shift every game threshold by the same factor.
#define PRESSURE_MBAR_CALIBRATION 1000L

#define PRESSURE_SENSOR_CUMULATIVE_ERROR_FACTOR 16

#define SCREEN_BRIGHTNESS_STEPS 16
#define SCREEN_ROTATION 3
#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240

#define M5STACK_JOYSTICK2_POWER_PIN 43

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
#define NEXT_PAGE_SELECTION_ID 32764
#define PREV_PAGE_SELECTION_ID 32763

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

// Multiplied the timeouts by 100L for debug purposes. I don't want the device to sleep while debugging.
#define GAME_SELECTION_POWEROFF_TIMEOUT 100L*3L*60L*1000L // Power off device if it spends >3 minutes on game selection screen
#define WIN_SCREEN_TIMEOUT 100L*3L*60L*1000L
#define SHOW_SYSTEM_UPDATE_ON_WINSCREEN_TIMEOUT 100L*3L*1000L

#define MANOMETER_WINSCREEN_PATH "/gfx/manometerWinscreen.bmp"

#define REQUIRED_TASK_TYPE_SHORTBLOWS       0b00000001
#define REQUIRED_TASK_TYPE_LONGBLOWS        0b00000010
#define REQUIRED_TASK_TYPE_EQUALBLOWS       0b00000100
#define REQUIRED_TASK_TYPE_TRAMPOLINE       0b00001000
#define REQUIRED_TASK_TYPE_INHALATION       0b00010000
#define REQUIRED_TASK_TYPE_INHALATIONPEP    0b00100000
#define REQUIRED_TASK_TYPE_PROGRESSION_MENU 0b01000000

#define COLOR_BUTTON_PRIMARY       0x001F
#define COLOR_BUTTON_PRIMARY_FRAME 0x94b2
#define COLOR_BUTTON_PRIMARY_TEXT  0xFFFF
#define COLOR_BUTTON_SELECTABLE    TFT_YELLOW

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
  bool isHidden = false;
};

struct ProfileData {
  String name = "";
  String imagePath = "";
  uint8_t cycles = 0;
  uint8_t tasks = 0;
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
  float pressure = 0;
  bool negativePressure = false;
  int32_t peakPressure = 0;
  int32_t minPressure = 0;
  int32_t targetPressure = 0;
  uint32_t cumulativeError = 0;
  uint32_t successes = 0;
  uint32_t fails = 0;
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
