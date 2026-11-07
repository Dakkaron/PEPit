#include "gameLua.h"
#include "constants.h"
#include "hardware/sdHandler.h"
#include "hardware/gfxHandler.hpp"
#include "hardware/higherLevelGfxHandler.hpp"
#include "hardware/engine3d.hpp"
#include "hardware/prefsHandler.h"
#include "hardware/joystickHandler.h"

static String luaGamePath;
lua_State* luaState;

DISPLAY_T* luaFramebuffer = nullptr;
DISPLAY_T* luaDisplay = nullptr;
bool luaProgressionMenuRunning;
bool luaWinScreenRunning;
bool luaStrictMode = false;
bool luaCacheGameCode = false;
static GameConfig gameConfig;

RoadColors roadColors;
RoadDrawFlags roadDrawFlags;
RoadDimensions roadDimensions;

#define SPRITE_COUNT_LIMIT 200

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
  static int compiledRef[10] = {LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF,
                                LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF, LUA_NOREF};
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
          if (compiledRef[cacheSlot] != LUA_NOREF) {
            luaL_unref(luaState, LUA_REGISTRYINDEX, compiledRef[cacheSlot]);
            compiledRef[cacheSlot] = LUA_NOREF;
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
        compiledRef[i] = compiledRef[i+1]; // registry refs are slot independent
      }
      if (script[9] != nullptr) {
        free(script[9]);
        script[9] = nullptr;
      }
      if (compiledRef[9] != LUA_NOREF) {
        luaL_unref(luaState, LUA_REGISTRYINDEX, compiledRef[9]);
        compiledRef[9] = LUA_NOREF;
      }
      cacheSlot = 9;
      script[cacheSlot] = readFileToNewPSBuffer(path.c_str());
      cachedPath[cacheSlot] = path;
      if (script[cacheSlot] == nullptr) {
        result = "Failed to load lua script "+path;
        return result;
      }
    }
    if (compiledRef[cacheSlot] != LUA_NOREF) {
      lua_rawgeti(luaState, LUA_REGISTRYINDEX, compiledRef[cacheSlot]);
      if (lua_pcall(luaState, 0, LUA_MULTRET, 0)) {
        result = "# lua error:\n" + String(lua_tostring(luaState, -1));
        lua_pop(luaState, 1);
        Serial.println("Error in lua file "+path+": " + result);
        if (luaStrictMode) {
          checkFailWithMessage("Error in lua file "+path+": " + result);
        }
      }
      return result;
    }
    if (script[cacheSlot] == nullptr) {
      script[cacheSlot] = readFileToNewPSBuffer(path.c_str());
      if (script[cacheSlot] == nullptr) {
        result = "Failed to load lua script "+path;
        return result;
      }
    }
    String redirectPath = findScriptRedirectPath(script[cacheSlot]);
    if (!redirectPath.isEmpty()) {
      result = lua_dofile(redirectPath);
      free(script[cacheSlot]);
      script[cacheSlot] = nullptr;
      return result;
    }
    if (luaL_loadstring(luaState, script[cacheSlot])) {
      result = "# lua error:\n" + String(lua_tostring(luaState, -1));
      lua_pop(luaState, 1);
      Serial.println("Error in lua file "+path+": " + result);
      if (luaStrictMode) {
        checkFailWithMessage("Error in lua file "+path+": " + result);
      }
      free(script[cacheSlot]);
      script[cacheSlot] = nullptr;
      return result;
    }
    // A failed compile stores NOREF, so the next frame retries from disk.
    compiledRef[cacheSlot] = luaL_ref(luaState, LUA_REGISTRYINDEX);
    free(script[cacheSlot]);
    script[cacheSlot] = nullptr;
    lua_rawgeti(luaState, LUA_REGISTRYINDEX, compiledRef[cacheSlot]);
    if (lua_pcall(luaState, 0, LUA_MULTRET, 0)) {
      result = "# lua error:\n" + String(lua_tostring(luaState, -1));
      lua_pop(luaState, 1);
      Serial.println("Error in lua file "+path+": " + result);
      if (luaStrictMode) {
        checkFailWithMessage("Error in lua file "+path+": " + result);
      }
    }
    return result;
  } else {
    //Serial.println("Caching disabled");
    // Slot 0 is shared with the cache — release whatever pair it held before
    // reusing it, otherwise cachedPath[0] keeps matching a freed script buffer.
    if (script[0] != nullptr) {
      free(script[0]);
      script[0] = nullptr;
    }
    cachedPath[0] = "";
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
      bool loadSpriteResult;
      if (maskingColor != -1) {
        loadSpriteResult = loadBmp(&sprites[i], path, options, maskingColor);
      } else {
        loadSpriteResult = loadBmp(&sprites[i], path, options);
      }
      if (!loadSpriteResult) {
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
  if (handle>=0 && handle<SPRITE_COUNT_LIMIT && sprites[handle].created()) {
    if (luaDisplay == &sprites[handle]) {
      luaDisplay = luaFramebuffer;
    }
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
      bool loadSpriteResult;
      if (maskingColor != -1) {
        loadSpriteResult = loadBmp(&sprites[i], path, options, maskingColor);
      } else {
        loadSpriteResult = loadBmp(&sprites[i], path, options);
      }
      if (!loadSpriteResult) {
        Serial.println("Failed to load sprite "+path);
        if (luaStrictMode) {
          checkFailWithMessage("Failed to load sprite "+path);
        }
        lua_pushinteger(luaState, -1);
        return 1;
      }
      Serial.print("Found sprite slot: ");
      Serial.println(i);
      spriteMetadata[i].frameW = luaL_checknumber(luaState, 2);
      spriteMetadata[i].frameH = luaL_checknumber(luaState, 3);
      if (spriteMetadata[i].frameW > sprites[i].width()) {
        Serial.printf("ERROR: Frame width (%d) is larger than sprite width (%d)!", spriteMetadata[i].frameW, sprites[i].width());
        sprites[i].deleteSprite();
        lua_pushinteger(luaState, -1);
        return 1;
      }
      if (spriteMetadata[i].frameH > sprites[i].height()) {
        Serial.printf("ERROR: Frame height (%d) is larger than sprite height (%d)!", spriteMetadata[i].frameH, sprites[i].height());
        sprites[i].deleteSprite();
        lua_pushinteger(luaState, -1);
        return 1;
      }
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
  barrierWorld3DFill3D();
  //Serial.print("Draw sprite ");
  int16_t handle = luaL_checkinteger(luaState, 1);
  //Serial.println(handle);
  Vector2D tPos;
  tPos.x = luaL_checknumber(luaState, 2);
  tPos.y = luaL_checknumber(luaState, 3);
  Vector2D scale;
  scale.x = 1.0f;
  scale.y = 1.0f;
  float angle=0.0f, alpha=1.0f;
  uint32_t flags=0;
  int32_t frame=-1;
  if (lua_istable(luaState, 4)) { // opts value is present
    lua_getfield(luaState, 4, "scaleX");scale.x = luaL_optnumber(luaState, -1, 1.0f);lua_pop(luaState, 1);
    lua_getfield(luaState, 4, "scaleY");scale.y = luaL_optnumber(luaState, -1, 1.0f);lua_pop(luaState, 1);
    lua_getfield(luaState, 4, "angle"); angle   = luaL_optnumber(luaState, -1, 0.0f);lua_pop(luaState, 1);
    lua_getfield(luaState, 4, "frame"); frame   = luaL_optnumber(luaState, -1, -1);lua_pop(luaState, 1);
    lua_getfield(luaState, 4, "flags"); flags   = luaL_optnumber(luaState, -1, 0);lua_pop(luaState, 1);
    lua_getfield(luaState, 4, "alpha"); alpha   = luaL_optnumber(luaState, -1, 1.0f);lua_pop(luaState, 1);
  }
  if (!isHandleValid(handle)) {
    return 0;
  }
  { // Claim the screen box this draw can paint so the gap fill leaves it alone.
    float sw, sh;
    if (frame < 0) { sw = sprites[handle].width(); sh = sprites[handle].height(); }
    else { sw = spriteMetadata[handle].frameW; sh = spriteMetadata[handle].frameH; }
    float fx0, fy0, fx1, fy1;
    if (angle == 0.0f) {
      fx0 = tPos.x; fy0 = tPos.y;
      fx1 = tPos.x + sw * fabsf(scale.x); fy1 = tPos.y + sh * fabsf(scale.y);
    } else { // matches the Matrix2D used by drawSpriteTransformed
      float c = cosf(angle), s = sinf(angle);
      float us[4] = {0, sw * scale.x, sw * scale.x, 0};
      float vs[4] = {0, 0, sh * scale.y, sh * scale.y};
      fx0 = fy0 = fx1 = fy1 = 0;
      for (int i = 0; i < 4; i++) {
        float rx = us[i] * c - vs[i] * s + tPos.x;
        float ry = us[i] * s + vs[i] * c + tPos.y;
        if (i == 0 || rx < fx0) fx0 = rx;
        if (i == 0 || rx > fx1) fx1 = rx;
        if (i == 0 || ry < fy0) fy0 = ry;
        if (i == 0 || ry > fy1) fy1 = ry;
      }
    }
    claimFramebufferRect3D(luaDisplay, (int16_t)floorf(fx0) - 1, (int16_t)floorf(fy0) - 1,
                           (int16_t)ceilf(fx1 - fx0) + 2, (int16_t)ceilf(fy1 - fy0) + 2);
  }
  int32_t maskingColor = spriteMetadata[handle].maskingColor;
  flags |= TRANSP_MASK;
  //Serial.printf("handle=%d, scaleX=%f, scaleY=%f, angle=%f, frame=%d, flags=%d, alpha=%f\n", handle, scale.x, scale.y, angle, frame, flags, alpha);
  if (frame<0) {
    if (scale.x==1.0f && scale.y==1.0f && angle==0.0f) {
      drawSprite(luaDisplay, &(sprites[handle]), tPos.x, tPos.y, spriteMetadata[handle].maskingColor, alpha, 0, 0, -1, -1, flags);
    } else if (angle==0.0f) {
      drawSpriteScaled(luaDisplay, &sprites[handle], &tPos, &scale, flags, spriteMetadata[handle].maskingColor);
    } else {
      float s = sin(angle);
      float c = cos(angle);
      Matrix2D transform = {
        c * scale.x, -s,
        s, c * scale.y
      };
      drawSpriteTransformed(luaDisplay, &sprites[handle], &tPos, &transform, flags, spriteMetadata[handle].maskingColor, alpha);
    }
  } else {
    int32_t sw = spriteMetadata[handle].frameW;
    int32_t sh = spriteMetadata[handle].frameH;

    int32_t cols = sprites[handle].width() / sw;

    int32_t col = frame % cols;
    int32_t row = frame / cols;
    if (scale.x==1.0f && scale.y==1.0f && angle==0.0f && frame==0) {
      drawSprite(luaDisplay, &(sprites[handle]), tPos.x, tPos.y, spriteMetadata[handle].maskingColor, alpha, sw*col, sh*row, sw, sh, flags);
    } else if (angle==0.0f) {
      drawSpriteScaled(luaDisplay, &sprites[handle], &tPos, &scale, flags, spriteMetadata[handle].maskingColor, sw, sh, frame, alpha);
    } else {
      float s = sin(angle);
      float c = cos(angle);
      Matrix2D transform = {
        c * scale.x, -s,
        s, c * scale.y
      };
      drawSpriteTransformed(luaDisplay, &sprites[handle], &tPos, &transform, flags, spriteMetadata[handle].maskingColor, sw, sh, frame);
    }
  }
  //Serial.println("Draw sprite done");
  return 0;
}

static int lua_wrapper_setDrawTargetSprite(lua_State* luaState) {
  int16_t handle = luaL_checkinteger(luaState, 1);
  if (!isHandleValid(handle)) {
    luaDisplay = luaFramebuffer;
    return 0;
  }
  luaDisplay = &sprites[handle];
  return 0;
}

static int lua_wrapper_setDrawTargetFramebuffer(lua_State* luaState) {
  luaDisplay = luaFramebuffer;
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
  barrierWorld3DFill3D();
  float minY = startY < endY ? startY : endY;
  float maxY = startY < endY ? endY : startY;
  claimFramebufferRect3D(luaDisplay, 0, (int16_t)floorf(minY) - 1, luaDisplay->width(),
                         (int16_t)ceilf(maxY - minY) + 3);
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

static int lua_wrapper_drawString(lua_State* luaState) {
  barrierWorld3DFill3D();
  String str = luaL_checkstring(luaState, 1);
  str.replace("€", "¶"); // Substitute € for ¶, because the € sign is very far down the
                         // unicode table, so ¶ is used as a stand-in for € to save space.
  char* s = (char*)malloc(strlen(str.c_str())+1);
  strcpy(s, str.c_str());
  int32_t x = luaL_checknumber(luaState, 2);
  int32_t y = luaL_checknumber(luaState, 3);
  char* token = strtok(s, "\n");
  int32_t fontHeight = luaDisplay->fontHeight();
  while (token != NULL) {
    claimTextRect3D(luaDisplay, token, x, y);
    luaDisplay->drawString(token, x, y);
    y += fontHeight;
    token = strtok(NULL, "\n");
  }
  free(s);
  return 0;
}

static int lua_wrapper_drawRect(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  int16_t h = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  claimFramebufferRect3D(luaDisplay, x - 1, y - 1, w + 2, 3);
  claimFramebufferRect3D(luaDisplay, x - 1, y + h - 2, w + 2, 3);
  claimFramebufferRect3D(luaDisplay, x - 1, y - 1, 3, h + 2);
  claimFramebufferRect3D(luaDisplay, x + w - 2, y - 1, 3, h + 2);
  luaDisplay->drawRect(x, y, w, h, color);
  return 0;
}

static int lua_wrapper_fillRect(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  int16_t h = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  claimFramebufferRect3D(luaDisplay, x, y, w, h);
  luaDisplay->fillRect(x, y, w, h, color);
  return 0;
}

static int lua_wrapper_drawCircle(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t r = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  claimFramebufferRect3D(luaDisplay, x - r - 1, y - r - 1, 2 * r + 3, 2 * r + 3);
  luaDisplay->drawCircle(x, y, r, color);
  return 0;
}

static int lua_wrapper_fillCircle(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t r = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  claimFramebufferRect3D(luaDisplay, x - r - 1, y - r - 1, 2 * r + 3, 2 * r + 3);
  luaDisplay->fillCircle(x, y, r, color);
  return 0;
}

static int lua_wrapper_drawLine(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x0 = luaL_checknumber(luaState, 1);
  int16_t y0 = luaL_checknumber(luaState, 2);
  int16_t x1 = luaL_checknumber(luaState, 3);
  int16_t y1 = luaL_checknumber(luaState, 4);
  uint16_t color = luaL_checknumber(luaState, 5);
  int16_t rx0 = x0 < x1 ? x0 : x1, rx1 = x0 < x1 ? x1 : x0;
  int16_t ry0 = y0 < y1 ? y0 : y1, ry1 = y0 < y1 ? y1 : y0;
  claimFramebufferRect3D(luaDisplay, rx0 - 1, ry0 - 1, rx1 - rx0 + 2, ry1 - ry0 + 2);
  luaDisplay->drawLine(x0, y0, x1, y1, color);
  return 0;
}

static int lua_wrapper_drawFastHLine(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t w = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  claimFramebufferRect3D(luaDisplay, x - 1, y - 1, w + 2, 3);
  luaDisplay->drawFastHLine(x, y, w, color);
  return 0;
}

static int lua_wrapper_drawFastVLine(lua_State* luaState) {
  barrierWorld3DFill3D();
  int16_t x = luaL_checknumber(luaState, 1);
  int16_t y = luaL_checknumber(luaState, 2);
  int16_t h = luaL_checknumber(luaState, 3);
  uint16_t color = luaL_checknumber(luaState, 4);
  claimFramebufferRect3D(luaDisplay, x - 1, y - 1, 3, h + 2);
  luaDisplay->drawFastVLine(x, y, h, color);
  return 0;
}

static int lua_wrapper_fillSprite(lua_State* luaState) {
  barrierWorld3DFill3D();
  uint16_t color = luaL_checknumber(luaState, 1);
  if (!fillFramebufferMasked3D(luaDisplay, color)) luaDisplay->fillScreen(color);
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

static void claimPrintRect(DISPLAY_T* d, const String& s) {
  if (!world3DMaskMode3D(d)) return;
  int16_t x = d->getCursorX();
  int16_t y = d->getCursorY();
  claimTextRect3D(d, s.c_str(), x, y);
  int32_t tw = d->textWidth(s.c_str());
  if (tw <= 0) tw = (int32_t)s.length() * (d->textsize * 6 + 1);
  if (x + tw > d->width()) { // wrapped remainder paints the next line
    claimFramebufferRect3D(d, 0, y, d->width(), d->fontHeight() * 2 + 2);
  }
}

static int lua_wrapper_print(lua_State* luaState) {
  barrierWorld3DFill3D();
  String s = luaL_checkstring(luaState, 1);
  s.replace("€", "¶"); // Substitute € for ¶, because the € sign is very far down the
                       // unicode table, so ¶ is used as a stand-in for € to save space.
  claimPrintRect(luaDisplay, s);
  luaDisplay->print(s);
  return 0;
}

static int lua_wrapper_println(lua_State* luaState) {
  barrierWorld3DFill3D();
  String s = luaL_checkstring(luaState, 1);
  s.replace("€", "¶"); // Substitute € for ¶, because the € sign is very far down the
                       // unicode table, so ¶ is used as a stand-in for € to save space.
  claimPrintRect(luaDisplay, s);
  luaDisplay->println(s);
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

static int lua_wrapper_prefsGetBytes(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  if (!prefs.isKey(key.c_str())) {
    lua_pushnil(luaState);
    return 1;
  }
  size_t len = prefs.getBytesLength(key.c_str());
  if (len > 8192) len = 8192; // Cap to keep large blobs out of the Lua stack
  uint8_t* buf = (uint8_t*)malloc(len ? len : 1);
  size_t readLen = prefs.getBytes(key.c_str(), buf, len);
  lua_pushlstring(luaState, (const char*)buf, readLen);
  free(buf);
  return 1;
}

static int lua_wrapper_prefsSetBytes(lua_State* luaState) {
  String key = luaL_checkstring(luaState, 1);
  size_t len = 0;
  const char* data = luaL_checklstring(luaState, 2, &len);
  prefs.putBytes(key.c_str(), data, len);
  prefs.end();
  applyGamePrefsNamespace();
  return 0;
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
  lua_pushinteger(luaState, SPRITE_COUNT_LIMIT-count);
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

static int lua_wrapper_isJoystickPresent(lua_State* luaState) {
  lua_pushnumber(luaState, isJoystickPresent());
  return 1;
}

static int lua_wrapper_getJoystickXY(lua_State* luaState) {
  float x;
  float y;
  getJoystickXY(&x, &y);
  lua_pushnumber(luaState, x);
  lua_pushnumber(luaState, y);
  return 2;
}

static int lua_wrapper_getJoystickX(lua_State* luaState) {
  float x;
  float y;
  getJoystickXY(&x, &y);
  lua_pushnumber(luaState, x);
  return 1;
}

static int lua_wrapper_getJoystickY(lua_State* luaState) {
  float x;
  float y;
  getJoystickXY(&x, &y);
  lua_pushnumber(luaState, y);
  return 1;
}

static int lua_wrapper_getJoystickButton(lua_State* luaState) {
  lua_pushboolean(luaState, getJoystickButton());
  return 1;
}

static void *l_alloc_psram (void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;  /* not used */
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  else
    return heap_caps_realloc(ptr, nsize, MALLOC_CAP_SPIRAM);
}

static int panic (lua_State *L) {
  const char *msg = (lua_type(L, -1) == LUA_TSTRING)
                  ? lua_tostring(L, -1)
                  : "error object is not a string";
  lua_writestringerror("PANIC: unprotected error in call to Lua API (%s)\n",
                        msg);
  return 0;  /* return to Lua to abort */
}

static void warnfoff (void *ud, const char *message, int tocont);
static void warnfon (void *ud, const char *message, int tocont);
static void warnfcont (void *ud, const char *message, int tocont);

static int checkcontrol (lua_State *L, const char *message, int tocont) {
  if (tocont || *(message++) != '@')  /* not a control message? */
    return 0;
  else {
    if (strcmp(message, "off") == 0)
      lua_setwarnf(L, warnfoff, L);  /* turn warnings off */
    else if (strcmp(message, "on") == 0)
      lua_setwarnf(L, warnfon, L);   /* turn warnings on */
    return 1;  /* it was a control message */
  }
}

static void warnfcont (void *ud, const char *message, int tocont) {
  lua_State *L = (lua_State *)ud;
  lua_writestringerror("%s", message);  /* write message */
  if (tocont)  /* not the last part? */
    lua_setwarnf(L, warnfcont, L);  /* to be continued */
  else {  /* last part */
    lua_writestringerror("%s", "\n");  /* finish message with end-of-line */
    lua_setwarnf(L, warnfon, L);  /* next call is a new message */
  }
}

static void warnfon (void *ud, const char *message, int tocont) {
  if (checkcontrol((lua_State *)ud, message, tocont))  /* control message? */
    return;  /* nothing else to be done */
  lua_writestringerror("%s", "Lua warning: ");  /* start a new warning */
  warnfcont(ud, message, tocont);  /* finish processing */
}

static void warnfoff (void *ud, const char *message, int tocont) {
  checkcontrol((lua_State *)ud, message, tocont);
}



static uint8_t luaColorArg(lua_State* luaState, int32_t index) {
  int32_t value = luaL_checkinteger(luaState, index);
  if (value < 0) value = 0;
  if (value > 255) value = 255;
  return (uint8_t)value;
}

static int lua_wrapper_loadMesh(lua_State* luaState) {
  String path = luaGamePath + luaL_checkstring(luaState, 1);
  lua_pushinteger(luaState, loadMesh3D(path));
  return 1;
}

static int lua_wrapper_createCube(lua_State* luaState) {
  lua_pushinteger(luaState, createMesh3DCube());
  return 1;
}

static int lua_wrapper_createSphere(lua_State* luaState) {
  float radius = luaL_checknumber(luaState, 1);
  int32_t segments = luaL_optinteger(luaState, 2, 12);
  lua_pushinteger(luaState, createMesh3DSphere(radius, (uint8_t)segments));
  return 1;
}

static int lua_wrapper_createPlane(lua_State* luaState) {
  float width = luaL_checknumber(luaState, 1);
  float height = luaL_checknumber(luaState, 2);
  lua_pushinteger(luaState, createMesh3DPlane(width, height));
  return 1;
}

static int lua_wrapper_freeMesh(lua_State* luaState) {
  lua_pushboolean(luaState, freeMesh3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_setMeshColor(lua_State* luaState) {
  setMeshColor3D(luaL_checkinteger(luaState, 1), luaColorArg(luaState, 2), luaColorArg(luaState, 3), luaColorArg(luaState, 4));
  return 0;
}

static int lua_wrapper_loadTexture(lua_State* luaState) {
  String path = luaGamePath + luaL_checkstring(luaState, 1);
  lua_pushinteger(luaState, loadTexture3D(path));
  return 1;
}

static int lua_wrapper_freeTexture(lua_State* luaState) {
  lua_pushboolean(luaState, freeTexture3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_setMeshTexture(lua_State* luaState) {
  setMeshTexture3D(luaL_checkinteger(luaState, 1), luaL_checkinteger(luaState, 2));
  return 0;
}

static int lua_wrapper_setMeshFlipped(lua_State* luaState) {
  setMeshFlipped3D(luaL_checkinteger(luaState, 1), luaL_checkinteger(luaState, 2) != 0);
  return 0;
}

static int lua_wrapper_createEntity(lua_State* luaState) {
  lua_pushinteger(luaState, createEntity3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_freeEntity(lua_State* luaState) {
  lua_pushboolean(luaState, freeEntity3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_positionEntity(lua_State* luaState) {
  positionEntity3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_rotateEntity(lua_State* luaState) {
  rotateEntity3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_turnEntity(lua_State* luaState) {
  turnEntity3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_scaleEntity(lua_State* luaState) {
  scaleEntity3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_entityColor(lua_State* luaState) {
  entityColor3D(luaL_checkinteger(luaState, 1), luaColorArg(luaState, 2), luaColorArg(luaState, 3), luaColorArg(luaState, 4));
  return 0;
}

static int lua_wrapper_hideEntity(lua_State* luaState) {
  hideEntity3D(luaL_checkinteger(luaState, 1), luaL_checkinteger(luaState, 2) != 0);
  return 0;
}

static int lua_wrapper_linkEntity(lua_State* luaState) {
  lua_pushboolean(luaState, linkEntity3D(luaL_checkinteger(luaState, 1), luaL_checkinteger(luaState, 2)));
  return 1;
}

static int lua_wrapper_entityOrder(lua_State* luaState) {
  entityOrder3D(luaL_checkinteger(luaState, 1), (int8_t)luaL_checkinteger(luaState, 2));
  return 0;
}

static int lua_wrapper_createCamera(lua_State* luaState) {
  lua_pushinteger(luaState, createCamera3D());
  return 1;
}

static int lua_wrapper_freeCamera(lua_State* luaState) {
  lua_pushboolean(luaState, freeCamera3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_positionCamera(lua_State* luaState) {
  positionCamera3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_rotateCamera(lua_State* luaState) {
  rotateCamera3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_turnCamera(lua_State* luaState) {
  turnCamera3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_cameraZoom(lua_State* luaState) {
  cameraZoom3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2));
  return 0;
}

static int lua_wrapper_cameraRange(lua_State* luaState) {
  cameraRange3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3));
  return 0;
}

static int lua_wrapper_ambientLight(lua_State* luaState) {
  ambientLight3D(luaColorArg(luaState, 1), luaColorArg(luaState, 2), luaColorArg(luaState, 3));
  return 0;
}

static int lua_wrapper_createLight(lua_State* luaState) {
  uint8_t r = luaColorArg(luaState, 1);
  uint8_t g = luaColorArg(luaState, 2);
  uint8_t b = luaColorArg(luaState, 3);
  float x = luaL_checknumber(luaState, 4);
  float y = luaL_checknumber(luaState, 5);
  float z = luaL_checknumber(luaState, 6);
  float range = luaL_optnumber(luaState, 7, 0);
  int32_t type = luaL_optinteger(luaState, 8, LIGHT3D_TYPE_POINT);
  lua_pushinteger(luaState, createLight3D(r, g, b, x, y, z, range, (uint8_t)type));
  return 1;
}

static int lua_wrapper_freeLight(lua_State* luaState) {
  lua_pushboolean(luaState, freeLight3D(luaL_checkinteger(luaState, 1)));
  return 1;
}

static int lua_wrapper_setLightDirection(lua_State* luaState) {
  setLightDirection3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3), luaL_checknumber(luaState, 4));
  return 0;
}

static int lua_wrapper_setLightCone(lua_State* luaState) {
  setLightCone3D(luaL_checkinteger(luaState, 1), luaL_checknumber(luaState, 2), luaL_checknumber(luaState, 3));
  return 0;
}

static int lua_wrapper_updateWorld(lua_State* luaState) {
  updateWorld3D(luaDisplay);
  return 0;
}

static int lua_wrapper_drawWorld(lua_State* luaState) {
  drawWorld3D(luaDisplay);
  return 0;
}

static int lua_wrapper_setViewport(lua_State* luaState) {
  int32_t x = luaL_checkinteger(luaState, 1);
  int32_t y = luaL_checkinteger(luaState, 2);
  int32_t w = luaL_checkinteger(luaState, 3);
  int32_t h = luaL_checkinteger(luaState, 4);
  setWorld3DViewport3D(x, y, w, h);
  return 0;
}

static int lua_wrapper_freeAll3D(lua_State* luaState) {
  quiesceWorld3DBlit3D();
  resetWorld3D();
  return 0;
}


void initLua() {
  static bool bindingsInitiated = false;
  if (bindingsInitiated) {
    return;
  }
  luaState = lua_newstate(l_alloc_psram, NULL);
  lua_atpanic(luaState, &panic);
  lua_setwarnf(luaState, warnfoff, luaState);
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
  lua_register(luaState, "SetDrawTargetSprite", (lua_CFunction) &lua_wrapper_setDrawTargetSprite);
  lua_register(luaState, "SetDrawTargetFramebuffer", (lua_CFunction) &lua_wrapper_setDrawTargetFramebuffer);
  lua_register(luaState, "SpriteWidth", (lua_CFunction) &lua_wrapper_spriteWidth);
  lua_register(luaState, "SpriteHeight", (lua_CFunction) &lua_wrapper_spriteHeight);
  lua_register(luaState, "DrawMode7", (lua_CFunction) &lua_wrapper_drawMode7);
  lua_register(luaState, "Mode7WorldToScreen", (lua_CFunction) &lua_wrapper_mode7WorldToScreen);
  lua_register(luaState, "DrawString", (lua_CFunction) &lua_wrapper_drawString);
  lua_register(luaState, "DrawRect", (lua_CFunction) &lua_wrapper_drawRect);
  lua_register(luaState, "FillRect", (lua_CFunction) &lua_wrapper_fillRect);
  lua_register(luaState, "DrawCircle", (lua_CFunction) &lua_wrapper_drawCircle);
  lua_register(luaState, "FillCircle", (lua_CFunction) &lua_wrapper_fillCircle);
  lua_register(luaState, "DrawLine", (lua_CFunction) &lua_wrapper_drawLine);
  lua_register(luaState, "DrawFastHLine", (lua_CFunction) &lua_wrapper_drawFastHLine);
  lua_register(luaState, "DrawFastVLine", (lua_CFunction) &lua_wrapper_drawFastVLine);
  lua_register(luaState, "FillSprite", (lua_CFunction) &lua_wrapper_fillSprite);
  lua_register(luaState, "SetTextColor", (lua_CFunction) &lua_wrapper_setTextColor);
  lua_register(luaState, "SetTextSize", (lua_CFunction) &lua_wrapper_setTextSize);
  lua_register(luaState, "SetTextDatum", (lua_CFunction) &lua_wrapper_setTextDatum);
  lua_register(luaState, "SetCursor", (lua_CFunction) &lua_wrapper_setCursor);
  lua_register(luaState, "Print", (lua_CFunction) &lua_wrapper_print);
  lua_register(luaState, "Println", (lua_CFunction) &lua_wrapper_println);
  lua_register(luaState, "PrefsSetString", (lua_CFunction) &lua_wrapper_prefsSetString);
  lua_register(luaState, "PrefsGetString", (lua_CFunction) &lua_wrapper_prefsGetString);
  lua_register(luaState, "PrefsSetInt", (lua_CFunction) &lua_wrapper_prefsSetInt);
  lua_register(luaState, "PrefsGetInt", (lua_CFunction) &lua_wrapper_prefsGetInt);
  lua_register(luaState, "PrefsSetNumber", (lua_CFunction) &lua_wrapper_prefsSetNumber);
  lua_register(luaState, "PrefsGetNumber", (lua_CFunction) &lua_wrapper_prefsGetNumber);
  lua_register(luaState, "PrefsGetBytes", (lua_CFunction) &lua_wrapper_prefsGetBytes);
  lua_register(luaState, "PrefsSetBytes", (lua_CFunction) &lua_wrapper_prefsSetBytes);
  lua_register(luaState, "CloseProgressionMenu", (lua_CFunction) &lua_wrapper_closeProgressionMenu);
  lua_register(luaState, "CloseWinScreen", (lua_CFunction) &lua_wrapper_closeWinScreen);
  lua_register(luaState, "Constrain", (lua_CFunction) &lua_wrapper_constrain);
  lua_register(luaState, "IsTouchInZone", (lua_CFunction) &lua_wrapper_isTouchInZone);
  lua_register(luaState, "GetTouchX", (lua_CFunction) &lua_wrapper_getTouchX);
  lua_register(luaState, "GetTouchY", (lua_CFunction) &lua_wrapper_getTouchY);
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
  lua_register(luaState, "IsJoystickPresent", (lua_CFunction) &lua_wrapper_isJoystickPresent);
  lua_register(luaState, "GetJoystickXY", (lua_CFunction) &lua_wrapper_getJoystickXY);
  lua_register(luaState, "GetJoystickX", (lua_CFunction) &lua_wrapper_getJoystickX);
  lua_register(luaState, "GetJoystickY", (lua_CFunction) &lua_wrapper_getJoystickY);
  lua_register(luaState, "GetJoystickButton", (lua_CFunction) &lua_wrapper_getJoystickButton);

  String flags = "FLIPPED_H="+String(FLIPPED_H)+"\n"+\
                 "FLIPPED_V="+String(FLIPPED_V)+"\n"+\
                 "DITHER_TRANSPARENCY="+String(DITHER_TRANSPARENCY)+"\n"+\
                 "ALIGN_H_LEFT="+String(ALIGN_H_LEFT)+"\n"+\
                 "ALIGN_H_CENTER="+String(ALIGN_H_CENTER)+"\n"+\
                 "ALIGN_H_RIGHT="+String(ALIGN_H_RIGHT)+"\n"+\
                 "ALIGN_V_TOP="+String(ALIGN_V_TOP)+"\n"+\
                 "ALIGN_V_CENTER="+String(ALIGN_V_CENTER)+"\n"+\
                 "ALIGN_V_BOTTOM="+String(ALIGN_V_BOTTOM)+"\n"+\
                 "TRANSP_OFF="+String(TRANSP_OFF)+"\n"+\
                 "TRANSP_MASK="+String(TRANSP_MASK);
  lua_dostring(flags.c_str(), "init()");
  lua_register(luaState, "LoadMesh", (lua_CFunction) &lua_wrapper_loadMesh);
  lua_register(luaState, "CreateCube", (lua_CFunction) &lua_wrapper_createCube);
  lua_register(luaState, "CreateSphere", (lua_CFunction) &lua_wrapper_createSphere);
  lua_register(luaState, "CreatePlane", (lua_CFunction) &lua_wrapper_createPlane);
  lua_register(luaState, "FreeMesh", (lua_CFunction) &lua_wrapper_freeMesh);
  lua_register(luaState, "SetMeshColor", (lua_CFunction) &lua_wrapper_setMeshColor);
  lua_register(luaState, "LoadTexture", (lua_CFunction) &lua_wrapper_loadTexture);
  lua_register(luaState, "FreeTexture", (lua_CFunction) &lua_wrapper_freeTexture);
  lua_register(luaState, "SetMeshTexture", (lua_CFunction) &lua_wrapper_setMeshTexture);
  lua_register(luaState, "SetMeshFlipped", (lua_CFunction) &lua_wrapper_setMeshFlipped);
  lua_register(luaState, "CreateEntity", (lua_CFunction) &lua_wrapper_createEntity);
  lua_register(luaState, "FreeEntity", (lua_CFunction) &lua_wrapper_freeEntity);
  lua_register(luaState, "PositionEntity", (lua_CFunction) &lua_wrapper_positionEntity);
  lua_register(luaState, "RotateEntity", (lua_CFunction) &lua_wrapper_rotateEntity);
  lua_register(luaState, "TurnEntity", (lua_CFunction) &lua_wrapper_turnEntity);
  lua_register(luaState, "ScaleEntity", (lua_CFunction) &lua_wrapper_scaleEntity);
  lua_register(luaState, "EntityColor", (lua_CFunction) &lua_wrapper_entityColor);
  lua_register(luaState, "HideEntity", (lua_CFunction) &lua_wrapper_hideEntity);
  lua_register(luaState, "LinkEntity", (lua_CFunction) &lua_wrapper_linkEntity);
  lua_register(luaState, "EntityOrder", (lua_CFunction) &lua_wrapper_entityOrder);
  lua_register(luaState, "CreateCamera", (lua_CFunction) &lua_wrapper_createCamera);
  lua_register(luaState, "FreeCamera", (lua_CFunction) &lua_wrapper_freeCamera);
  lua_register(luaState, "PositionCamera", (lua_CFunction) &lua_wrapper_positionCamera);
  lua_register(luaState, "RotateCamera", (lua_CFunction) &lua_wrapper_rotateCamera);
  lua_register(luaState, "TurnCamera", (lua_CFunction) &lua_wrapper_turnCamera);
  lua_register(luaState, "CameraZoom", (lua_CFunction) &lua_wrapper_cameraZoom);
  lua_register(luaState, "CameraRange", (lua_CFunction) &lua_wrapper_cameraRange);
  lua_register(luaState, "AmbientLight", (lua_CFunction) &lua_wrapper_ambientLight);
  lua_register(luaState, "CreateLight", (lua_CFunction) &lua_wrapper_createLight);
  lua_register(luaState, "FreeLight", (lua_CFunction) &lua_wrapper_freeLight);
  lua_register(luaState, "SetLightDirection", (lua_CFunction) &lua_wrapper_setLightDirection);
  lua_register(luaState, "SetLightCone", (lua_CFunction) &lua_wrapper_setLightCone);
  lua_register(luaState, "UpdateWorld", (lua_CFunction) &lua_wrapper_updateWorld);
  lua_register(luaState, "DrawWorld", (lua_CFunction) &lua_wrapper_drawWorld);
  lua_register(luaState, "SetViewport", (lua_CFunction) &lua_wrapper_setViewport);
  lua_register(luaState, "FreeAll3D", (lua_CFunction) &lua_wrapper_freeAll3D);
  lua_pushinteger(luaState, LIGHT3D_TYPE_POINT);
  lua_setglobal(luaState, "POINTLIGHT");
  lua_pushinteger(luaState, LIGHT3D_TYPE_SPOT);
  lua_setglobal(luaState, "SPOTLIGHT");
  lua_pushinteger(luaState, LIGHT3D_TYPE_DIRECTIONAL);
  lua_setglobal(luaState, "DIRECTIONALLIGHT");
  bindingsInitiated = true;
}

void initGames_lua(String gamePath, GameConfig* pGameConfig, String* errorMessage) {
  String ignoreErrorMessage;
  readGameConfig(gamePath, &gameConfig, errorMessage);
  setWorld3DDebug3D(systemConfig.debugLogRender3D);
  setWorld3DI80Push3D(systemConfig.renderI80Push3D);
  setWorld3DSplitXf3D(systemConfig.renderSplitXf3D);
  resetWorld3D();
  // Games default to the area not covered by system UI and the pressure/time
  // bars; a game can change it with SetViewport(x, y, w, h).
  setWorld3DViewport3D(0, 21, SCREEN_WIDTH, SCREEN_HEIGHT - 72);
  initLua();
  String variablesString = "Ms="+String(millis())+"\n"+\
                    "LeftHandedMode="+String(systemConfig.leftHandMode ? "true\n" : "false\n");
  lua_dostring(variablesString.c_str(), "initGames_lua()");
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
  String blowDataString = "CurrentlyBlowing="+String(blowData->currentlyBlowing ? "true\n" : "false\n")+\
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
                          "TotalTaskNumber="+String(blowData->totalTaskNumber)+"\n"+\
                          "IsNewTask="+String(isNewTask ? "true\n" : "false\n")+\
                          "BreathingScore="+String(blowData->breathingScore)+"\n"+\
                          "LeftHandedMode="+String(systemConfig.leftHandMode ? "true\n" : "false\n");
  lastKnownTaskNumber = taskNumber;
  lua_dostring(blowDataString.c_str(), "updateBlowData()");
  lastMs = blowData->ms;
  lastRepetition = blowData->blowCount;
}

void updateJumpData(JumpData* jumpData) {
  static uint32_t lastMs = 0;
  static int32_t lastKnownTaskNumber = -1;
  static uint32_t lastRepetition = 0;
  static uint32_t lastBonusRepetition = 0;
  static uint32_t lastJumpMs = 0;
  int32_t taskNumber = jumpData->taskNumber + jumpData->cycleNumber * jumpData->totalTaskNumber;
  bool isNewTask = taskNumber != lastKnownTaskNumber;
  if (jumpData->jumpCount > lastRepetition) {
    lastJumpMs = jumpData->ms;
  }
  String jumpDataString = "Ms="+String(jumpData->ms)+"\n"+\
                          "MsDelta="+String(isNewTask ? 1 : jumpData->ms - lastMs)+"\n"+\
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
                          "LastJumpMs="+String(lastJumpMs)+"\n"+\
                          "LeftHandedMode="+String(systemConfig.leftHandMode ? "true\n" : "false\n");
  lastKnownTaskNumber = taskNumber;
  lua_dostring(jumpDataString.c_str(), "updateJumpData()");
  lastMs = jumpData->ms;
  lastRepetition = jumpData->jumpCount;
}

void drawShortBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepShortScriptPath);
}

void drawLongBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepLongScriptPath);
}

void drawEqualBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.pepEqualScriptPath);
}

void drawTrampolineGame_lua(DISPLAY_T* display, JumpData* jumpData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateJumpData(jumpData);
  lua_dofile(luaGamePath + gameConfig.trampolineScriptPath);
}

void drawInhalationGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.inhalationScriptPath);
}

void drawInhalationBlowGame_lua(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  updateBlowData(blowData);
  lua_dofile(luaGamePath + gameConfig.inhalationPepScriptPath);
}

bool displayProgressionMenu_lua(DISPLAY_T *display, String *errorMessage) {
  luaProgressionMenuRunning = true;
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
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
  luaFramebuffer = display;
  if (luaDisplay == nullptr) {
    luaDisplay = display;
  }
  lua_dostring(("Ms="+String(millis())).c_str(), "displayWinScreen_lua()");
  String error = lua_dofile(luaGamePath + gameConfig.winScreenScriptPath);
  if (!error.isEmpty()) {
    errorMessage->concat(error);
    return false;
  }
  return luaWinScreenRunning;
}
