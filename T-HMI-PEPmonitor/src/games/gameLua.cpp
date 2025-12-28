#include "gameLua.h"
#include "hardware/sdHandler.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/higherLevelGfxHandler.hpp"
#include "hardware/prefsHandler.h"

static String luaGamePath;
lua_State* luaState;

DISPLAY_T* luaDisplay;
bool luaProgressionMenuRunning;
bool luaWinScreenRunning;
bool luaStrictMode = false;
bool luaCacheGameCode = false;
static GameConfig gameConfig;

RoadColors roadColors;
RoadDrawFlags roadDrawFlags;
RoadDimensions roadDimensions;

#define SPRITE_COUNT_LIMIT 100

struct SpriteMetadata {
  uint32_t frameW;
  uint32_t frameH;
  int32_t maskingColor;
};

static SpriteMetadata spriteMetadata[SPRITE_COUNT_LIMIT];

// Block of SPRITE_COUNT_LIMIT sprites to be used within Lua games
static TFT_eSprite* sprites = nullptr;


static String findScriptRedirectPath(char* input) {
  String scriptString = String(input);
  scriptString.trim();
  if (scriptString.indexOf("\n")!=-1 || scriptString.indexOf(";")!=-1 || scriptString.indexOf("\r")!=-1) {
    return String(""); // Not a one-line redirect
  }
  if (!scriptString.startsWith("RunScript(\"") || !scriptString.endsWith("\")")) {
    return String("");
  }
  String redirectPath = scriptString.substring(11, scriptString.length()-2);
  if (redirectPath.indexOf("\"")!=-1) {
    return String("");
  }
  
  return luaGamePath + redirectPath;
}

String lua_dofile(const String path, bool dontCache) {
  static char* script[10];
  static String cachedPath[10];
  String result;
  int cacheSlot = -1;
  if (luaCacheGameCode && !dontCache) {
    //Serial.println("Caching enalbed");
    for (int i=0;i<10;i++) {
      if (cachedPath[i] == path) {
        cacheSlot = i;
        //Serial.println("Found cached script "+path+" in slot "+String(cacheSlot));
        break;
      }
    }
    if (cacheSlot == -1) {
      for (int i=0;i<10;i++) {
        if (cachedPath[i].isEmpty()) {
          cacheSlot = i;
          //Serial.println("Caching script "+path+" in slot "+String(cacheSlot));
          
          Serial.print("Collect garbage before caching script, free RAM before: ");
          Serial.println(ESP.getFreeHeap());
          lua_gc(luaState, LUA_GCCOLLECT, 0);
          Serial.print("Free RAM after: ");
          Serial.println(ESP.getFreeHeap());
          
          if (script[cacheSlot] != nullptr) {
            free(script[cacheSlot]);
          }
          script[cacheSlot] = readFileToNewPSBuffer(path.c_str());
          cachedPath[cacheSlot] = path;
          if (script[cacheSlot] == nullptr) {
            result = "Failed to load lua script "+path;
            return result;
          }
          break;
        }
      }
    }
    if (cacheSlot == -1) {
      //Serial.println("Cache full, removing oldest script "+cachedPath[0]);
      for (int i=0;i<9;i++) {
        if (script[i] != nullptr) {
          free(script[i]);
        }
        script[i] = script[i+1];
        cachedPath[i] = cachedPath[i+1];
      }
      cacheSlot = 9;
      script[cacheSlot] = readFileToNewPSBuffer(path.c_str());
      cachedPath[cacheSlot] = path;
      if (script[cacheSlot] == nullptr) {
        result = "Failed to load lua script "+path;
        return result;
      }
    }
  } else {
    //Serial.println("Caching disabled");
    script[0] = readFileToNewPSBuffer(path.c_str());
    cacheSlot = 0;
    if (script[cacheSlot] == nullptr) {
      result = "Failed to load lua script "+path;
      return result;
    }
  }
  String redirectPath = findScriptRedirectPath(script[cacheSlot]);
  if (!redirectPath.isEmpty()) {
    result = lua_dofile(redirectPath);
  } else if (luaL_dostring(luaState, script[cacheSlot])) {
    result = "# lua error:\n" + String(lua_tostring(luaState, -1));
    lua_pop(luaState, 1);
    Serial.println("Error in lua file "+path+": " + result);
    if (luaStrictMode) {
      checkFailWithMessage("Error in lua file "+path+": " + result);
    }
  }
  if (!luaCacheGameCode || dontCache) {
    if (script[cacheSlot] != nullptr) {
      free(script[cacheSlot]);
      script[cacheSlot] = nullptr;
    }
  }
  return result;
}

String lua_dofile(const String path) {
  return lua_dofile(path, false);
}


String lua_dostring(const char* script, String marker, bool strictMode) {
  String result;
  if (luaL_dostring(luaState, script)) {
    result += "# lua error:\n" + String(lua_tostring(luaState, -1));
    lua_pop(luaState, 1);
    Serial.println("Error in lua string: " + result + "\n(" + marker + ")");
    if (luaStrictMode) {
      checkFailWithMessage("Error in lua string: " + result + "\n(" + marker + ")");
    }
  }
  return result;
}

String lua_dostring(const char* script, String marker) {
  return lua_dostring(script, marker, luaStrictMode);
}


static int lua_wrapper_disableCaching(lua_State* luaState) {
  luaCacheGameCode = false;
  return 0;
}

static int lua_wrapper_serialPrint(lua_State* luaState) {
  Serial.print(luaL_checkstring(luaState, 1));
  return 0;
}


static int lua_wrapper_serialPrintln(lua_State* luaState) {
  Serial.println(luaL_checkstring(luaState, 1));
  return 0;
}

static int lua_wrapper_runScript(lua_State* luaState) {
  String path = luaGamePath + luaL_checkstring(luaState, 1);
  lua_dofile(path);
  /*String script = readFileToString((path).c_str());
  if (script.isEmpty()) {
    Serial.println("Error: Failed to load script "+path);
    return 0;
  }
  String error = lua_dostring(script.c_str(), "runScript()", false);
  if (!error.isEmpty()) {
    Serial.println("Error in script " + path + ": " + error);
  }*/
  return 0;
}

