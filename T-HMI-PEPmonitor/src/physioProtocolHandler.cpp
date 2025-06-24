#include "physioProtocolHandler.h"
#include "hardware/sdHandler.h"
#include "hardware/wifiHandler.h"
#include "hardware/powerHandler.h"
#include "hardware/touchHandler.h"
#include "hardware/pressuresensor.h"
#include "hardware/wifiHandler.h"
#include "hardware/serialHandler.h"
#include "games/games.h"
#include "constants.h"
#include "updateHandler.h"

ProfileData profileData;
uint32_t currentTask;
uint32_t currentCycle;

BlowData blowData;
JumpData jumpData;

uint32_t lastMs = 0;

void runGameSelection(uint32_t requiredTaskTypes) {
  spr.fillSprite(TFT_BLACK);
  spr.pushSpriteFast(0,0);
  spr.fillSprite(TFT_BLACK);
  tft.fillScreen(TFT_BLACK);
  String errorMessage;
  uint16_t numberOfGames = getNumberOfGames(&errorMessage, requiredTaskTypes);
  checkFailWithMessage(errorMessage);
  Serial.print("Number of games: ");
  Serial.println(numberOfGames);
  String gamePath;
  if (numberOfGames == 0) {
    checkFailWithMessage("No fitting game found!");
  } else {
    gamePath = getGamePath(displayGameSelection(&spr, numberOfGames, requiredTaskTypes, &errorMessage), requiredTaskTypes, &errorMessage);
  }
  checkFailWithMessage(errorMessage);
  Serial.print("Game path: ");
  Serial.println(gamePath);
  displayFullscreenMessage("Spiel wird geladen\nBitte warten...");
  initGames(gamePath, &errorMessage);
  checkFailWithMessage(errorMessage);
}

