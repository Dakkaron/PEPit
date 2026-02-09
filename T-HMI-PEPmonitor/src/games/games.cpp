#include "games.h"
#include "gameMonsterCatcher.h"
#include "gameRacing.h"
#include "gameLua.h"
#include "systemStateHandler.h"

#define EXECUTION_LOG_MAX_LINES 24

static String currentGamePath;
static GameConfig gameConfig;
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

void drawInhalationBlowGame(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  if (gameConfig.templateName == "monster") {
    drawInhalationBlowGame_monsterCatcher(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "race") {
    drawInhalationBlowGame_racing(display, blowData, errorMessage);
  } else if (gameConfig.templateName == "lua") {
    drawInhalationBlowGame_lua(display, blowData, errorMessage);
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

static String leftPadString(String str, int len, String chr = " ") {
  for (uint32_t i = str.length(); i < len; i++) {
    str = chr + str;
  }
  return str;
}

String getCsvToken(String* input, uint32_t index) {
  int32_t startIndex = 0;
  int32_t endIndex = input->indexOf(';');
  for (int32_t i = 0; i<index; i++) {
    if (endIndex == -1) {
      return "";
    }
    startIndex = endIndex + 1;
    endIndex = input->indexOf(';', endIndex + 1);
  }
  if (endIndex == -1) {
    return input->substring(startIndex, input->length());
  } else {
    return input->substring(startIndex, endIndex);
  }
}

bool displayExecutionList(DISPLAY_T *display, String *executionLog, String *errorMessage) {
  static uint32_t skipLines = 0xFFFFFFFF;
  uint32_t lineStart = 0;
  uint32_t totalLineCount = 0;
  uint32_t yPos = 40;

  if (executionLog->length()==0 || executionLog->indexOf('\n')==-1) {
    spr.setTextSize(2);
    spr.drawString("Keine gespeicherten Ausführungen.", 0, 100);
    spr.fillRect(240, 200, 100, 40, 0x001F);
    spr.drawString("Zurück", 250, 215);
    spr.setTextSize(1);
    if (isTouchInZone(240, 200, 100, 40)) {
      ESP.restart();
    }
    return true;
  }
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

  spr.drawString("Datum", 0, 32);
  spr.drawString("Zeit", 58, 32);
  spr.drawString("Profil", 90, 32);
  spr.drawString("OK", 160, 32);
  spr.drawString("NOK", 180, 32);
  spr.drawString("Dauer", 210, 32);
  spr.drawFastHLine(0, 39, 320, 0xffff);

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
    if (!line.startsWith("profileName;executionDate;executionTime")) {
      String profile = getCsvToken(&line, 0);
      String date = getCsvToken(&line, 1);
      if (date.equals(lastDate)) {
        date = "";
      } else {
        if (yPos > 40) {
          spr.drawFastHLine(0, yPos-1, 320, 0x528a);
        }      
        lastDate = date;
      }
      String time = getCsvToken(&line, 2);
      time = leftPadString(time, 5);
      String successes = getCsvToken(&line, 3);
      String fails = getCsvToken(&line, 4);
      int32_t durationSeconds = getCsvToken(&line, 5).toInt() / 1000;
      String duration = leftPadString(String(durationSeconds/60), 2, "0") + ":" + leftPadString(String(durationSeconds%60), 2, "0");
      spr.drawString(date, 0, yPos);
      spr.drawString(time, 58, yPos);
      spr.drawString(profile, 90, yPos);
      spr.drawString(successes, 160, yPos);
      spr.drawString(fails, 180, yPos);
      spr.drawString(duration, 210, yPos);
    }
    yPos += 8;
    lineStart = lineEnd + 1;
  }
  drawBmp(&spr, "/gfx/arrow_up.bmp", 280, 40, 0xf81f, false);
  drawBmp(&spr, "/gfx/arrow_down.bmp", 280, 158, 0xf81f, false);
  if (skipLines > 0 && isTouchInZone(280, 40, 32, 32)) {
    skipLines--;
  } else if (skipLines < totalLineCount - EXECUTION_LOG_MAX_LINES && isTouchInZone(280, 158, 32, 32)) {
    skipLines++;
  }
  uint32_t scrollBarHeight = (160*EXECUTION_LOG_MAX_LINES)/totalLineCount;
  uint32_t scrollBarY = (160*skipLines)/totalLineCount;
  spr.fillRect(315, scrollBarY+40, 5, scrollBarHeight, TFT_WHITE);

  spr.setTextSize(2);
  spr.fillRect(240, 200, 100, 40, 0x001F);
    spr.drawString("Zurück", 250, 215);
    spr.setTextSize(1);
  if (isTouchInZone(240, 200, 100, 40)) {
    ESP.restart();
  }
  return true;
}

void endGame(String* errorMessage) {
  setSystemState(STATE_GAME_ENDING);
  if (gameConfig.templateName == "lua") {
    endGame_lua(errorMessage);
  }
}


bool displayWinScreen(DISPLAY_T *display, String *errorMessage) {
  setSystemState(STATE_WIN_SCREEN);
  if (gameConfig.templateName == "lua") {
    return displayWinScreen_lua(display, errorMessage);
  }
  return false;
}