static int lua_wrapper_loadSprite(lua_State* luaState) {
  Serial.print("Load BMP sprite ");
  String path = luaGamePath + luaL_checkstring(luaState, 1);
  Serial.println(path);
  int32_t options = luaL_optinteger(luaState, 2, 0);
  int32_t maskingColor = luaL_optinteger(luaState, 3, -1);

  Serial.print("Collect garbage before loading sprite, free RAM before: ");
  Serial.println(ESP.getFreeHeap());
  lua_gc(luaState, LUA_GCCOLLECT, 0);
  Serial.print("Free RAM after: ");
  Serial.println(ESP.getFreeHeap());

  for (int32_t i=0;i<SPRITE_COUNT_LIMIT;i++) {
    if (!sprites[i].created()) {
      if (!loadBmp(&sprites[i], path, options, maskingColor)) {
        Serial.println("Failed to load sprite "+path);
        if (luaStrictMode) {
          checkFailWithMessage("Failed to load sprite "+path);
        }
        lua_pushinteger(luaState, -1);
        return 1;
      }
      spriteMetadata[i].frameW = sprites[i].width();
      spriteMetadata[i].frameH = sprites[i].height();
      spriteMetadata[i].maskingColor = maskingColor;
      Serial.print("Found sprite slot: ");
      Serial.println(i);
      lua_pushinteger(luaState, i);
      return 1;
    }
  }
  Serial.println("Failed to find sprite slot for sprite "+path);
  if (luaStrictMode) {
    checkFailWithMessage("Failed to find sprite slot for sprite "+path);
  }
  lua_pushinteger(luaState, -1);
  return 1;
}

static int lua_wrapper_freeSprite(lua_State* luaState) {
  Serial.print("Free BMP sprite ");
  int16_t handle = luaL_checkinteger(luaState, 1);
  Serial.println(handle);
  if (sprites[handle].created()) {
    sprites[handle].deleteSprite();
  }
  return 0;
}

static int lua_wrapper_loadAnimSprite(lua_State* luaState) {
  Serial.print("Load animated BMP sprite ");
  String path = luaGamePath + luaL_checkstring(luaState, 1);
  Serial.println(path);
  int32_t options = luaL_optinteger(luaState, 4, 0);
  int32_t maskingColor = luaL_optinteger(luaState, 5, -1);

  //Serial.print("Collect garbage before loading sprite, free RAM before: ");
  Serial.println(ESP.getFreeHeap());
  lua_gc(luaState, LUA_GCCOLLECT, 0);
  //Serial.print("Free RAM after: ");
  Serial.println(ESP.getFreeHeap());

  for (int32_t i=0;i<SPRITE_COUNT_LIMIT;i++) {
    if (!sprites[i].created()) {
      if (maskingColor != -1) {
        loadBmp(&sprites[i], path, options, maskingColor);
      } else {
        loadBmp(&sprites[i], path, options);
      }
      Serial.print("Found sprite slot: ");
      Serial.println(i);
      spriteMetadata[i].frameW = luaL_checknumber(luaState, 2);
      spriteMetadata[i].frameH = luaL_checknumber(luaState, 3);
      spriteMetadata[i].maskingColor = maskingColor;
      lua_pushinteger(luaState, i);
      return 1;
    }
  }
  Serial.println("Failed to find sprite slot");
  lua_pushinteger(luaState, -1);
  return 1;
}

static bool isHandleValid(int32_t handle) {
  if ((handle<0) || (handle>=SPRITE_COUNT_LIMIT) || (!sprites[handle].created())) {
    Serial.println("ERROR: invalid sprite handle!");
    if (luaStrictMode) {
      checkFailWithMessage("ERROR: invalid sprite handle!");
    }
    return false;
  }
  return true;
}

static int lua_wrapper_drawSprite(lua_State* luaState) {
  //Serial.print("Draw sprite ");
  int16_t handle = luaL_checkinteger(luaState, 1);
  //Serial.println(handle);
  int16_t x = luaL_checknumber(luaState, 2);
  int16_t y = luaL_checknumber(luaState, 3);
  if (!isHandleValid(handle)) {
    return 0;
  }
  int32_t maskingColor = spriteMetadata[handle].maskingColor;
  if (maskingColor != -1) {
    sprites[handle].pushToSprite(luaDisplay, x, y, spriteMetadata[handle].maskingColor);
  } else {
    sprites[handle].pushToSprite(luaDisplay, x, y);
  }
  
  //Serial.println("Draw sprite done");
  return 0;
}

static int lua_wrapper_drawSpriteRegion(lua_State* luaState) {
  //Serial.println("Draw sprite region");
  int16_t handle = luaL_checkinteger(luaState, 1);
  int16_t tx = luaL_checknumber(luaState, 2);
  int16_t ty = luaL_checknumber(luaState, 3);
  int16_t sx = luaL_checknumber(luaState, 4);
  int16_t sy = luaL_checknumber(luaState, 5);
  int16_t sw = luaL_checknumber(luaState, 6);
  int16_t sh = luaL_checknumber(luaState, 7);
  if (!isHandleValid(handle)) {
    return 0;
  }
  int32_t maskingColor = spriteMetadata[handle].maskingColor;
  if (maskingColor != -1) {
    sprites[handle].pushToSprite(luaDisplay, tx, ty, sx, sy, sw, sh, maskingColor);
  } else {
    sprites[handle].pushToSprite(luaDisplay, tx, ty, sx, sy, sw, sh);
  }
  //Serial.println("Draw sprite region done");
  return 0;
}