uint32_t runProfileSelection() {
  String errorMessage;
  bool profileSuccessfullyLoaded = false;
  uint32_t requiredTaskTypes = 0;
  while (!profileSuccessfullyLoaded) {
    spr.fillSprite(TFT_BLACK);
    spr.pushSpriteFast(0,0);
    spr.fillSprite(TFT_BLACK);
    tft.fillScreen(TFT_BLACK);
    uint32_t totalNumberOfProfiles = getNumberOfProfiles(&errorMessage);
    checkFailWithMessage(errorMessage);
    int32_t selectedProfileId = displayProfileSelection(&spr, totalNumberOfProfiles, &errorMessage);
    profileSuccessfullyLoaded = true;
    if (selectedProfileId == PROGRESS_MENU_SELECTION_ID) {
      runGameSelection(REQUIRED_TASK_TYPE_PROGRESSION_MENU);
      uint32_t ms = millis();
      spr.fillSprite(TFT_BLACK);
      while (displayProgressionMenu(&spr, &errorMessage)) {
        checkFailWithMessage(errorMessage);
        lastMs = ms;
        ms = millis();
        drawSystemStats(ms, lastMs);
        spr.pushSpriteFast(0,0);
        spr.fillSprite(TFT_BLACK);
        handleSerial();
        vTaskDelay(1); // watchdog
      }
      ESP.restart(); // Todo: don't restart, just go back to profile selection, but clear running game data
      
      profileSuccessfullyLoaded = false;
      continue;
    } else if (selectedProfileId == EXECUTION_LIST_SELECTION_ID) {
      spr.fillSprite(TFT_BLACK);
      spr.pushSpriteFast(0,0);
      tft.fillScreen(TFT_BLACK);
      String executionLog = readFileToString(EXECUTION_LOG_PATH);
      while (displayExecutionList(&spr, &executionLog, &errorMessage)) {
        uint32_t ms = millis();
        checkFailWithMessage(errorMessage);
        lastMs = ms;
        ms = millis();
        drawSystemStats(ms, lastMs);
        spr.pushSpriteFast(0,0);
        spr.fillSprite(TFT_BLACK);
        handleSerial();
        vTaskDelay(1); // watchdog
      }
    } else if (selectedProfileId == SYSTEM_UPDATE_SELECTION_ID) {
      spr.fillSprite(TFT_BLACK);
      spr.pushSpriteFast(0,0);
      tft.fillScreen(TFT_BLACK);
      downloadAndRunSystemUpdate(&errorMessage);
      checkFailWithMessage(errorMessage);
      profileSuccessfullyLoaded = false; // Should never happen
      continue;
    }
    checkFailWithMessage(errorMessage);
    readProfileData(selectedProfileId, &profileData, &errorMessage);
    checkFailWithMessage(errorMessage);
    for (uint32_t i=0;i<profileData.tasks;i++) {
      if (profileData.taskType[i] == PROFILE_TASK_TYPE_SHORTBLOWS) {
        requiredTaskTypes |= REQUIRED_TASK_TYPE_SHORTBLOWS;
      } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_LONGBLOWS) {
        requiredTaskTypes |= REQUIRED_TASK_TYPE_LONGBLOWS;
      } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_EQUALBLOWS) {
        requiredTaskTypes |= REQUIRED_TASK_TYPE_EQUALBLOWS;
      } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_INHALATION) {
        requiredTaskTypes |= REQUIRED_TASK_TYPE_INHALATION;
      } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_TRAMPOLINE) {
        requiredTaskTypes |= REQUIRED_TASK_TYPE_TRAMPOLINE;
        spr.fillSprite(TFT_BLACK);
        spr.pushSpriteFast(0,0);
        spr.fillSprite(TFT_BLACK);
        tft.fillScreen(TFT_BLACK);
        spr.setTextSize(2);
        spr.setTextColor(TFT_WHITE);
        spr.setCursor(1, 16);
        spr.print("Verbinde...");
        spr.pushSpriteFast(0,0);
        uint32_t trampolineConnectionStatus = connectToTrampoline();
        Serial.print("-1> CONN STAT: ");
        Serial.println(trampolineConnectionStatus);
        if (trampolineConnectionStatus != CONNECTION_OK) {
          Serial.println("Trampoline failed: no connection");
          Serial.println("Not changing mode");
          spr.fillSprite(TFT_BLACK);
          spr.setTextSize(2);
          spr.setTextColor(TFT_WHITE);
          spr.drawString("Keine Verbindung zum Trampolin!", 1, 16);
          spr.pushSpriteFast(0,0);
          delay(5000);
          profileSuccessfullyLoaded = false;
        }
        break;
      }
    }
    vTaskDelay(1); // watchdog
  }
  for (uint32_t i=0;i<profileData.tasks;i++) {
    switch (profileData.taskType[i]) {
    case PROFILE_TASK_TYPE_LONGBLOWS:
      blowData.totalLongBlowRepetitions += profileData.taskRepetitions[i]*profileData.cycles;break;
    case PROFILE_TASK_TYPE_EQUALBLOWS:
      blowData.totalEqualBlowRepetitions += profileData.taskRepetitions[i]*profileData.cycles;break;
    case PROFILE_TASK_TYPE_SHORTBLOWS:
      blowData.totalShortBlowRepetitions += profileData.taskRepetitions[i]*profileData.cycles;break;
    }
  }
  blowData.totalCycleNumber = profileData.cycles;
  blowData.totalTaskNumber = profileData.tasks;
  jumpData.totalCycleNumber = profileData.cycles;
  jumpData.totalTaskNumber = profileData.tasks;
  return requiredTaskTypes;
}

inline static unsigned long getTaskDurationUntilLastAction() {
  return _max(1, (blowData.blowEndMs!=0 ? blowData.blowEndMs : blowData.taskStartMs) - blowData.taskStartMs);
}

