#ifndef __GAME_LUA_H__
#define __GAME_LUA_H__

#define LUA_USE_C89
#include "lua/lua.hpp"

#include "constants.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/sdHandler.h"
#include "hardware/touchHandler.h"
#include "hardware/prefsHandler.h"

void initLua();
void initGames_lua(String gamePath, GameConfig* gameConfig, String* errorMessage);
void drawShortBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawEqualBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawLongBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawTrampolineGame_lua(DISPLAY_T* display, JumpData* jumpData, String* errorMessage);
void drawInhalationGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
void drawInhalationBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage);
bool displayProgressionMenu_lua(DISPLAY_T *display, String *errorMessage);
void endGame_lua(String* errorMessage);
String lua_dofile(const String path);
String lua_dostring(const char* script, String marker);
String lua_dostring(const char* script, String marker, bool strictMode);

extern bool luaProgressionMenuRunning;
extern DISPLAY_T* luaDisplay;
extern lua_State* luaState;

#endif /* __GAME_LUA_H__ */