static int lua_wrapper_drawAnimSprite(lua_State* luaState) {
  //Serial.print("Draw anim sprite ");
  int16_t handle = luaL_checkinteger(luaState, 1);
  //Serial.println(handle);
  int16_t tx = luaL_checknumber(luaState, 2);
  int16_t ty = luaL_checknumber(luaState, 3);
  int16_t frame = luaL_checknumber(luaState, 4);

  if (!isHandleValid(handle)) {
    return 0;
  }
  
  int16_t sw = spriteMetadata[handle].frameW;
  int16_t sh = spriteMetadata[handle].frameH;

  int16_t cols = sprites[handle].width() / sw;

  int16_t col = frame % cols;
  int16_t row = frame / cols;

  int32_t maskingColor = spriteMetadata[handle].maskingColor;
  if (maskingColor != -1) {
    sprites[handle].pushToSprite(luaDisplay, tx, ty, sw*col, sh*row, sw, sh, maskingColor);
  } else {
    sprites[handle].pushToSprite(luaDisplay, tx, ty, sw*col, sh*row, sw, sh);
  }
  
  //Serial.println("Draw anim sprite done");
  return 0;
}

static int lua_wrapper_drawSpriteToSprite(lua_State* luaState) {
  //Serial.print("Draw sprite ");
  int16_t srcHandle = luaL_checkinteger(luaState, 1);
  int16_t dstHandle = luaL_checkinteger(luaState, 2);
  //Serial.println(handle);
  int16_t x = luaL_checknumber(luaState, 3);
  int16_t y = luaL_checknumber(luaState, 4);

  if ((srcHandle<0) || (srcHandle>=SPRITE_COUNT_LIMIT) || (!sprites[srcHandle].created())) {
    Serial.println("ERROR: Could not draw sprite to sprite: invalid src sprite handle!");
    if (luaStrictMode) {
      checkFailWithMessage("ERROR: Could not draw sprite to sprite: invalid src sprite handle!");
    }
    return 0;
  }
  if ((dstHandle<0) || (dstHandle>=SPRITE_COUNT_LIMIT) || (!sprites[dstHandle].created())) {
    Serial.println("ERROR: Could not draw sprite to sprite: invalid dst sprite handle!");
    if (luaStrictMode) {
      checkFailWithMessage("ERROR: Could not draw sprite to sprite: invalid dst sprite handle!");
    }
    return 0;
  }

  int32_t maskingColor = spriteMetadata[srcHandle].maskingColor;
  Serial.print("PSTS: Masking color: ");
  Serial.println(maskingColor);
  bool oldSwapBytes = sprites[srcHandle].getSwapBytes();
  sprites[srcHandle].setSwapBytes(false);
  if (maskingColor != -1) {
    sprites[srcHandle].pushToSprite(&sprites[dstHandle], x, y, maskingColor);
  } else {
    sprites[srcHandle].pushToSprite(&sprites[dstHandle], x, y);
  }
  sprites[srcHandle].setSwapBytes(oldSwapBytes);
  //Serial.println("Draw sprite done");
  return 0;
}

static int lua_wrapper_drawSpriteScaled(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  Vector2D position;
  position.x = luaL_checknumber(luaState, 2);
  position.y = luaL_checknumber(luaState, 3);
  Vector2D scale;
  scale.x = luaL_checknumber(luaState, 4);
  scale.y = luaL_checknumber(luaState, 5);
  uint32_t flags = luaL_optinteger(luaState, 6, 0);
  flags |= TRANSP_MASK;
  drawSpriteScaled(luaDisplay, &sprites[handle], &position, &scale, flags, spriteMetadata[handle].maskingColor);
  return 0;
}

static int lua_wrapper_drawAnimSpriteScaled(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  int16_t sw = spriteMetadata[handle].frameW;
  int16_t sh = spriteMetadata[handle].frameH;
  Vector2D position;
  position.x = luaL_checknumber(luaState, 2);
  position.y = luaL_checknumber(luaState, 3);
  Vector2D scale;
  scale.x = luaL_checknumber(luaState, 4);
  scale.y = luaL_checknumber(luaState, 5);
  int16_t frame = luaL_checknumber(luaState, 6);
  uint32_t flags = luaL_optinteger(luaState, 7, 0);
  flags |= TRANSP_MASK;
  drawSpriteScaled(luaDisplay, &sprites[handle], &position, &scale, flags, spriteMetadata[handle].maskingColor, sw, sh, frame);
  return 0;
}

static int lua_wrapper_drawSpriteTransformed(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  Vector2D position;
  position.x = luaL_checknumber(luaState, 2);
  position.y = luaL_checknumber(luaState, 3);
  Matrix2D transform;
  transform.a = luaL_checknumber(luaState, 4);
  transform.b = luaL_checknumber(luaState, 5);
  transform.c = luaL_checknumber(luaState, 6);
  transform.d = luaL_checknumber(luaState, 7);
  uint32_t flags = luaL_optinteger(luaState, 8, 0);
  flags |= TRANSP_MASK;
  drawSpriteTransformed(luaDisplay, &sprites[handle], &position, &transform, flags, spriteMetadata[handle].maskingColor);
  return 0;
}

static int lua_wrapper_drawSpriteScaledRotated(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  Vector2D position;
  position.x = luaL_checknumber(luaState, 2);
  position.y = luaL_checknumber(luaState, 3);
  Vector2D scale;
  scale.x = luaL_checknumber(luaState, 4);
  scale.y = luaL_checknumber(luaState, 5);
  float angle = luaL_checknumber(luaState, 6);
  uint32_t flags = luaL_optinteger(luaState, 7, 0);
  flags |= TRANSP_MASK;
  float s = sin(angle);
  float c = cos(angle);
  Matrix2D transform = {
    c * scale.x, -s,
    s, c * scale.y
  };
  drawSpriteTransformed(luaDisplay, &sprites[handle], &position, &transform, flags, spriteMetadata[handle].maskingColor);
  return 0;
}