static void drawInhalationDisplay() {
  spr.fillSprite(TFT_BLACK);
  String errorMessage;
  drawInhalationGame(&spr, &blowData, &errorMessage);
  drawProgressBar(&spr, blowData.breathingScore, 0, PRESSURE_BAR_X, PRESSURE_BAR_Y+25, PRESSURE_BAR_WIDTH, PRESSURE_BAR_HEIGHT);
  checkFailWithMessage(errorMessage);
  
  drawProgressBar(&spr, blowData.pressure, 10, PRESSURE_BAR_X, PRESSURE_BAR_Y, PRESSURE_BAR_WIDTH, PRESSURE_BAR_HEIGHT);
  spr.setTextSize(2);
  spr.setCursor(PRESSURE_BAR_X + 40, PRESSURE_BAR_Y - 14);
  printShaded(&spr, String(blowData.blowCount));
  
  spr.setCursor(PRESSURE_BAR_X + 80, PRESSURE_BAR_Y - 14);
  if (blowData.totalTaskNumber>1 && blowData.totalCycleNumber>1) {
    printShaded(&spr, String(blowData.taskNumber) + "/" + String(blowData.cycleNumber));
  } else if (blowData.totalTaskNumber>1) {
    printShaded(&spr, String(blowData.taskNumber));
  } else if (blowData.totalCycleNumber>1) {
    printShaded(&spr, String(blowData.cycleNumber));
  }
  
  if (blowData.blowCount >= blowData.totalBlowCount) {
    spr.fillRect(240, 210, 80, 30, TFT_BLUE);
    spr.drawString("Fertig", 255, 220);
  }

  drawSystemStats(blowData.ms, lastMs);
  spr.pushSpriteFast(0, 0);
}

static void drawPEPDisplay() {
  spr.fillSprite(TFT_BLACK);
  String errorMessage;
  switch (blowData.taskType) {
    case PROFILE_TASK_TYPE_LONGBLOWS:
      drawLongBlowGame(&spr, &blowData, &errorMessage);break;
    case PROFILE_TASK_TYPE_EQUALBLOWS:
      drawEqualBlowGame(&spr, &blowData, &errorMessage);break;
    case PROFILE_TASK_TYPE_SHORTBLOWS:
      drawShortBlowGame(&spr, &blowData, &errorMessage);break;
  }
  drawProgressBar(&spr, blowData.currentlyBlowing ? (100 * (blowData.ms - blowData.blowStartMs) / blowData.targetDurationMs) : 0, 0, PRESSURE_BAR_X, PRESSURE_BAR_Y+25, PRESSURE_BAR_WIDTH, PRESSURE_BAR_HEIGHT);
  checkFailWithMessage(errorMessage);
  
  drawProgressBar(&spr, blowData.pressure, 10, PRESSURE_BAR_X, PRESSURE_BAR_Y, PRESSURE_BAR_WIDTH, PRESSURE_BAR_HEIGHT);
  spr.setTextSize(2);
  spr.setCursor(PRESSURE_BAR_X + 40, PRESSURE_BAR_Y - 14);
  printShaded(&spr, String(blowData.blowCount));
  
  spr.setCursor(PRESSURE_BAR_X + 80, PRESSURE_BAR_Y - 14);
  if (blowData.totalTaskNumber>1 && blowData.totalCycleNumber>1) {
    printShaded(&spr, String(blowData.taskNumber) + "/" + String(blowData.cycleNumber));
  } else if (blowData.totalTaskNumber>1) {
    printShaded(&spr, String(blowData.taskNumber));
  } else if (blowData.totalCycleNumber>1) {
    printShaded(&spr, String(blowData.cycleNumber));
  }
  
  drawSystemStats(blowData.ms, lastMs);
  spr.pushSpriteFast(0, 0);
}

