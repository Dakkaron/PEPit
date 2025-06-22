#include "sdHandler.h"
#include "gfxHandler.hpp"
#include "pins.h"
#include "prefsHandler.h"

uint32_t numberOfWinScreens;

void initSD(String* errorMessage) {
  Serial.println(F("Initializing SD..."));
  SD_MMC.setPins(SD_SCLK_PIN, SD_MOSI_PIN, SD_MISO_PIN);
  bool rlst = SD_MMC.begin("/sdcard", true);
  if (!rlst) {
    Serial.println("SD init failed");
    Serial.println("? No detected SdCard");
    errorMessage->concat("SD Karte nicht lesbar!\n");
    return;
  } else {
    Serial.println("SD init success");
    Serial.printf("? Detected SdCard insert: %.2f GB\r\n", SD_MMC.cardSize() / 1024.0 / 1024.0 / 1024.0);
  }
  Serial.println(F("done"));
}

void checkForPrefsReset() {
  if (SD_MMC.exists("/resetSaves")) {
    Serial.println("Prefs reset requested!");
    SD_MMC.remove("/resetSaves");
    clearPreferences();
    Serial.println("Prefs reset done!");
  }
}

bool stringIsTrue(String str, bool defaultValue) {
  str.trim();
  str.toLowerCase();
  if (defaultValue) {
    return str != "false" && str != "0";
  }
  return str == "true" || str == "1";
}

void readSystemConfig(SystemConfig* systemConfig, String* errorMessage) {
  char resBuffer[1024];
  String ignoreErrors = "";
  getIniSection(SYSTEM_CONFIG_INI_PATH, "[system]", resBuffer, 1024, errorMessage);
  systemConfig->wifiSsid = getIniValueFromSection(resBuffer, "wifiSSID", &ignoreErrors);
  systemConfig->wifiPassword = getIniValueFromSection(resBuffer, "wifiPassword", &ignoreErrors);
  systemConfig->wifiSsid2 = getIniValueFromSection(resBuffer, "wifiSSID2", &ignoreErrors);
  systemConfig->wifiPassword2 = getIniValueFromSection(resBuffer, "wifiPassword2", &ignoreErrors);
  systemConfig->wifiSsid3 = getIniValueFromSection(resBuffer, "wifiSSID3", &ignoreErrors);
  systemConfig->wifiPassword3 = getIniValueFromSection(resBuffer, "wifiPassword3", &ignoreErrors);
  systemConfig->trampolineIp = getIniValueFromSection(resBuffer, "trampolineIp", &ignoreErrors);
  systemConfig->touchScreenZThreshold = 2.5*(100-atoi(getIniValueFromSection(resBuffer, "touchScreenSensitivity", &ignoreErrors).c_str()));
  systemConfig->simulateTrampoline = stringIsTrue(getIniValueFromSection(resBuffer, "simulateTrampoline", &ignoreErrors), false);
  systemConfig->simulateBlows = stringIsTrue(getIniValueFromSection(resBuffer, "simulateBlowing", &ignoreErrors), false);
  systemConfig->simulateInhalation = stringIsTrue(getIniValueFromSection(resBuffer, "simulateInhalation", &ignoreErrors), false);
  systemConfig->debugLogBlowPressure = stringIsTrue(getIniValueFromSection(resBuffer, "debugLogBlowPressure", &ignoreErrors), false);
  systemConfig->debugLogTrampoline = stringIsTrue(getIniValueFromSection(resBuffer, "debugLogTrampoline", &ignoreErrors), false);
  systemConfig->logExecutions = stringIsTrue(getIniValueFromSection(resBuffer, "logExecutions", errorMessage), false);
  systemConfig->timezoneOffset = 60*atoi(getIniValueFromSection(resBuffer, "timezoneOffset", &ignoreErrors).c_str());
  Serial.println("Simulate blowing: "+String(systemConfig->simulateBlows));
  Serial.println("Simulate trampoline: "+String(systemConfig->simulateTrampoline));
  if (systemConfig->trampolineIp.isEmpty() || systemConfig->wifiSsid.isEmpty() || systemConfig->wifiPassword.isEmpty()) {
    Serial.println("No Wifi credentials or trampoline IP found in system config!");
    Serial.println("Using trampoline in simulation mode.");
    systemConfig->simulateTrampoline = true;
  }
}