static int lua_wrapper_drawAnimSpriteScaledRotated(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  int16_t sw = spriteMetadata[handle].frameW;
  int16_t sh = spriteMetadata[handle].frameH;
  Vector2D position;
  position.x = luaL_checknumber(luaState, 2);
  position.y = luaL_checknumber(luaState, 3);
  Vector2D scale;
  scale.x = luaL_checknumber(luaState, 4);
  scale.y = luaL_checknumber(luaState, 5);
  float angle = luaL_checknumber(luaState, 6);
  int16_t frame = luaL_checknumber(luaState, 7);
  uint32_t flags = luaL_optinteger(luaState, 8, ALIGN_H_CENTER | ALIGN_V_CENTER);
  flags |= TRANSP_MASK;
  float s = sin(angle);
  float c = cos(angle);
  Matrix2D scaleMatrix = {
    scale.x, 0,
    0, scale.y
  };
  Matrix2D rotateMatrix = {
    c, -s,
    s, c
  };
  Matrix2D transformMatrix;
  multMMF(&rotateMatrix, &scaleMatrix, &transformMatrix);
  drawSpriteTransformed(luaDisplay, &sprites[handle], &position, &transformMatrix, flags, spriteMetadata[handle].maskingColor, sw, sh, frame);
  return 0;
}

static int lua_wrapper_spriteHeight(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  lua_pushinteger(luaState, spriteMetadata[handle].frameH);
  return 1;
}

static int lua_wrapper_spriteWidth(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    return 0;
  }
  lua_pushinteger(luaState, spriteMetadata[handle].frameW);
  return 1;
}

static int lua_wrapper_drawMode7(lua_State* luaState) {
  int16_t textureHandle = luaL_checkinteger(luaState, 1);
  if ((textureHandle<0) || (textureHandle>=SPRITE_COUNT_LIMIT) || (!sprites[textureHandle].created())) {
    Serial.println("ERROR: Could not draw mode7: invalid sprite handle!");
    if (luaStrictMode) {
      checkFailWithMessage("ERROR: Could not draw mode7: invalid sprite handle!");
    }
    return 0;
  }
  Vector2D cameraPos;
  cameraPos.x = luaL_checknumber(luaState, 2);
  cameraPos.y = luaL_checknumber(luaState, 3);
  float cameraHeight = luaL_checknumber(luaState, 4);
  float yawAngle = luaL_checknumber(luaState, 5);
  float zoom = luaL_checknumber(luaState, 6);
  float horizonHeight = luaL_checknumber(luaState, 7);
  float startY = luaL_checknumber(luaState, 8);
  float endY = luaL_checknumber(luaState, 9);
  drawMode7(luaDisplay, &sprites[textureHandle], &cameraPos, cameraHeight, yawAngle, zoom, horizonHeight, startY, endY);
  return 0;
}

static int lua_wrapper_mode7WorldToScreen(lua_State* luaState) {
  Vector2D worldPos;
  worldPos.x = luaL_checknumber(luaState, 1);
  worldPos.y = luaL_checknumber(luaState, 2);
  Vector2D cameraPos;
  cameraPos.x = luaL_checknumber(luaState, 3);
  cameraPos.y = luaL_checknumber(luaState, 4);
  float cameraHeight = luaL_checknumber(luaState, 5);
  float yawAngle = luaL_checknumber(luaState, 6);
  float zoom = luaL_checknumber(luaState, 7);
  float horizonHeight = luaL_checknumber(luaState, 8);
  float startY = luaL_checknumber(luaState, 9);
  float endY = luaL_checknumber(luaState, 10);
  Vector3D output;
  mode7WorldToScreen(&worldPos, &cameraPos, cameraHeight, yawAngle, zoom, horizonHeight, startY, endY, &output);
  lua_pushinteger(luaState, (int32_t)output.x);
  lua_pushinteger(luaState, (int32_t)output.y);
  lua_pushnumber(luaState, output.z);
  return 3;
}

static int lua_wrapper_log(lua_State* luaState) {
  String s = luaL_checkstring(luaState, 1);
  Serial.println(s);
  return 0;
}

static int lua_wrapper_drawString(lua_State* luaState) {
  String s = luaL_checkstring(luaState, 1);
  int16_t x = luaL_checknumber(luaState, 2);
  int16_t y = luaL_checknumber(luaState, 3);
  luaDisplay->drawString(s, x, y);
  return 0;
}

static int lua_wrapper_drawRect(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  int16_t h = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  luaDisplay->drawRect(x, y, w, h, color);
  return 0;
}

static int lua_wrapper_fillRect(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  int16_t h = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  luaDisplay->fillRect(x, y, w, h, color);
  return 0;
}

static int lua_wrapper_drawCircle(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t r = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  luaDisplay->drawCircle(x, y, r, color);
  return 0;
}

static int lua_wrapper_fillCircle(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t r = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  luaDisplay->fillCircle(x, y, r, color);
  return 0;
}

static int lua_wrapper_drawLine(lua_State* luaState) {
  int16_t x0 = luaL_checknumber(luaState, 1);
  int16_t y0 = luaL_checknumber(luaState, 2);
  int16_t x1 = luaL_checknumber(luaState, 3);
  int16_t y1 = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  luaDisplay->drawLine(x0, y0, x1, y1, color);
  return 0;
}

static int lua_wrapper_drawFastHLine(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  luaDisplay->drawFastHLine(x, y, w, color);
  return 0;
}

static int lua_wrapper_drawFastVLine(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t h = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  luaDisplay->drawFastVLine(x, y, h, color);
  return 0;
}

static int lua_wrapper_fillScreen(lua_State* luaState) {
  uint16_t color = luaL_checknumber(luaState, 1);
  luaDisplay->fillScreen(color);
  return 0;
}

static int lua_wrapper_setTextColor(lua_State* luaState) {
  uint16_t color = luaL_checknumber(luaState, 1);
  luaDisplay->setTextColor(color);
  return 0;
}

static int lua_wrapper_setTextSize(lua_State* luaState) {
  uint8_t size = luaL_checknumber(luaState, 1);
  luaDisplay->setTextSize(size);
  return 0;
}

static int lua_wrapper_setTextDatum(lua_State* luaState) {
  uint8_t datum = luaL_checknumber(luaState, 1);
  luaDisplay->setTextDatum(datum);
  return 0;
}

static int lua_wrapper_setCursor(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  luaDisplay->setCursor(x, y);
  return 0;
}