static void drawTrampolineDisplay() {
  spr.fillSprite(TFT_BLACK);
  String errorMessage;
  drawTrampolineGame(&spr, &jumpData, &errorMessage);
  checkFailWithMessage(errorMessage);
  if (jumpData.msLeft > 0) {
    drawProgressBar(&spr, (100L*(jumpData.totalTime-jumpData.msLeft))/jumpData.totalTime, 0, PRESSURE_BAR_X, PRESSURE_BAR_Y, PRESSURE_BAR_WIDTH, PRESSURE_BAR_HEIGHT);
    spr.setTextSize(2);
    spr.drawString(String(jumpData.jumpCount) + "/" + String(jumpData.highscore), PRESSURE_BAR_X + 20, PRESSURE_BAR_Y - 14);
    spr.setCursor(100, 5);
    spr.setTextSize(3);
    int32_t secondsLeft = _max(0, jumpData.msLeft/1000);
    spr.print(secondsLeft / 60);
    spr.print(":");
    if ((secondsLeft % 60) < 10) {
      spr.print("0");
    }
    spr.print(secondsLeft % 60);
  }
  drawSystemStats(blowData.ms, lastMs);
  spr.pushSpriteFast(0, 0);
}

static void handleLogExecutions() {
  uint32_t textDatumBak = tft.getTextDatum();
  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  tft.setTextSize(2);
  tft.drawString("Schreibe Log auf SD-Karte", 160, 120);
  tft.drawString("NICHT AUSSCHALTEN!", 160, 140);
  tft.setTextDatum(textDatumBak);

  bool wifiExists = false;
  for (uint32_t i=0;i<3;i++) {
    wifiExists = startFetchingNTPTime();
    if (wifiExists) {
      break;
    }
  }

  String errorMessage;
  String ntpDateString, ntpTimeString;
  Serial.println("Wifi exists: " + String(wifiExists));
  if (wifiExists) {
    getNTPTime(&ntpDateString, &ntpTimeString, &errorMessage);
    checkSoftFailWithMessage(errorMessage);
    if (!errorMessage.isEmpty()) {
      ntpDateString = "N/A";
      ntpTimeString = "N/A";
      errorMessage = "";
    }
  } else {
    ntpDateString = "N/A";
    ntpTimeString = "N/A";
  }
  Serial.println("NTP Time: " + ntpDateString + " " + ntpTimeString);
  logExecutionToSD(&profileData, ntpDateString, ntpTimeString, &errorMessage);
  checkFailWithMessage(errorMessage);
}

static void drawFinished() {
  static uint32_t winscreenTimeout = 0;
  static String winScreenPath = "";
  if (winscreenTimeout == 0) { // Only runs on first execution
    String errorMessage;
    endGame(&errorMessage);
    checkFailWithMessage(errorMessage);
    winscreenTimeout = millis() + WIN_SCREEN_TIMEOUT;
    winScreenPath = getRandomWinScreenPathForCurrentGame(&errorMessage);
    Serial.println("Win screen path: "+winScreenPath);
    checkFailWithMessage(errorMessage);
    spr.frameBuffer(1);
    spr.fillSprite(TFT_BLACK);
    spr.frameBuffer(2);
    spr.fillSprite(TFT_BLACK);
    if (systemConfig.logExecutions) {
      handleLogExecutions();
    }
    spr.frameBuffer(1);
    spr.fillSprite(TFT_BLACK);
    spr.frameBuffer(2);
    spr.fillSprite(TFT_BLACK);
    tft.fillRect(32,0,38,20,TFT_BLACK);
    drawBmp(winScreenPath, 0, 0);
  } else if (millis() > winscreenTimeout) {
    power_off();
  }
  spr.fillRect(32,0,38,20,TFT_BLACK);
  drawSystemStats(blowData.ms, lastMs);
  spr.pushSpriteFast(0,0);
}

