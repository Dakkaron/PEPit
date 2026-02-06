#ifndef __SDHANDLER_H__
#define __SDHANDLER_H__
#include <Arduino.h>
#include <SD_MMC.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include "systemconfig.h"
#include "constants.h"

#define SYSTEM_CONFIG_INI_PATH "/systemConfig.ini"
#define PROFILE_DATA_INI_PATH "/profiles.ini"
#define EXECUTION_LOG_PATH "/executionsLog.csv"
#define INI_BUFFER_LEN 2048
#define INI_LINE_BUFFER_LEN 512

#define WIN_SCREEN_PATH "gfx/win"
#define GAMES_ROOT_DIR "/games"

void initSD(String* errorMessage);
void checkForPrefsReset();

void readSystemConfig(SystemConfig* systemConfig, String* errorMessage);

uint16_t getNumberOfGames(String* errorMessage, uint32_t requiredTaskTypes);
String getGamePath(uint16_t gameId, uint32_t requiredTaskTypes, String* errorMessage);
uint32_t getNumberOfProfiles(String* errorMessage);
void readProfileData(uint32_t profileId, ProfileData* profileData, String* errorMessage);

void readGameConfig(String gamePath, GameConfig* gameConfig, String* errorMessage);

void getIniSection(String iniPath, String section, char* resultBuffer, uint16_t len, String* errorMessage);
bool isKeyInSection(char* sectionData, String key);
void getIniValueFromSection(char* sectionData, String key, String* output, String* errorMessage);
bool getBoolIniValueFromSection(char* sectionData, String key, String* errorMessage, bool def);
int32_t getIntIniValueFromSection(char* sectionData, String key, String* errorMessage, int32_t def=0);
void getIniValue(String iniPath, String section, String key, String* output, String* errorMessage);

String getRandomWinScreenPath(String gamePath, String* errorMessage);

char* readFileToNewPSBuffer(const char *path);
String readFileToString(const char *path);
String readFileLineToString(const char *path, uint32_t lineNr);
void writeStringToFile(const char *path, String val);
void writeIntToFile(const char *path, int32_t val);
int32_t readIntFromFile(const char *path);
int32_t readIntFromFile(const char *path, uint32_t lineNr);

void logExecutionToSD(ProfileData* profileData, String ntpDateString, String ntpTimeString, uint32_t successes, uint32_t fails, uint32_t time, String* errorMessage);

#endif /* __SDHANDLER_H__*/