uint32_t getNumberOfProfiles(String* errorMessage) {
  uint32_t count = 0;
  String ignoreErrors = "";
  char resBuffer[1024];
  while (ignoreErrors.isEmpty()) {
    getIniSection(PROFILE_DATA_INI_PATH, "[profile_" + String(count) + "]", resBuffer, 1024, &ignoreErrors);
    if (ignoreErrors.isEmpty()) {
      count++;
    }
  }
  if (count == 0) {
    errorMessage->concat("No profiles found!\n");
  }
  return count;
}

void readProfileData(uint32_t profileId, ProfileData* profileData, String* errorMessage) {
  char resBuffer[2048];
  getIniSection(PROFILE_DATA_INI_PATH, "[profile_" + String(profileId) + "]", resBuffer, 2048, errorMessage);
  profileData->name = getIniValueFromSection(resBuffer, "name", errorMessage);
  profileData->imagePath = getIniValueFromSection(resBuffer, "imagePath", errorMessage);
  profileData->cycles = atoi(getIniValueFromSection(resBuffer, "cycles", errorMessage).c_str());
  for (uint8_t taskId=0; taskId<10; taskId++) {
    if (!isKeyInSection(resBuffer, "task_" + String(taskId) + "_type")) {
      break;
    }
    String taskType = getIniValueFromSection(resBuffer, "task_" + String(taskId) + "_type", errorMessage);
    if (taskType == "shortBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_SHORTBLOWS;
    } else if (taskType == "longBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_LONGBLOWS;
    } else if (taskType == "equalBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_EQUALBLOWS;
    } else if (taskType == "trampoline") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_TRAMPOLINE;
    } else if (taskType == "inhalation") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_INHALATION;
    } else {
      errorMessage->concat("Unknown task type "+taskType+". Needs to be either of shortBlows, longBlows, equalBlows or trampoline.\n");
    }
    String ignoreErrors;
    if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_LONGBLOWS ||
        profileData->taskType[taskId] == PROFILE_TASK_TYPE_EQUALBLOWS || 
        profileData->taskType[taskId] == PROFILE_TASK_TYPE_SHORTBLOWS) {
      profileData->taskMinStrength[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minStrength", errorMessage).c_str());
      profileData->taskNegativeStrength[taskId] = false;
      profileData->taskTargetStrength[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_targetStrength", errorMessage).c_str());
      profileData->taskRepetitions[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_repetitions", errorMessage).c_str());
    } else if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_INHALATION) {
      profileData->taskMinStrength[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minStrength", errorMessage).c_str());
      profileData->taskTargetStrength[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_targetStrength", errorMessage).c_str());
      profileData->taskRepetitions[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minRepetitions", errorMessage).c_str());
      profileData->taskNegativeStrength[taskId] = true;
    } else if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_TRAMPOLINE) {
    }
    profileData->taskTime[taskId] = atoi(getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_time", errorMessage).c_str());
    profileData->taskChangeImagePath[taskId] = getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_changeImagePath", &ignoreErrors).c_str();
    profileData->taskChangeMessage[taskId] = getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_changeText", &ignoreErrors).c_str();
    checkFailWithMessage(*errorMessage);
    profileData->tasks++; 
  }
  if (profileData->tasks == 0) {
    errorMessage->concat("No tasks found in profile ");
    errorMessage->concat(profileId);
    errorMessage->concat(".\n");
  }
}

bool gameSupportsTaskTypes(String gamePath, uint32_t requiredTaskTypes, String* errorMessage) {
  String gameTemplate = getIniValue(gamePath+"/gameconfig.ini", "[game]", "template", errorMessage);
  uint32_t gameTaskTypes = 0;
  if (gameTemplate == "monster" || gameTemplate == "race") {
    gameTaskTypes = REQUIRED_TASK_TYPE_SHORTBLOWS | REQUIRED_TASK_TYPE_LONGBLOWS | REQUIRED_TASK_TYPE_EQUALBLOWS | REQUIRED_TASK_TYPE_TRAMPOLINE;
  }
  if (SD_MMC.exists(gamePath+"/shortBlow.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_SHORTBLOWS;
  }
  if (SD_MMC.exists(gamePath+"/longBlow.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_LONGBLOWS;
  }
  if (SD_MMC.exists(gamePath+"/equalBlow.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_EQUALBLOWS;
  }
  if (SD_MMC.exists(gamePath+"/trampoline.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_TRAMPOLINE;
  }
  if (SD_MMC.exists(gamePath+"/inhalation.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_INHALATION;
  }
  if (SD_MMC.exists(gamePath+"/progressionMenu.lua")) {
    gameTaskTypes |= REQUIRED_TASK_TYPE_PROGRESSION_MENU;
  }
  Serial.print("Game task types: ");
  Serial.print(gameTaskTypes, BIN);
  Serial.print(", required task types: ");
  Serial.print(requiredTaskTypes, BIN);
  Serial.print(", result: ");
  Serial.println((requiredTaskTypes & ~gameTaskTypes) == 0);
  return (requiredTaskTypes & ~gameTaskTypes) == 0;
}

uint16_t getNumberOfGames(String* errorMessage, uint32_t requiredTaskTypes) {
  Serial.println("getNumberOfGames()");
  File root = SD_MMC.open(GAMES_ROOT_DIR);

  if (!root) {
    errorMessage->concat("Failed to open /games directory!\n");
    Serial.println(*errorMessage);
    return 0;
  }
  if (!root.isDirectory()) {
    errorMessage->concat("/games is not a directory!\n");
    Serial.println(*errorMessage);
    return 0;
  }
  File file = root.openNextFile();
  uint16_t gameCount = 0;
  while (file) {
    if (file.isDirectory()) {
      Serial.print("Found game directory ");
      Serial.print(String(GAMES_ROOT_DIR) + "/" + file.name() + "/" + "gameconfig.ini");
      File gameIni = SD_MMC.open(String(GAMES_ROOT_DIR) + "/" + file.name() + "/" + "gameconfig.ini");
      Serial.print(", ");
      if (gameIni && !gameIni.isDirectory()) {
        if (gameSupportsTaskTypes(String(String(GAMES_ROOT_DIR) + "/" + file.name()), requiredTaskTypes, errorMessage)) {
          gameCount++;
          Serial.println("CONFIRMED!");
        } else {
          Serial.println("Game " + String(file.name()) + " does not support requred task types.");
        }
      } else {
        Serial.println("NOT A GAME!");
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return gameCount;
}

String getGamePath(uint16_t gameId, uint32_t requiredTaskTypes, String* errorMessage) {
  Serial.println("getGamePath()");
  File root = SD_MMC.open(GAMES_ROOT_DIR);

  if (!root) {
    errorMessage->concat("Failed to open /games directory!\n");
    Serial.println(*errorMessage);
    return "";
  }
  if (!root.isDirectory()) {
    errorMessage->concat("/games is not a directory!\n");
    Serial.println(*errorMessage);
    return "";
  }
  File file = root.openNextFile();
  uint16_t gameCount = 0;
  while (file) {
    if (file.isDirectory()) {
      File gameIni = SD_MMC.open(String(GAMES_ROOT_DIR) + "/" + file.name() + "/" + "gameconfig.ini");
      if (gameIni && !gameIni.isDirectory() && gameSupportsTaskTypes(String(String(GAMES_ROOT_DIR) + "/" + file.name()), requiredTaskTypes, errorMessage)) {
        if (gameCount == gameId) {
          Serial.print("Game ini returned: ");
          Serial.println(String(GAMES_ROOT_DIR) + "/" + file.name() + "/");
          return String(GAMES_ROOT_DIR) + "/" + file.name() + "/";
        }
        gameCount++;
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
  return "";
}

void readGameConfig(String gamePath, GameConfig* gameConfig, String* errorMessage) {
  char resBuffer[1024];
  getIniSection(gamePath+"gameconfig.ini", "[game]", resBuffer, 1024, errorMessage);
  gameConfig->name = getIniValueFromSection(resBuffer, "name", errorMessage);
  gameConfig->templateName = getIniValueFromSection(resBuffer, "template", errorMessage);
  gameConfig->prefsNamespace = getIniValueFromSection(resBuffer, "prefsNamespace", errorMessage);
}

void getIniSection(String iniPath, String section, char* resultBuffer, uint16_t len, String* errorMessage) {
  char lineBuffer[INI_BUFFER_LEN];
  if (section.c_str()[0] != '[') {
    section = "[" + section + "]";
  }
  section = section + "\n";

  File file = SD_MMC.open(iniPath);
  if (!file) {
    errorMessage->concat("Failed to open INI file ");
    errorMessage->concat(iniPath);
    errorMessage->concat("\n");
    Serial.println(*errorMessage);
    return;
  }
  uint16_t i = 0;
  char* resultLinePointer = resultBuffer;
  bool inCorrectSection = false;
  while (file.available()) {
    lineBuffer[i] = file.read();
    lineBuffer[i+1] = 0;
    if (lineBuffer[i] == '\n' || lineBuffer[i] == '\r') {
      lineBuffer[i] = '\n';
      if (lineBuffer[0] != '#' && inCorrectSection) {
        if (lineBuffer[0] == '[') {
          return; // Correct section finished
        }
        strcpy(resultLinePointer, lineBuffer);
        resultLinePointer += i+1;
        resultLinePointer[0] = 0;
      } else if (lineBuffer[0] == '[') {
        if (strcmp(section.c_str(), lineBuffer) == 0) {
          inCorrectSection = true;
        }
      }
      i = -1;
      lineBuffer[0] = 0;
    }
    i++;
  }
  file.close();
  if (!inCorrectSection) {
    errorMessage->concat("Section ");
    errorMessage->concat(section);
    errorMessage->concat(" not found in INI file ");
    errorMessage->concat(iniPath);
    errorMessage->concat("\n");
    Serial.println(*errorMessage);
  }
}

bool isKeyInSection(char* sectionData, String key) {
  String errorMessage;
  getIniValueFromSection(sectionData, key, &errorMessage);
  return errorMessage.isEmpty();
}

String getIniValue(String iniPath, String section, String key, String* errorMessage) {
  char resBuffer[INI_BUFFER_LEN];
  getIniSection(iniPath, section, resBuffer, INI_BUFFER_LEN, errorMessage);
  if (!errorMessage->isEmpty()) {
    return String("");
  }
  return getIniValueFromSection(resBuffer, key, errorMessage);
}

String getIniValueFromSection(char* sectionData, String key, String* errorMessage) {
  int16_t lineStartMarker = 0;
  int16_t keyEndMarker = -1;
  int16_t valStartMarker = -1;
  int16_t i = 0;
  char buffer[INI_BUFFER_LEN];
  while (sectionData[i]!=0) {
    if (sectionData[i] == '\n') {
      if (keyEndMarker!=-1) {
        if (strncmp(key.c_str(), sectionData + lineStartMarker, keyEndMarker - lineStartMarker) == 0) {
          strncpy(buffer, sectionData + valStartMarker, i-valStartMarker);
          buffer[i-valStartMarker] = 0;
          return String(buffer);
        }
      }
      lineStartMarker = i+1;
      keyEndMarker = -1;
      valStartMarker = -1;
    } else if (sectionData[i] == '=') {
      uint16_t v = i-1;
      while (v>0 && (sectionData[v] == ' ' || sectionData[v] == '\t')) {
        v--;
      }
      keyEndMarker = v;
      v = i+1;
      while (sectionData[v] == ' ' || sectionData[v] == '\t') {
        v++;
      }
      valStartMarker = v;
    }
    i++;
  }
  errorMessage->concat("Key ");
  errorMessage->concat(key);
  errorMessage->concat(" not found in INI section.\n");
  return String("");
}

void scanForWinScreens(String gamePath, String* errorMessage) {
  String gameWinScreenDir = gamePath + WIN_SCREEN_PATH;
  numberOfWinScreens = 0;
  Serial.printf("Searching directory for winscreens: %s\n", gameWinScreenDir.c_str());

  File root = SD_MMC.open(gameWinScreenDir.c_str());
  if (!root) {
    errorMessage->concat("Failed to open directory ");
    errorMessage->concat(gameWinScreenDir);
    errorMessage->concat("\n");
    Serial.println(*errorMessage);
    return;
  }
  if (!root.isDirectory()) {
    errorMessage->concat(gameWinScreenDir);
    errorMessage->concat(" is not a directory!");
    errorMessage->concat("\n");
    Serial.println(*errorMessage);
    return;
  }

  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      numberOfWinScreens++;
    }
    file = root.openNextFile();
  }
  file.close();
}

String getRandomWinScreenPath(String gamePath, String* errorMessage) {
  String gameWinScreenDir = gamePath + WIN_SCREEN_PATH;
  if (numberOfWinScreens == 0) {
    scanForWinScreens(gamePath, errorMessage);
  }
  uint32_t selectedWinScreenNumber = random(0,numberOfWinScreens);
  Serial.printf("Getting winscreen number %i/%i path from directory: %s\n", selectedWinScreenNumber, numberOfWinScreens, gameWinScreenDir.c_str());

  File root = SD_MMC.open(gameWinScreenDir.c_str());
  if (!root) {
    errorMessage->concat("Failed to open directory ");
    errorMessage->concat(gameWinScreenDir);
    errorMessage->concat("\n");
    Serial.println(*errorMessage);
    return "";
  }
  if (!root.isDirectory()) {
    errorMessage->concat(gameWinScreenDir);
    errorMessage->concat(" is not a directory!\n");
    Serial.println(*errorMessage);
    return "";
  }

  uint32_t i = 0;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      if (i == selectedWinScreenNumber) {
        return gameWinScreenDir + "/" + file.name();
      }
      i++;
    }
    file = root.openNextFile();
  }
  root.close();
  return "";
}

char* readFileToNewPSBuffer(const char *path) {
  File file = SD_MMC.open(path);
  if (!file) {
    Serial.print("Failed to open file");
    Serial.print(path);
    Serial.println(" for reading");
    return nullptr;
  }

  uint32_t fileSize = file.size();
  //Serial.println("File size: "+String(fileSize));
  char* buffer = (char*)heap_caps_malloc(fileSize+1, MALLOC_CAP_SPIRAM);
  file.readBytes(buffer, fileSize);
  buffer[fileSize] = 0;
  file.close();
  return buffer;
}


String readFileToString(const char *path) {
  File file = SD_MMC.open(path);
  if (!file) {
    Serial.print("Failed to open file");
    Serial.print(path);
    Serial.println(" for reading");
    return "";
  }

  String s;
  while (file.available()) {
    char charRead = file.read();
    s += charRead;
  }
  file.close();
  return s;
}

String readFileLineToString(const char *path, uint32_t lineNr) {
  File file = SD_MMC.open(path);
  if (!file) {
    Serial.print("Failed to open file");
    Serial.print(path);
    Serial.println(" for reading");
    return "";
  }

  uint32_t currentLineNr = 0;
  String s;
  while (file.available()) {
    char charRead = file.read();
    if (charRead == '\n') {
      currentLineNr++;
    } else if (currentLineNr == lineNr) {
      s += charRead;
    } else if (currentLineNr > lineNr) {
      break;
    }
  }
  file.close();
  return s;
}

void writeStringToFile(const char *path, String val) {
  File file = SD_MMC.open(path, FILE_WRITE, true);
  Serial.println("Writing to file "+String(path) + ": " + val);
  if (!file) {
    Serial.print("Failed to open file");
    Serial.print(path);
    Serial.println(" for writing!");
  }
  file.write((uint8_t*)val.c_str(), val.length());
  file.close();
}

void writeIntToFile(const char *path, int32_t val) {
  writeStringToFile(path, String(val));
}

int32_t readIntFromFile(const char *path) {
  return readIntFromFile(path, 0);
}

int32_t readIntFromFile(const char *path, uint32_t lineNr) {
  String data = readFileLineToString(path, lineNr);
  if (data.equals("")) {
    return 0;
  }
  return data.toInt();
}

void logExecutionToSD(ProfileData* profileData, String ntpDateString, String ntpTimeString, String* errorMessage) {
  bool writeHeader = !SD_MMC.exists(EXECUTION_LOG_PATH);
  File file = SD_MMC.open(EXECUTION_LOG_PATH, FILE_APPEND);
  if (!file) {
    errorMessage->concat("Failed to open "+String(EXECUTION_LOG_PATH)+" for writing!\n");
    return;
  }
  if (writeHeader) {
    file.println("profileName;executionDate;executionTime");
  }
  file.print(profileData->name);
  file.print(";");
  file.print(ntpDateString);
  file.print(";");
  file.print(ntpTimeString);
  file.print("\n");
  file.close();
}