void displayPhysioRotateScreen() {
  if (profileData.taskChangeImagePath[currentTask].isEmpty() && profileData.taskChangeMessage[currentTask].isEmpty()) {
    return;
  }
  spr.fillSprite(TFT_BLACK);
  spr.pushSpriteFast(0, 0);
  spr.fillSprite(TFT_BLACK);
  spr.setTextSize(4);
  drawBmp(profileData.taskChangeImagePath[currentTask], 0, 0);
  spr.setTextDatum(TL_DATUM);
  spr.drawString(profileData.taskChangeMessage[currentTask], 5, 5);
  spr.pushSpriteFast(0, 0);
  uint32_t displayOkButtonMs = millis() + 5000;

  while (!isTouchInZone(230, 170, 80, 60)) {
    if (displayOkButtonMs!=0 && millis() > displayOkButtonMs) {
      spr.fillSprite(TFT_BLACK);
      spr.pushSpriteFast(0, 0);
      spr.fillSprite(TFT_BLACK);
      spr.setTextSize(4);
      drawBmp(profileData.taskChangeImagePath[currentTask], 0, 0);
      spr.setTextDatum(TL_DATUM);
      spr.drawString(profileData.taskChangeMessage[currentTask], 5, 5);
      spr.fillRect(230, 170, 80, 60, 0x18db);
      spr.drawString("OK", 245, 185);
      spr.pushSpriteFast(0, 0);
      displayOkButtonMs = 0;
    }
    vTaskDelay(1); // watchdog
  }
  
  tft.fillScreen(TFT_BLACK);
  spr.fillScreen(TFT_BLACK);
  spr.pushSpriteFast(0, 0);
  delay(200);
}

inline static bool isPepTask() {
  switch (profileData.taskType[currentTask]) {
    case PROFILE_TASK_TYPE_LONGBLOWS:
    case PROFILE_TASK_TYPE_EQUALBLOWS:
    case PROFILE_TASK_TYPE_SHORTBLOWS:
      return true;
    default:
      return false;
    }
}

inline static bool isInhalationTask() {
  return profileData.taskType[currentTask] == PROFILE_TASK_TYPE_INHALATION;
}

inline static unsigned long getLastBlowEvent() {
  return blowData.currentlyBlowing ? blowData.blowStartMs : blowData.blowEndMs;
}