static int lua_wrapper_print(lua_State* luaState) {
  String s = luaL_checkstring(luaState, 1);
  luaDisplay->print(s);
  return 0;
}

static int lua_wrapper_println(lua_State* luaState) {
  String s = luaL_checkstring(luaState, 1);
  luaDisplay->println(s);
  return 0;
}

static int lua_wrapper_cls(lua_State* luaState) {
  luaDisplay->fillSprite(TFT_BLACK);
  return 0;
}

static int lua_wrapper_prefsSetString(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  String value = luaL_checkstring(luaState, 2);
  prefs.putString(key.c_str(), value.c_str());
  prefs.end();
  applyGamePrefsNamespace();
  return 0;
}

static int lua_wrapper_prefsGetString(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  String def = luaL_optstring(luaState, 2, "");
  String value = prefs.getString(key.c_str(), def.c_str());
  lua_pushstring(luaState, value.c_str());
  return 1;
}

static int lua_wrapper_prefsSetInt(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  int value = luaL_checknumber(luaState, 2);
  prefs.putInt(key.c_str(), value);
  prefs.end();
  applyGamePrefsNamespace();
  return 0;
}

static int lua_wrapper_prefsGetInt(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  int def = luaL_optinteger(luaState, 2, 0);
  int value = prefs.getInt(key.c_str(), def);
  lua_pushinteger(luaState, value);
  return 1;
}

static int lua_wrapper_prefsSetNumber(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  float value = luaL_checknumber(luaState, 2);
  prefs.putFloat(key.c_str(), value);
  prefs.end();
  applyGamePrefsNamespace();
  return 0;
}

static int lua_wrapper_prefsGetNumber(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  float def = luaL_optnumber(luaState, 2, 0.0);
  float value = prefs.getFloat(key.c_str(), def);
  lua_pushnumber(luaState, value);
  return 1;
}

static int lua_wrapper_constrain(lua_State* luaState) {
  float val = luaL_checknumber(luaState, 1);
  float min = luaL_checknumber(luaState, 2);
  float max = luaL_checknumber(luaState, 3);
  lua_pushnumber(luaState, constrain(val, min, max));
  return 1;
}

static int lua_wrapper_closeProgressionMenu(lua_State* luaState) {
  luaProgressionMenuRunning = false;
  return 0;
}

static int lua_wrapper_closeWinScreen(lua_State* luaState) {
  luaWinScreenRunning = false;
  return 0;
}

static int lua_wrapper_isTouchInZone(lua_State* luaState) {
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  int16_t h = luaL_checknumber(luaState, 4);
  bool result = isTouchInZone(x, y, w, h);
  lua_pushboolean(luaState, result);
  return 1;
}

static int lua_wrapper_getTouchX(lua_State* luaState) {
  lua_pushinteger(luaState, touch.X());
  return 1;
}

static int lua_wrapper_getTouchY(lua_State* luaState) {
  lua_pushinteger(luaState, touch.Y());
  return 1;
}

static int lua_wrapper_getTouchPressure(lua_State* luaState) {
  lua_pushinteger(luaState, touch.RawZ());
  return 1;
}

static int lua_wrapper_getFreeRAM(lua_State* luaState) {
  lua_pushinteger(luaState, ESP.getFreeHeap());
  return 1;
}

static int lua_wrapper_getFreePSRAM(lua_State* luaState) {
  lua_pushinteger(luaState, ESP.getFreePsram());
  return 1;
}

static int lua_wrapper_getFreeSpriteSlots(lua_State* luaState) {
  int32_t count = 0;
  for (int32_t i=0;i<SPRITE_COUNT_LIMIT;i++) {
    if (sprites[i].created()) {
      count++;
    }
  }
  lua_pushinteger(luaState, count);
  return 1;
}

static int lua_wrapper_setRoadColors(lua_State* luaState) {
  roadColors.pavementA = luaL_checknumber(luaState, 1);
  roadColors.pavementB = luaL_checknumber(luaState, 2);
  roadColors.embankmentA = luaL_checknumber(luaState, 3);
  roadColors.embankmentB = luaL_checknumber(luaState, 4);
  roadColors.grassA = luaL_checknumber(luaState, 5);
  roadColors.grassB = luaL_checknumber(luaState, 6);
  roadColors.wallA = luaL_checknumber(luaState, 7);
  roadColors.wallB = luaL_checknumber(luaState, 8);
  roadColors.railA = luaL_checknumber(luaState, 9);
  roadColors.railB = luaL_checknumber(luaState, 10);
  roadColors.lamp = luaL_checknumber(luaState, 11);
  roadColors.mountain = luaL_checknumber(luaState, 12);
  roadColors.centerLine = luaL_checknumber(luaState, 13);
  return 0;
}

static int lua_wrapper_setRoadDrawFlags(lua_State* luaState) {
  roadDrawFlags.drawMountain = lua_toboolean(luaState, 1);
  roadDrawFlags.drawLamps = lua_toboolean(luaState, 2);
  roadDrawFlags.drawRails = lua_toboolean(luaState, 3);
  roadDrawFlags.drawCenterLine = lua_toboolean(luaState, 4);
  roadDrawFlags.drawTunnelDoors = lua_toboolean(luaState, 5);
  return 0;
}

static int lua_wrapper_setRoadDimensions(lua_State* luaState) {
  roadDimensions.roadWidth = luaL_checknumber(luaState, 1);
  roadDimensions.embankmentWidth = luaL_checknumber(luaState, 2);
  roadDimensions.centerlineWidth = luaL_checknumber(luaState, 3);
  roadDimensions.railingDistance = luaL_checknumber(luaState, 4);
  roadDimensions.railingHeight = luaL_checknumber(luaState, 5);
  roadDimensions.railingThickness = luaL_checknumber(luaState, 6);
  roadDimensions.lampHeight = luaL_checknumber(luaState, 7);
  return 0;
}

