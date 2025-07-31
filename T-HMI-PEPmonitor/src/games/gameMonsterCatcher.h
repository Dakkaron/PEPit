#ifndef __GAME_MONSTER_CATCHER_H__
#define __GAME_MONSTER_CATCHER_H__
#include <Arduino.h>

#include "constants.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/sdHandler.h"
#include "hardware/touchHandler.h"
#include "hardware/prefsHandler.h"
#include "games/monsterDetails.h"

#define DRAW_PLAYER_ALIVE     0b00000001
#define DRAW_PLAYER_DEAD      0b00000010
#define DRAW_PLAYER_DODGE     0b00000100
#define DRAW_ENEMY_ALIVE      0b00001000
#define DRAW_ENEMY_DEAD       0b00010000
#define DRAW_ENEMY_DODGE      0b00100000

void initGames_monsterCatcher(String gamePath, GameConfig* gameConfig, String* errorMessage);
void drawShortBlowGame_monsterCatcher(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawEqualBlowGame_monsterCatcher(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawLongBlowGame_monsterCatcher(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawTrampolineGame_monsterCatcher(DISPLAY_T* display, JumpData* jumpData, String* errorMessage);
void drawInhalationGame_monsterCatcher(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawInhalationBlowGame_monsterCatcher(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
bool displayProgressionMenu_monsterCatcher(DISPLAY_T *display, String *errorMessage);

#define TRAINER_ANIM_PATH "gfx/trainer/brendan.bmp"

#endif /* __GAME_MONSTER_CATCHER_H__*/