void handlePhysioTask() {
  static uint32_t taskFinishedTimeout = 0;
  lastMs = blowData.ms;
  blowData.ms = millis();
  jumpData.ms = blowData.ms;
  if (blowData.taskStartMs == 0) {
    blowData.taskStartMs = blowData.ms;
  }
  if (profileData.taskType[currentTask] == PROFILE_TASK_TYPE_TRAMPOLINE) {
    if (jumpData.msLeft < -5000) {
      currentCycle++;
      drawFinished(); //Todo: Make jump task compatible with multi-task configs
      return;
    }

    jumpData.cycleNumber = currentCycle;
    jumpData.taskNumber = currentTask;
    jumpData.totalTime = profileData.taskTime[currentTask];

    getJumpData(&jumpData);
    drawTrampolineDisplay();
  } else {
    blowData.targetPressure = profileData.taskTargetStrength[currentTask];
    blowData.taskNumber = currentTask;
    blowData.cycleNumber = currentCycle;
    blowData.minPressure = 100*profileData.taskMinStrength[currentTask]/profileData.taskTargetStrength[currentTask];
    blowData.negativePressure = profileData.taskNegativeStrength[currentTask];
    blowData.targetDurationMs = profileData.taskTime[currentTask];
    blowData.totalBlowCount = profileData.taskRepetitions[currentTask];
    blowData.taskType = profileData.taskType[currentTask];

    if (currentCycle >= blowData.totalCycleNumber) {
      drawFinished();
      return;
    }
    readPressure(&blowData);
    if (blowData.pressure>blowData.minPressure && !blowData.currentlyBlowing) {
      Serial.print(F("Blowing... "));
      blowData.currentlyBlowing = true;
      blowData.blowStartMs = blowData.ms;
      blowData.peakPressure = 0;
      blowData.cumulativeError = 0;
      blowData.lastBlowStatus |= NEW_BLOW;
    } else if (blowData.pressure<=blowData.minPressure && blowData.currentlyBlowing) {
      blowData.blowEndMs = blowData.ms;
      blowData.totalTimeSpentBreathing += blowData.blowEndMs-blowData.blowStartMs;
      blowData.breathingScore = 200 * blowData.totalTimeSpentBreathing / getTaskDurationUntilLastAction();
      Serial.print(blowData.blowEndMs-blowData.blowStartMs);
      Serial.println(F("ms"));
      blowData.currentlyBlowing = false;
      Serial.print(F("Ending blow"));
      if (blowData.ms-blowData.blowStartMs > blowData.targetDurationMs) {
        blowData.blowCount++;
        blowData.lastBlowStatus = LAST_BLOW_SUCCEEDED;
        Serial.println(F(" successfully"));
        Serial.print(F(" Blowstart:       "));
        Serial.println(blowData.blowStartMs);
        Serial.print(F(" Blowend:         "));  
        Serial.println(blowData.blowEndMs);
        Serial.print(F(" Target Duration: "));
        Serial.println(blowData.targetDurationMs);
        // Check for task end on PEP tasks
        if (isPepTask() && taskFinishedTimeout==0 && blowData.blowCount >= blowData.totalBlowCount) {
          taskFinishedTimeout = blowData.ms + 2000;
        }
      } else {
        blowData.fails++;
        blowData.lastBlowStatus = LAST_BLOW_FAILED;
      }
    }
    // Check for task end on inhalation tasks
    if (blowData.blowCount >= blowData.totalBlowCount) {
      if (isTouchInZone(240, 210, 80, 30)) {
        taskFinishedTimeout = blowData.ms;
        tft.invertDisplay(0);
      } else if (isInhalationTask() && !blowData.currentlyBlowing && blowData.ms-getLastBlowEvent() > INHALATION_TASK_END_TIMEOUT && taskFinishedTimeout==0) {
        taskFinishedTimeout = blowData.ms;
        tft.invertDisplay(0);
      } else if (isInhalationTask() && !blowData.currentlyBlowing && blowData.ms-getLastBlowEvent() > INHALATION_TASK_WARN_TIMEOUT) {
        tft.invertDisplay((blowData.ms/500) % 2);
      } else {
        tft.invertDisplay(0);
      }
    }
    blowData.peakPressure = _max(blowData.peakPressure, blowData.pressure);
    if (taskFinishedTimeout!=0 && blowData.ms > taskFinishedTimeout) {
      Serial.println();
      Serial.println("##### DISPLAY ROTATE ######");
      Serial.print("    Rotating to: ");
      switch (blowData.taskType) {
        case PROFILE_TASK_TYPE_EQUALBLOWS:
          Serial.print("Equal blows");break;
        case PROFILE_TASK_TYPE_LONGBLOWS:
          Serial.print("Long blows");break;
        case PROFILE_TASK_TYPE_SHORTBLOWS:
          Serial.print("Short blows");break;
        case PROFILE_TASK_TYPE_TRAMPOLINE:
          Serial.print("Trampoline");break;
        case PROFILE_TASK_TYPE_INHALATION:
          Serial.print("Inhalation");break;
      }
      Serial.println();
      currentTask++;
      if (currentTask >= profileData.tasks) {
        currentCycle++;
        currentTask = 0;
      }
      blowData.blowCount = 0;
      blowData.lastBlowStatus = 0;
      blowData.taskStartMs = blowData.ms;
      taskFinishedTimeout = 0;
      if (currentCycle < blowData.totalCycleNumber) {
        displayPhysioRotateScreen();
      }
    }
    if (isPepTask()) {
      drawPEPDisplay();
    } else if (isInhalationTask()) {
      drawInhalationDisplay();
    }
  }
}