static int lua_wrapper_drawRaceOutdoor(lua_State* luaState) {
  drawRaceOutdoor(luaDisplay,
    (int32_t)luaL_checknumber(luaState, 1), // x
    (int32_t)luaL_checknumber(luaState, 2), // y
    luaL_checknumber(luaState, 3),          // w
    (int32_t)luaL_checknumber(luaState, 4), // roadYOffset
    luaL_checknumber(luaState, 5),          // lastX
    luaL_checknumber(luaState, 6),          // lastW
    &roadColors,
    &roadDrawFlags,
    &roadDimensions
  );
  return 0;
}

static int lua_wrapper_drawRaceTunnel(lua_State* luaState) {
  drawRaceTunnel(luaDisplay,
    (int32_t)luaL_checknumber(luaState, 1), // x
    (int32_t)luaL_checknumber(luaState, 2), // y
    luaL_checknumber(luaState, 3),          // w
    (int32_t)luaL_checknumber(luaState, 4), // roadYOffset
    luaL_checknumber(luaState, 5),          // lastX
    luaL_checknumber(luaState, 6),          // lastW
    &roadColors,
    &roadDrawFlags,
    &roadDimensions
  );
  return 0;
}

static int lua_wrapper_calculateRoadProperties(lua_State* luaState) {
  float x;
  float w;
  int32_t roadYOffset;
  calculateRoadProperties(
    luaL_checknumber(luaState, 1), // y
    luaL_checknumber(luaState, 2), // distance
    luaL_checknumber(luaState, 3), // horizonY
    luaL_checknumber(luaState, 4), // baselineY
    luaL_checknumber(luaState, 5), // roadXOffset
    &x, &w, &roadYOffset
  );
  lua_pushnumber(luaState, x);
  lua_pushnumber(luaState, w);
  lua_pushinteger(luaState, roadYOffset);
  return 3;
}

static int lua_wrapper_projectRoadPointToScreen(lua_State* luaState) {
  float x;
  float y;
  float scale;
  projectRoadPointToScreen(
    luaL_checknumber(luaState, 1), // roadX
    luaL_checknumber(luaState, 2), // roadZ
    luaL_checknumber(luaState, 3), // horizonY
    luaL_checknumber(luaState, 4), // baselineY
    luaL_checknumber(luaState, 5), // roadXOffset
    &x, &y, &scale
  );
  lua_pushnumber(luaState, x);
  lua_pushnumber(luaState, y);
  lua_pushnumber(luaState, scale);
  return 3;
}

