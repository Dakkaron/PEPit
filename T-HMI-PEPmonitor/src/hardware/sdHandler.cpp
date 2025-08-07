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
  char resBuffer[1024*3];
  String ignoreErrors = "";
  getIniSection(SYSTEM_CONFIG_INI_PATH, "[system]", resBuffer, 1024*3, errorMessage);
  getIniValueFromSection(resBuffer, "wifiSSID", &(systemConfig->wifiSsid), &ignoreErrors);
  getIniValueFromSection(resBuffer, "wifiPassword", &(systemConfig->wifiPassword),  &ignoreErrors);
  getIniValueFromSection(resBuffer, "wifiSSID2", &(systemConfig->wifiSsid2),  &ignoreErrors);
  getIniValueFromSection(resBuffer, "wifiPassword2", &(systemConfig->wifiPassword2),  &ignoreErrors);
  getIniValueFromSection(resBuffer, "wifiSSID3", &(systemConfig->wifiSsid3),  &ignoreErrors);
  getIniValueFromSection(resBuffer, "wifiPassword3", &(systemConfig->wifiPassword3),  &ignoreErrors);
  getIniValueFromSection(resBuffer, "trampolineIp", &(systemConfig->trampolineIp),  &ignoreErrors);
  Serial.println("Loaded Wifi config:");
  Serial.println("1: "+systemConfig->wifiSsid + "/" + systemConfig->wifiPassword);
  Serial.println("2: "+systemConfig->wifiSsid2 + "/" + systemConfig->wifiPassword2);
  Serial.println("3: "+systemConfig->wifiSsid3 + "/" + systemConfig->wifiPassword3);
  systemConfig->touchScreenZThreshold = 2.5*(100-getIntIniValueFromSection(resBuffer, "touchScreenSensitivity", &ignoreErrors));
  systemConfig->simulateTrampoline    = getBoolIniValueFromSection(resBuffer, "simulateTrampoline", &ignoreErrors, false);
  systemConfig->simulateBlows         = getBoolIniValueFromSection(resBuffer, "simulateBlowing", &ignoreErrors, false);
  systemConfig->simulateInhalation    = getBoolIniValueFromSection(resBuffer, "simulateInhalation", &ignoreErrors, false);
  systemConfig->debugLogBlowPressure  = getBoolIniValueFromSection(resBuffer, "debugLogBlowPressure", &ignoreErrors, false);
  systemConfig->debugLogTrampoline    = getBoolIniValueFromSection(resBuffer, "debugLogTrampoline", &ignoreErrors, false);
  systemConfig->logExecutions         = getBoolIniValueFromSection(resBuffer, "logExecutions", errorMessage, false);
  systemConfig->timezoneOffset = 60*getIntIniValueFromSection(resBuffer, "timezoneOffset", &ignoreErrors);
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
  getIniValueFromSection(resBuffer, "name",      &(profileData->name), errorMessage);
  getIniValueFromSection(resBuffer, "imagePath", &(profileData->imagePath), errorMessage);
  profileData->cycles = getIntIniValueFromSection(resBuffer, "cycles", errorMessage, 1);
  for (uint8_t taskId=0; taskId<10; taskId++) {
    if (!isKeyInSection(resBuffer, "task_" + String(taskId) + "_type")) {
      break;
    }
    String taskType;
    getIniValueFromSection(resBuffer, "task_" + String(taskId) + "_type", &taskType, errorMessage);
    if (taskType == "pepShort" || taskType == "shortBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_SHORTBLOWS;
    } else if (taskType == "pepLong" || taskType == "longBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_LONGBLOWS;
    } else if (taskType == "pepEqual" || taskType == "equalBlows") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_EQUALBLOWS;
    } else if (taskType == "trampoline") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_TRAMPOLINE;
    } else if (taskType == "inhalation") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_INHALATION;
    } else if (taskType == "inhalationPep") {
      profileData->taskType[taskId] = PROFILE_TASK_TYPE_INHALATIONPEP;
    } else {
      errorMessage->concat("Unknown task type "+taskType+". Needs to be either of pepShort, pepLong, pepEqual or trampoline.\n");
    }
    String ignoreErrors;
    if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_LONGBLOWS ||
        profileData->taskType[taskId] == PROFILE_TASK_TYPE_EQUALBLOWS || 
        profileData->taskType[taskId] == PROFILE_TASK_TYPE_SHORTBLOWS) {
      profileData->taskMinStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minStrength", errorMessage);
      profileData->taskNegativeStrength[taskId] = false;
      profileData->taskTargetStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_targetStrength", errorMessage);
      profileData->taskRepetitions[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_repetitions", errorMessage);
      profileData->taskTime[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_time", errorMessage);
    } else if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_INHALATION) {
      profileData->taskMinStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minStrength", errorMessage);
      profileData->taskTargetStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_targetStrength", errorMessage);
      profileData->taskRepetitions[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minRepetitions", errorMessage);
      profileData->taskTime[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_time", errorMessage);
      profileData->taskNegativeStrength[taskId] = true;
    } else if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_INHALATIONPEP) {
      profileData->taskRepetitions[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_minRepetitions", errorMessage);

      profileData->taskMinStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_inhalationMinStrength", errorMessage);
      profileData->taskTargetStrength[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_inhalationTargetStrength", errorMessage);
      profileData->taskTime[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_inhalationTime", errorMessage);
      profileData->taskNegativeStrength[taskId] = true;

      profileData->taskMinStrength2[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_exhalationMinStrength", errorMessage);
      profileData->taskTargetStrength2[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_exhalationTargetStrength", errorMessage);
      profileData->taskTime2[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_exhalationTime", errorMessage);
      profileData->taskNegativeStrength2[taskId] = false;
    } else if (profileData->taskType[taskId] == PROFILE_TASK_TYPE_TRAMPOLINE) {
      profileData->taskTime[taskId] = getIntIniValueFromSection(resBuffer, "task_"+String(taskId)+"_time", errorMessage);
    }
    getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_changeImagePath", &(profileData->taskChangeImagePath[taskId]), &ignoreErrors);
    getIniValueFromSection(resBuffer, "task_"+String(taskId)+"_changeText", &(profileData->taskChangeMessage[taskId]), &ignoreErrors);
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
  GameConfig config;
  String ignoreErrors;
  if (!gamePath.endsWith("/")) {
    gamePath += "/";
  }
  readGameConfig(gamePath, &config, &ignoreErrors);
  uint32_t gameTaskTypes = 0;
  if (config.templateName == "monster" || config.templateName == "race") {
    gameTaskTypes = REQUIRED_TASK_TYPE_SHORTBLOWS | REQUIRED_TASK_TYPE_LONGBLOWS | REQUIRED_TASK_TYPE_EQUALBLOWS | REQUIRED_TASK_TYPE_TRAMPOLINE;
  } else {
    Serial.println(gamePath+config.trampolineScriptPath);
    if (!config.pepShortScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.pepShortScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_SHORTBLOWS;
    }
    if (!config.pepLongScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.pepLongScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_LONGBLOWS;
    }
    if (!config.pepEqualScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.pepEqualScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_EQUALBLOWS;
    }
    if (!config.trampolineScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.trampolineScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_TRAMPOLINE;
    }
    if (!config.inhalationScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.inhalationScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_INHALATION;
    }
    if (!config.inhalationPepScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.inhalationPepScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_INHALATIONPEP;
    }
    if (!config.progressionMenuScriptPath.isEmpty() && SD_MMC.exists(gamePath+config.progressionMenuScriptPath)) {
      gameTaskTypes |= REQUIRED_TASK_TYPE_PROGRESSION_MENU;
    }
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
  if (!gamePath.endsWith("/")) {
    gamePath += "/";
  }
  getIniSection(gamePath+"gameconfig.ini", "[game]", resBuffer, 1024, errorMessage);
  getIniValueFromSection(resBuffer, "name",           &(gameConfig->name),           errorMessage);
  getIniValueFromSection(resBuffer, "template",       &(gameConfig->templateName),   errorMessage);
  getIniValueFromSection(resBuffer, "prefsNamespace", &(gameConfig->prefsNamespace), errorMessage);
  String ignoreErrors;
  getIniValueFromSection(resBuffer, "pepShortScriptPath",        &(gameConfig->pepShortScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "pepLongScriptPath",         &(gameConfig->pepLongScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "pepEqualScriptPath",        &(gameConfig->pepEqualScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "inhalationPepScriptPath",   &(gameConfig->inhalationPepScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "inhalationScriptPath",      &(gameConfig->inhalationScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "trampolineScriptPath",      &(gameConfig->trampolineScriptPath), &ignoreErrors);
  getIniValueFromSection(resBuffer, "progressionMenuScriptPath", &(gameConfig->progressionMenuScriptPath), &ignoreErrors);
}

void getIniSection(String iniPath, String section, char* resultBuffer, uint16_t len, String* errorMessage) {
  char lineBuffer[INI_LINE_BUFFER_LEN];
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
  String output;
  String errorMessage;
  getIniValueFromSection(sectionData, key, &output, &errorMessage);
  return errorMessage.isEmpty();
}

void getIniValue(String iniPath, String section, String key, String* output, String* errorMessage) {
  char resBuffer[INI_BUFFER_LEN];
  getIniSection(iniPath, section, resBuffer, INI_BUFFER_LEN, errorMessage);
  if (!errorMessage->isEmpty()) {
    output->clear();
  } else {
    getIniValueFromSection(resBuffer, key, output, errorMessage);
  }
}

int32_t getIntIniValueFromSection(char* sectionData, String key, String* errorMessage, int32_t def) {
  String output;
  getIniValueFromSection(sectionData, key, &output, errorMessage);
  if (output.isEmpty()) {
    return def;
  } else {
    return atoi(output.c_str());
  }
}

bool getBoolIniValueFromSection(char* sectionData, String key, String* errorMessage, bool def) {
  String output;
  getIniValueFromSection(sectionData, key, &output, errorMessage);
  return stringIsTrue(output, def);
}

void getIniValueFromSection(char* sectionData, String key, String* output, String* errorMessage) {
  int16_t lineStartMarker = 0;
  int16_t keyEndMarker = -1;
  int16_t valStartMarker = -1;
  int16_t i = 0;
  char buffer[INI_BUFFER_LEN];
  while (sectionData[i]!=0) {
    if (sectionData[i] == '\n') {
      if (keyEndMarker!=-1) {
        if (strncmp(key.c_str(), sectionData + lineStartMarker, key.length()) == 0) {
          strncpy(buffer, sectionData + valStartMarker, i-valStartMarker);
          buffer[i-valStartMarker] = 0;
          output->clear();
          output->concat(buffer);
          return;
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
  output->clear();
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
  Serial.printf("Getting winscreen number %lu/%lu path from directory: %s\n", selectedWinScreenNumber, numberOfWinScreens, gameWinScreenDir.c_str());

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