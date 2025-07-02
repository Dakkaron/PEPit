#include "games.h"
#include "gameMonsterCatcher.h"
#include "gameRacing.h"
#include "gameLua.h"

#define EXECUTION_LOG_MAX_LINES 24

String currentGamePath;
GameConfig gameConfig;
void initGames(String gamePath, String* errorMessage) {
  currentGamePath = gamePath;
  readGameConfig(gamePath, &gameConfig, errorMessage);
  if (gameConfig.templateName == "monster") {
    initGames_monsterCatcher(gamePath, &gameConfig, errorMessage);
  } else if (gameConfig.templateName == "race") {
    initGames_racing(gamePath, &gameConfig, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    initGames_lua(gamePath, &gameConfig, errorMessage);
  } else {
    errorMessage->concat("Invalid game template ");
    errorMessage->concat(gameConfig.templateName);
  }
}

String getRandomWinScreenPathForCurrentGame(String* errorMessage) {
  return getRandomWinScreenPath(currentGamePath, errorMessage);
}

void drawShortBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawShortBlowGame_monsterCatcher(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawShortBlowGame_racing(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawShortBlowGame_lua(display, blowData, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
  }
}

void drawEqualBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawEqualBlowGame_monsterCatcher(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawEqualBlowGame_racing(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawEqualBlowGame_lua(display, blowData, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
  }
}

void drawLongBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawLongBlowGame_monsterCatcher(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawLongBlowGame_racing(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawLongBlowGame_lua(display, blowData, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
  }
}

void drawTrampolineGame(DISPLAY_T* display, JumpData* jumpData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawTrampolineGame_monsterCatcher(display, jumpData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawTrampolineGame_racing(display, jumpData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawTrampolineGame_lua(display, jumpData, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
  }
}

void drawInhalationGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawInhalationGame_monsterCatcher(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawInhalationGame_racing(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawInhalationGame_lua(display, blowData, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
  }
}

bool displayProgressionMenu(DISPLAY_T *display, String *errorMessage) {
  if (gameConfig.templateName == "monster") {
    return displayProgressionMenu_monsterCatcher(display, errorMessage);
  } else if (gameConfig.templateName == "race") {
    return displayProgressionMenu_racing(display, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    return displayProgressionMenu_lua(display, errorMessage);
  } else {
    errorMessage->concat("No game loaded!");
    return false;
  }
}

static String leftPadString(String str, int len) {
  for (uint32_t i = str.length(); i < len; i++) {
    str = " " + str;
  }
  return str;
}

bool displayExecutionList(DISPLAY_T *display, String *executionLog, String *errorMessage) {
  static uint32_t skipLines = 0xFFFFFFFF;
  uint32_t lineStart = 0;
  uint32_t totalLineCount = 0;
  uint32_t yPos = 40;

  while (lineStart < executionLog->length()) {
    uint32_t lineEnd = executionLog->indexOf('\n', lineStart);
    if (lineEnd == -1) {
      lineEnd = executionLog->length();
    }
    lineStart = lineEnd + 1;
    totalLineCount++;
  }

  if (skipLines > totalLineCount) {
    if (totalLineCount > EXECUTION_LOG_MAX_LINES) {
      skipLines = totalLineCount - EXECUTION_LOG_MAX_LINES;
    } else {
      skipLines = 0;
    }
  }

  lineStart = 0;
  uint32_t lineCount = 0;
  String lastDate = "";
  while (lineStart < executionLog->length()) {
    uint32_t lineEnd = executionLog->indexOf('\n', lineStart);
    if (lineEnd == -1) {
      lineEnd = executionLog->length();
    }
    lineCount++;
    if (lineCount < skipLines) {
      lineStart = lineEnd + 1;
      continue;
    }
    String line = executionLog->substring(lineStart, lineEnd);
    uint32_t sepIndex = line.indexOf(';');
    String profile = line.substring(0, sepIndex);
    sepIndex = line.indexOf(';', sepIndex + 1);
    String date = line.substring(profile.length() + 1, sepIndex);
    if (date.equals(lastDate)) {
      date = "";
    } else {
      if (yPos > 40) {
        spr.drawFastHLine(0, yPos-1, 320, 0x528a);
      }      
      lastDate = date;
    }
    String time = line.substring(sepIndex + 1, line.length());
    time = leftPadString(time, 5);
    spr.drawString(date, 0, yPos);
    spr.drawString(time, 80, yPos);
    spr.drawString(profile, 150, yPos);
    yPos += 8;
    lineStart = lineEnd + 1;
  }
  drawBmp(&spr, "/gfx/arrow_up.bmp", 280, 40, 0xf81f, false);
  drawBmp(&spr, "/gfx/arrow_down.bmp", 280, 208, 0xf81f, false);
  if (skipLines > 0 && isTouchInZone(280, 40, 32, 32)) {
    skipLines--;
  } else if (skipLines < totalLineCount - EXECUTION_LOG_MAX_LINES && isTouchInZone(280, 208, 32, 32)) {
    skipLines++;
  }
  uint32_t scrollBarHeight = (200*EXECUTION_LOG_MAX_LINES)/totalLineCount;
  uint32_t scrollBarY = (200*skipLines)/totalLineCount;
  spr.fillRect(315, scrollBarY+40, 5, scrollBarHeight, TFT_WHITE);
  return true;
}

void endGame(String* errorMessage) {
  if (gameConfig.templateName == "lua") {
    endGame_lua(errorMessage);
  }
}