void initLua() {
  static bool bindingsInitiated = false;
  if (bindingsInitiated) {
    return;
  }
  luaState = luaL_newstate();
  luaopen_base(luaState);
  luaopen_table(luaState);
  lua_setglobal(luaState, "table");
  luaopen_string(luaState);
  lua_setglobal(luaState, "string");
  luaopen_math(luaState);
  lua_setglobal(luaState, "math");

  sprites = (TFT_eSprite*) heap_caps_malloc(sizeof(TFT_eSprite) * SPRITE_COUNT_LIMIT, MALLOC_CAP_SPIRAM);
  if (!sprites) {
    Serial.println("Failed to allocate sprites in PSRAM!");
    checkFailWithMessage("init: Failed to allocate sprites in PSRAM!");
    return;
  }

  for (size_t i = 0; i < SPRITE_COUNT_LIMIT; ++i) {
    new (&sprites[i]) TFT_eSprite(&tft);
    sprites[i].setColorDepth(16);
  }


  lua_register(luaState, "SerialPrintln", (lua_CFunction) &lua_wrapper_serialPrintln);
  lua_register(luaState, "SerialPrint", (lua_CFunction) &lua_wrapper_serialPrint);
  lua_register(luaState, "RunScript", (lua_CFunction) &lua_wrapper_runScript);
  lua_register(luaState, "LoadSprite", (lua_CFunction) &lua_wrapper_loadSprite);
  lua_register(luaState, "LoadAnimSprite", (lua_CFunction) &lua_wrapper_loadAnimSprite);
  lua_register(luaState, "FreeSprite", (lua_CFunction) &lua_wrapper_freeSprite);
  lua_register(luaState, "DrawSprite", (lua_CFunction) &lua_wrapper_drawSprite);
  lua_register(luaState, "DrawSpriteRegion", (lua_CFunction) &lua_wrapper_drawSpriteRegion);
  lua_register(luaState, "DrawAnimSprite", (lua_CFunction) &lua_wrapper_drawAnimSprite);
  lua_register(luaState, "DrawSpriteToSprite", (lua_CFunction) &lua_wrapper_drawSpriteToSprite);
  lua_register(luaState, "DrawSpriteScaled", (lua_CFunction) &lua_wrapper_drawSpriteScaled);
  lua_register(luaState, "DrawAnimSpriteScaled", (lua_CFunction) &lua_wrapper_drawAnimSpriteScaled);
  lua_register(luaState, "DrawSpriteScaledRotated", (lua_CFunction) &lua_wrapper_drawSpriteScaledRotated);
  lua_register(luaState, "DrawAnimSpriteScaledRotated", (lua_CFunction) &lua_wrapper_drawAnimSpriteScaledRotated);
  lua_register(luaState, "DrawSpriteTransformed", (lua_CFunction) &lua_wrapper_drawSpriteTransformed);
  lua_register(luaState, "SpriteWidth", (lua_CFunction) &lua_wrapper_spriteWidth);
  lua_register(luaState, "SpriteHeight", (lua_CFunction) &lua_wrapper_spriteHeight);
  lua_register(luaState, "DrawMode7", (lua_CFunction) &lua_wrapper_drawMode7);
  lua_register(luaState, "Mode7WorldToScreen", (lua_CFunction) &lua_wrapper_mode7WorldToScreen);
  lua_register(luaState, "Log", (lua_CFunction) &lua_wrapper_log);
  lua_register(luaState, "DrawString", (lua_CFunction) &lua_wrapper_drawString);
  lua_register(luaState, "DrawRect", (lua_CFunction) &lua_wrapper_drawRect);
  lua_register(luaState, "FillRect", (lua_CFunction) &lua_wrapper_fillRect);
  lua_register(luaState, "DrawCircle", (lua_CFunction) &lua_wrapper_drawCircle);
  lua_register(luaState, "FillCircle", (lua_CFunction) &lua_wrapper_fillCircle);
  lua_register(luaState, "DrawLine", (lua_CFunction) &lua_wrapper_drawLine);
  lua_register(luaState, "DrawFastHLine", (lua_CFunction) &lua_wrapper_drawFastHLine);
  lua_register(luaState, "DrawFastVLine", (lua_CFunction) &lua_wrapper_drawFastVLine);
  lua_register(luaState, "FillScreen", (lua_CFunction) &lua_wrapper_fillScreen);
  lua_register(luaState, "SetTextColor", (lua_CFunction) &lua_wrapper_setTextColor);
  lua_register(luaState, "SetTextSize", (lua_CFunction) &lua_wrapper_setTextSize);
  lua_register(luaState, "SetTextDatum", (lua_CFunction) &lua_wrapper_setTextDatum);
  lua_register(luaState, "SetCursor", (lua_CFunction) &lua_wrapper_setCursor);
  lua_register(luaState, "Print", (lua_CFunction) &lua_wrapper_print);
  lua_register(luaState, "Println", (lua_CFunction) &lua_wrapper_println);
  lua_register(luaState, "Cls", (lua_CFunction) &lua_wrapper_cls);
  lua_register(luaState, "PrefsSetString", (lua_CFunction) &lua_wrapper_prefsSetString);
  lua_register(luaState, "PrefsGetString", (lua_CFunction) &lua_wrapper_prefsGetString);
  lua_register(luaState, "PrefsSetInt", (lua_CFunction) &lua_wrapper_prefsSetInt);
  lua_register(luaState, "PrefsGetInt", (lua_CFunction) &lua_wrapper_prefsGetInt);
  lua_register(luaState, "PrefsSetNumber", (lua_CFunction) &lua_wrapper_prefsSetNumber);
  lua_register(luaState, "PrefsGetNumber", (lua_CFunction) &lua_wrapper_prefsGetNumber);
  lua_register(luaState, "CloseProgressionMenu", (lua_CFunction) &lua_wrapper_closeProgressionMenu);
  lua_register(luaState, "CloseWinScreen", (lua_CFunction) &lua_wrapper_closeWinScreen);
  lua_register(luaState, "Constrain", (lua_CFunction) &lua_wrapper_constrain);
  lua_register(luaState, "IsTouchInZone", (lua_CFunction) &lua_wrapper_isTouchInZone);
  lua_register(luaState, "GetTouchX", (lua_CFunction) &lua_wrapper_getTouchX);
  lua_register(luaState, "GetTouchY", (lua_CFunction) &lua_wrapper_getTouchY);
  lua_register(luaState, "GetTouchPressure", (lua_CFunction) &lua_wrapper_getTouchPressure);
  lua_register(luaState, "GetFreeRAM", (lua_CFunction) &lua_wrapper_getFreeRAM);
  lua_register(luaState, "GetFreePSRAM", (lua_CFunction) &lua_wrapper_getFreePSRAM);
  lua_register(luaState, "GetFreeSpriteSlots", (lua_CFunction) &lua_wrapper_getFreeSpriteSlots);
  lua_register(luaState, "DisableCaching", (lua_CFunction) &lua_wrapper_disableCaching);
  lua_register(luaState, "SetRoadColors", (lua_CFunction) &lua_wrapper_setRoadColors);
  lua_register(luaState, "SetRoadDrawFlags", (lua_CFunction) &lua_wrapper_setRoadDrawFlags);
  lua_register(luaState, "SetRoadDimensions", (lua_CFunction) &lua_wrapper_setRoadDimensions);
  lua_register(luaState, "DrawRaceOutdoor", (lua_CFunction) &lua_wrapper_drawRaceOutdoor);
  lua_register(luaState, "DrawRaceTunnel", (lua_CFunction) &lua_wrapper_drawRaceTunnel);
  lua_register(luaState, "CalculateRoadProperties", (lua_CFunction) &lua_wrapper_calculateRoadProperties);
  lua_register(luaState, "ProjectRoadPointToScreen", (lua_CFunction) &lua_wrapper_projectRoadPointToScreen);
  bindingsInitiated = true;
}

void initGames_lua(String gamePath, GameConfig* pGameConfig, String* errorMessage) {
  String ignoreErrorMessage;
  readGameConfig(gamePath, &gameConfig, errorMessage);
  initLua();
  String msString = "Ms="+String(millis());
  lua_dostring(msString.c_str(), "initGames_lua()");
  luaGamePath = gamePath;
  String luaGameIniPath = gamePath + "gameconfig.ini";
  String output;
  Serial.print("LUA GAME INI PATH: ");
  Serial.println(luaGameIniPath);
  getIniValue(luaGameIniPath, "[game]", "strictMode", &output, &ignoreErrorMessage);
  luaStrictMode = output.equalsIgnoreCase("true");
  luaCacheGameCode = true;

  setGamePrefsNamespace(gameConfig.prefsNamespace.c_str());

  lua_dofile(luaGamePath + "init.lua");
}

