#ifndef __GAMES_H__
#define __GAMES_H__

#include <Arduino.h>
#include "hardware/gfxHandler.hpp"
#include "hardware/sdHandler.h"

void initGames(String gamePath, String* errorMessage);
void drawShortBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawEqualBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawLongBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawTrampolineGame(DISPLAY_T* display, JumpData* jumpData, String* errorMessage);
void drawInhalationGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawInhalationBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
bool displayProgressionMenu(DISPLAY_T* display, String* errorMessage);
bool displayExecutionList(DISPLAY_T *display, String *executionLog, String *errorMessage);
void endGame(String* errorMessage);
String getRandomWinScreenPathForCurrentGame(String* errorMessage);

#endif /* __GAMES_H__ */