void updateBlowData(BlowData* blowData) {
  static uint32_t lastMs = 0;
  static int32_t lastKnownTaskNumber = -1;
  static uint32_t lastRepetition = 0;
  int32_t taskNumber = blowData->taskNumber + blowData->cycleNumber * blowData->totalTaskNumber;
  bool isNewTask = taskNumber != lastKnownTaskNumber;
  String blowDataString = "CurrentlyBlowing="+String(blowData->currentlyBlowing ? "true" : "false")+"\n"+\
                          "Ms="+String(blowData->ms)+"\n"+\
                          "MsDelta="+String(isNewTask ? 1 : blowData->ms - lastMs)+"\n"+\
                          "BlowStartMs="+String(blowData->blowStartMs)+"\n"+\
                          "BlowEndMs="+String(blowData->blowEndMs)+"\n"+\
                          "TargetDurationMs="+String(blowData->targetDurationMs)+"\n"+\
                          "CycleNumber="+String(blowData->cycleNumber)+"\n"+\
                          "TotalCycleNumber="+String(blowData->totalCycleNumber)+"\n"+\
                          "CurrentRepetition="+String(blowData->blowCount)+"\n"+\
                          "NewRepetition="+String((blowData->blowCount>lastRepetition) ? "true" : "false")+"\n"+\
                          "Repetitions="+String(blowData->totalBlowCount)+"\n"+\
                          "Pressure="+String(blowData->pressure)+"\n"+\
                          "PeakPressure="+String(blowData->peakPressure)+"\n"+\
                          "MinPressure="+String(blowData->minPressure)+"\n"+\
                          "CumulativeError="+String(blowData->cumulativeError)+"\n"+\
                          "Fails="+String(blowData->fails)+"\n"+\
                          "TaskType="+String(blowData->taskType)+"\n"+\
                          "LastBlowStatus="+String(blowData->lastBlowStatus)+"\n"+\
                          "TotalTimeSpentBreathing="+String(blowData->totalTimeSpentBreathing)+"\n"+\
                          "TaskStartMs="+String(blowData->taskStartMs)+"\n"+\
                          "CumulatedTaskNumber="+String(blowData->taskNumber + blowData->cycleNumber * blowData->totalTaskNumber)+"\n"+\
                          "TaskNumber="+String(taskNumber)+"\n"+\
                          "TotalTaskNumber="+String(blowData->totalTaskNumber)+"\n"+
                          "IsNewTask="+String(isNewTask ? "true" : "false")+"\n"+
                          "BreathingScore="+String(blowData->breathingScore);
  lastKnownTaskNumber = taskNumber;
  lua_dostring(blowDataString.c_str(), "updateBlowData()");
  lastMs = blowData->ms;
  lastRepetition = blowData->blowCount;
}

void updateJumpData(JumpData* jumpData) {
  static uint32_t lastMs = 0;
  static uint32_t lastRepetition = 0;
  static uint32_t lastBonusRepetition = 0;
  static uint32_t lastJumpMs = 0;
  int32_t taskNumber = jumpData->taskNumber + jumpData->cycleNumber * jumpData->totalTaskNumber;
  if (jumpData->jumpCount > lastRepetition) {
    lastJumpMs = jumpData->ms;
  }
  String jumpDataString = "Ms="+String(jumpData->ms)+"\n"+\
                          "MsDelta="+String(jumpData->ms - lastMs)+"\n"+\
                          "CycleNumber="+String(jumpData->cycleNumber)+"\n"+\
                          "TotalCycleNumber="+String(jumpData->totalCycleNumber)+"\n"+\
                          "CumulatedTaskNumber="+String(jumpData->taskNumber + jumpData->cycleNumber * jumpData->totalTaskNumber)+"\n"+\
                          "TaskNumber="+String(taskNumber)+"\n"+\
                          "TotalTaskNumber="+String(jumpData->totalTaskNumber)+"\n"+\
                          "CurrentRepetition="+String(jumpData->jumpCount)+"\n"+\
                          "CurrentBonusRepetition="+String(jumpData->bonusJumpCount)+"\n"+\
                          "CurrentlyJumping="+String(jumpData->currentlyJumping ? "true" : "false")+"\n"+\
                          "NewRepetition="+String((jumpData->jumpCount>lastRepetition) ? "true" : "false")+"\n"+\
                          "NewBonusRepetition="+String((jumpData->bonusJumpCount>lastBonusRepetition) ? "true" : "false")+"\n"+\
                          "MsLeft="+String(jumpData->msLeft)+"\n"+\
                          "LastJumpMs="+String(lastJumpMs);
  lua_dostring(jumpDataString.c_str(), "updateJumpData()");
  lastMs = jumpData->ms;
  lastRepetition = jumpData->jumpCount;
}

void drawShortBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaDisplay = display;
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepShortScriptPath);
}

void drawLongBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaDisplay = display;
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepLongScriptPath);
}

void drawEqualBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaDisplay = display;
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepEqualScriptPath);
}

void drawTrampolineGame_lua(DISPLAY_T* display, JumpData* jumpData, String* errorMessage) {
  luaDisplay = display;
  updateJumpData(jumpData);
  lua_dofile(luaGamePath + gameConfig.trampolineScriptPath);
}

void drawInhalationGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaDisplay = display;
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.inhalationScriptPath);
}

void drawInhalationBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaDisplay = display;
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.inhalationPepScriptPath);
}

bool displayProgressionMenu_lua(DISPLAY_T *display, String *errorMessage) {
  luaProgressionMenuRunning = true;
  luaDisplay = display;
  lua_dostring(("Ms="+String(millis())).c_str(), "displayProgressionMenu_lua()");
  String error = lua_dofile(luaGamePath + gameConfig.progressionMenuScriptPath);
  if (!error.isEmpty()) {
    errorMessage->concat(error);
    return false;
  }
  return luaProgressionMenuRunning;
}

void endGame_lua(String *errorMessage) {
  lua_dofile(luaGamePath + "end.lua", true);
  Serial.print("Collect garbage after running end script, free RAM before: ");
  Serial.println(ESP.getFreeHeap());
  lua_gc(luaState, LUA_GCCOLLECT, 0);
  Serial.print("Free RAM after: ");
  Serial.println(ESP.getFreeHeap());
}

bool displayWinScreen_lua(DISPLAY_T *display, String *errorMessage) {
  if (gameConfig.winScreenScriptPath.isEmpty()) {
    Serial.println("No win screen script defined, skipping...");
    luaWinScreenRunning = false;
    return false;
  }
  luaWinScreenRunning = true;
  luaDisplay = display;
  lua_dostring(("Ms="+String(millis())).c_str(), "displayWinScreen_lua()");
  String error = lua_dofile(luaGamePath + gameConfig.winScreenScriptPath);
  if (!error.isEmpty()) {
    errorMessage->concat(error);
    return false;
  }
  return luaWinScreenRunning;
}