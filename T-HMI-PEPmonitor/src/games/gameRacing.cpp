#include "gameRacing.h"
#include "gameLua.h"

#define TURN_STRAIGHT 0
#define TURN_LEFT 1
#define TURN_RIGHT 2
#define TURN_HARD_LEFT 3
#define TURN_HARD_RIGHT 4

#define HORIZON_Y 60
#define BASELINE_Y 190
#define ROAD_WIDTH 260.0f
#define SPEED 0.03f
#define SPEED_NITRO 0.045f

static uint8_t nitro;
static String racerGamePath;
static String racerGameIniPath;

static TFT_eSprite playerCarSprite[] =    {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft),
                                           TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};
static TFT_eSprite pitstopCarSprite[] =   {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};
static TFT_eSprite pitstopWheelSprite =    TFT_eSprite(&tft);
static TFT_eSprite enemyCarSprite[][4] = {{TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)},
                                          {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)},
                                          {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)},
                                          {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)}};
static TFT_eSprite nitroLSprite =          TFT_eSprite(&tft);
static TFT_eSprite nitroSSprite =          TFT_eSprite(&tft);
static TFT_eSprite nitroEffectSprite[] =  {TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft), TFT_eSprite(&tft)};

void initGames_racing(String gamePath, GameConfig* gameConfig, String* errorMessage) {
  Serial.print("Game path: ");
  Serial.println(gamePath);
  racerGamePath = gamePath;
  racerGameIniPath = gamePath + "gameconfig.ini";
  setGamePrefsNamespace(gameConfig->prefsNamespace.c_str());
  nitro = prefs.getUInt("nitro", 0);
  for (int8_t i=0; i<4; i++) {
    loadBmp(&pitstopCarSprite[i], racerGamePath + "gfx/car"+i+"_top.bmp", FLIPPED_H);
    loadBmp(&pitstopWheelSprite, racerGamePath + "gfx/wheel.bmp");
    loadBmp(&enemyCarSprite[i][0], racerGamePath + "gfx/car"+i+"_0_0.bmp");
    loadBmp(&enemyCarSprite[i][1], racerGamePath + "gfx/car"+i+"_0.75.bmp");
    loadBmp(&enemyCarSprite[i][2], racerGamePath + "gfx/car"+i+"_0.5.bmp");
    loadBmp(&enemyCarSprite[i][3], racerGamePath + "gfx/car"+i+"_0.25.bmp");
  }
  TFT_eSprite* nitroSpriteRefs[] = { // Reload player anim at the beginning of the round
    &nitroEffectSprite[0],
    &nitroEffectSprite[1],
    &nitroEffectSprite[2],
    &nitroEffectSprite[3],
    &nitroEffectSprite[4],
  };
  loadBmpAnim(nitroSpriteRefs, racerGamePath + "gfx/nitroEffect.bmp", 5);
  loadBmp(&nitroLSprite, racerGamePath + "gfx/nitroL.bmp");
  loadBmp(&nitroSSprite, racerGamePath + "gfx/nitroS.bmp");
  loadBmp(&playerCarSprite[0], racerGamePath + "gfx/car0_0_0.bmp");
  loadBmp(&playerCarSprite[1], racerGamePath + "gfx/car0_0_1.bmp");
  loadBmp(&playerCarSprite[2], racerGamePath + "gfx/car0_1_0.bmp");
  loadBmp(&playerCarSprite[3], racerGamePath + "gfx/car0_1_1.bmp");
  loadBmp(&playerCarSprite[4], racerGamePath + "gfx/car0_1_0.bmp", FLIPPED_H);
  loadBmp(&playerCarSprite[5], racerGamePath + "gfx/car0_1_1.bmp", FLIPPED_H);
  loadBmp(&playerCarSprite[6], racerGamePath + "gfx/car0_2_0.bmp");
  loadBmp(&playerCarSprite[7], racerGamePath + "gfx/car0_2_1.bmp");
  loadBmp(&playerCarSprite[8], racerGamePath + "gfx/car0_2_0.bmp", FLIPPED_H);
  loadBmp(&playerCarSprite[9], racerGamePath + "gfx/car0_2_1.bmp", FLIPPED_H);
}

static uint8_t currentTurnType;
static int32_t roadXOffset = 0;
static uint8_t enemySpriteId = 0;
static void calculateRoad(float y, float speed, unsigned long ms, float* x, float* w, int32_t* roadYOffset) {
  static float roadBaseOffset = 0;
  static uint32_t roadLastMs = 0;
  if (roadLastMs==0) {
    roadLastMs = ms;
  }
  *w = ROAD_WIDTH*(float)(y-HORIZON_Y)/(BASELINE_Y-HORIZON_Y);
  *x = 160.0 - *w*0.5;
  float yInv = BASELINE_Y+HORIZON_Y-y;
  *x += ((float)roadXOffset)*(yInv*yInv)*0.0001;
  roadBaseOffset += (float)(ms-roadLastMs)*speed;
  roadLastMs = ms;
  float wP = 1.0-*w/(float)ROAD_WIDTH;
  *roadYOffset = (wP*wP*wP * 25.0 + roadBaseOffset);
}

static void drawEnemyCar(DISPLAY_T* display, int32_t x, int32_t y, int32_t w, int32_t enemyY) {
  // Draw enemy car
  if (y == enemyY) {
    uint8_t enemySpriteLevel;
    if (y>150) {
      enemySpriteLevel = 0;
    } else if (y>100) {
      enemySpriteLevel = 1;
    } else if (y>50) {
      enemySpriteLevel = 2;
    } else {
      enemySpriteLevel = 3;
    }
    TFT_eSprite* sprite = &enemyCarSprite[enemySpriteId][enemySpriteLevel];
    if (currentTurnType == TURN_RIGHT || currentTurnType == TURN_HARD_RIGHT) {
      sprite->pushToSprite(display, x + w - w/7 - sprite->width()/2, y - sprite->height(), 0);
    } else {
      sprite->pushToSprite(display, x + w/7 - sprite->width()/2, y - sprite->height(), 0);
    }
  }
}

static void drawRace_desert(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, float lastX, float lastW) {
  float railTopHeight = 0.15*w;
  float railThickness = 0.08*w;
  float lampHeight = 0.8*w;
  int32_t dxTotal = constrain((lastX-lastW/5) - (x-w/5), 0, 5);
  display->drawFastHLine(0, y, x-w*0.1, (roadYOffset/6) % 2 ? 0x94a0 : 0xce80);
  display->drawFastHLine(_max(0,x+w*1.1), y, 320, (roadYOffset/6) % 2 ? 0x94a0 : 0xce80);
  for (int8_t dx = 0; dx <= dxTotal; dx++) {
    display->drawFastVLine(x-w*0.2 + dx, y-railTopHeight, (roadYOffset % 12) == 6 ? railTopHeight : railThickness, (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
  }
  int32_t dxTotalR = constrain((lastX+lastW+lastW/5) - (x+w+w/5), -5, 0);
  for (int8_t dx = dxTotalR; dx <= 0; dx++) {
    display->drawFastVLine(x+w*1.2 + dx, y-railTopHeight, (roadYOffset % 12) == 6 ? railTopHeight : railThickness, (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
  }

  // Draw lamp mast
  if (((roadYOffset) % 12) == 1) {
    for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
      display->drawFastVLine(x-w*0.2 + dx, y-lampHeight, lampHeight, (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
    }
    for (uint32_t i=0;i<(w*0.02);i++) {
      display->drawFastHLine(x-w*0.2, y-lampHeight+i, w*(0.2+0.5+0.125), (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
    }
  }

  // Draw lamp
  if (((roadYOffset) % 12) == 1) {
    for (uint32_t i=1;i<(w*0.05)+1;i++) {
      display->drawFastHLine(x + w*(0.5 - 0.125), y-lampHeight+i, w/4, 0xffff);
    }
  }
}

void drawRace_tunnel(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, int32_t lastX, int32_t lastW, bool drawMountain) {
  float wallHeight = w;

  // Draw roof
  display->drawFastHLine(x-w*0.2, y-wallHeight, w*1.4, (roadYOffset/6) % 2 ? 0x5AEB : 0x8410);

  if (drawMountain) {
    for (int32_t mY = 0; mY<y-wallHeight; mY++) {
      display->drawFastHLine(0, mY, 320, 0x7bef);
    }
    for (int32_t mY = y-wallHeight; mY<y; mY++) {
      display->drawFastHLine(0, mY, x-w*0.2, 0x7bef);
      display->drawFastHLine(x+w*1.2, mY, 320, 0x7bef);
    }
  }

  // Draw wall
  int32_t dxTotal = constrain((lastX-lastW/5) - (x-w/5), -5, 5);
  if (dxTotal>=0) { // Only draw left wall if visible
    for (int32_t dx = 0; dx <= dxTotal; dx++) {
      display->drawFastVLine(x-w*0.2 + dx, y-wallHeight, wallHeight, (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
    }
  }
  dxTotal = constrain((lastX+lastW+lastW/5) - (x+w+w/5), -5, 5);
  if (dxTotal<=0) { // Only draw right wall if visible
    for (int32_t dx = dxTotal; dx <= 0; dx++) {
      display->drawFastVLine(x+w*1.2 + dx, y-wallHeight, wallHeight, (roadYOffset/6) % 2 ? 0xbdf7 : 0xce59);
    }

    // Draw emergency escape sign
    if ((roadYOffset % 40) == 0) {
      for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
        display->drawFastVLine(x+w*1.2 + dx, y-w*0.5, w/10, 0x0540);
        display->drawPixel(x+w*1.2 + dx, y-w*0.5, 0x0000);
        display->drawPixel(x+w*1.2 + dx, y-w*0.4, 0x0000);
      }
    }

    // Draw escape door
    if ((roadYOffset % 40) > 1 && (roadYOffset % 40) < 4) {
      for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
        display->drawFastVLine(x+w*1.2 + dx, y-w*0.5, w*0.5, 0x3166);
      }
    }
  }

  // Draw lamp light
  /*if (((roadYOffset/2) % 6) <= 2) {
    display->drawFastHLine(x + w*(0.5 - 0.25 - 0.03125), y-w+2, w*(0.5 + 0.0625), 0xffed);
  }*/

  //Draw lamp
  if (((roadYOffset/2) % 6) == 1) {
    for (uint32_t i=1;i<(w*0.05)+1;i++) {
      display->drawFastHLine(x + w*(0.5 - 0.25), y-wallHeight+i, w*0.5, 0xffff);
    }
  }
}

static void drawRace(DISPLAY_T* display, BlowData* blowData, int32_t animTime) {
  int32_t enemyY = -1;
  int32_t enemy2Y = -1;
  float w = 0;
  float x = 160.0;
  int32_t roadYOffset;
  float lastX, lastW;
  float speed = SPEED;
  if (animTime <= 2000 && (blowData->lastBlowStatus & LAST_BLOW_FAILED)) {
    enemyY = 50 + (150*(2000-animTime) / 2000);
  } else if (animTime <= 2000) {
    enemyY = animTime / 10;
    if (nitro>0) {
      enemy2Y = enemyY + 40;
      speed = SPEED_NITRO;
    }
  }
  switch (currentTurnType) {
    case TURN_LEFT:
      roadXOffset -= roadXOffset>-25 ? 1 : 0;
      break;
    case TURN_RIGHT:
      roadXOffset += roadXOffset<25 ? 1 : 0;
      break;
    case TURN_HARD_LEFT:
      roadXOffset -= roadXOffset>-45 ? 2 : 0;
      break;
    case TURN_HARD_RIGHT:
      roadXOffset += roadXOffset<45 ? 2 : 0;
      break;
  }
  for (int32_t y=0;y<HORIZON_Y;y++) {
    display->drawFastHLine(0, y, 320, 0x31b4);
  }
  for (int32_t y=HORIZON_Y;y<190;y++) {
    lastX = x;
    lastW = w;
    calculateRoad(y, speed, blowData->ms, &x, &w, &roadYOffset);
    if (y==50) {
      lastX = x;
      lastW = w;
    }
    // Draw road surface
    display->drawFastHLine(x, y, w, ((roadYOffset/6) % 2 ? 0x5AEB : 0x8410));
    // Draw embankment
    display->drawFastHLine(x-w*0.1, y, w*0.1, (roadYOffset/3) % 2 ? 0xf800: 0xffff);
    display->drawFastHLine(x+w, y, w*0.1, (roadYOffset/3) % 2 ? 0xf800: 0xffff);
    // Draw center line
    if ((roadYOffset/4) % 2) {
      display->drawFastHLine(x+w*0.5-w*0.05, y, w*0.1, 0xffff);
    }

    if (roadYOffset % 1000 < 500) {
      drawRace_tunnel(display, x, y, w, roadYOffset, lastX, lastW, (roadYOffset % 1000) == 0);
    } else {
      drawRace_desert(display, x, y, w, roadYOffset, lastX, lastW);
    }

    // Draw enemy car
    drawEnemyCar(display, x, y, w, enemyY);
    drawEnemyCar(display, x, y, w, enemy2Y);
  }
  uint8_t playerTurnType = TURN_STRAIGHT;
  int8_t nitroEffectXOffset = 0;
  if (roadXOffset<-35) {
    playerTurnType = TURN_HARD_LEFT;
    nitroEffectXOffset = 10;
  } else if (roadXOffset<-5) {
    playerTurnType = TURN_LEFT;
    nitroEffectXOffset = 5;
  } else if (roadXOffset>5) {
    playerTurnType = TURN_RIGHT;
    nitroEffectXOffset = -5;
  } else if (roadXOffset>35) {
    playerTurnType = TURN_HARD_RIGHT;
    nitroEffectXOffset = -10;
  }
  uint32_t playerCarSpriteId = playerTurnType*2 + ((blowData->ms/200) % 2);
  playerCarSprite[playerCarSpriteId].pushToSprite(display, 160-playerCarSprite[playerCarSpriteId].width()/2, 187-playerCarSprite[playerCarSpriteId].height(), 0x0000);
  if (nitro>0 && animTime <= 2000 && !(blowData->lastBlowStatus & LAST_BLOW_FAILED)) {
    uint8_t animFrame = _max(0,_min(5,(2000-animTime)/400));
    nitroEffectSprite[animFrame].pushToSprite(display, 125 + nitroEffectXOffset, 170 + (animFrame <= 2 ? 6 : 0), 0x0000);
  }
}

void drawShortBlowGame_racing(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  int32_t animTime = ((int32_t)blowData->ms) - ((int32_t)blowData->blowEndMs);
  for (uint8_t i = 0; i<=3; i++) {
    display->fillRect(i*80, 30, 80, 145, i%2 ? 0x5AEB : 0x8410);
  }
  for (uint8_t i = 0; i<7; i++) {
    display->fillRect(i*55, 95, 35, 10, 0xFFFF);
  }
  for (uint8_t i = 0; i<8; i++) {
    display->fillRect(i*40, 20, 40, 10, i%2 ? 0xF800 : 0xFFFF);
    display->fillRect(i*40, 165, 40, 10, i%2 ? 0xF800 : 0xFFFF);
  }
  if (animTime <= 2000 && !(blowData->lastBlowStatus & LAST_BLOW_FAILED)) {
    if (blowData->blowCount==1) {
      pitstopWheelSprite.pushToSprite(display, 15, 40 + animTime/40, 0x0000);
    }
    if (blowData->blowCount==2) {
      pitstopWheelSprite.pushToSprite(display, 85, 40 + animTime/40, 0x0000);
    }
    if (blowData->blowCount==3) {
      pitstopWheelSprite.pushToSprite(display, 15, 140 - animTime/40, 0x0000);
    }
    if (blowData->blowCount==4) {
      pitstopWheelSprite.pushToSprite(display, 85, 140 - animTime/40, 0x0000);
    }
    if (blowData->blowCount==5) {
      pitstopCarSprite[0].pushToSprite(display, animTime*animTime/5000, 70, 0x0000);
    }
  }
  if (blowData->blowCount<1) {
    pitstopWheelSprite.pushToSprite(display, 15, 40, 0x0000);
  }
  if (blowData->blowCount<2) {
    pitstopWheelSprite.pushToSprite(display, 85, 40, 0x0000);
  }
  if (blowData->blowCount<3) {
    pitstopWheelSprite.pushToSprite(display, 15, 140, 0x0000);
  }
  if (blowData->blowCount<4) {
    pitstopWheelSprite.pushToSprite(display, 85, 140, 0x0000);
  }
  if (blowData->blowCount<5) {
    pitstopCarSprite[0].pushToSprite(display, 0, 70, 0x0000);
  }
}

int8_t lastCheckedBlowCount = -1;
int8_t currentPosition = 31;
bool savedBlows=false;
unsigned long lastCheckedBlowEndMs = 0;
void drawLongBlowGame_racing(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  int32_t animTime = ((int32_t)blowData->ms) - ((int32_t)blowData->blowEndMs);
  if (blowData->ms%10000L<35) { // TODO remove debug code
    currentTurnType = random(1,5);
  }
  if (lastCheckedBlowCount==-1 || (blowData->blowCount>lastCheckedBlowCount && animTime>2000)) {
    currentTurnType = random(1,5);
    enemySpriteId = random(0,4);
    lastCheckedBlowCount = blowData->blowCount;
  }
  if (lastCheckedBlowEndMs < blowData->blowEndMs) {
    if (blowData -> lastBlowStatus == LAST_BLOW_FAILED) {
      currentPosition++;
    } else {
      currentPosition--;
      if (nitro>0) {
        nitro--;
        currentPosition--;
      }
    }
    lastCheckedBlowEndMs = blowData->blowEndMs;
  }

  
  drawRace(display, blowData, animTime);
  display->setTextSize(4);
  display->setCursor(260, 200);
  if (currentPosition<10) {
    display->print(" ");
  }
  display->print(currentPosition);

  if (nitro > 0) {
    display->setCursor(247, 12);
    display->setTextSize(3);
    printShaded(display, (nitro<10 ? " " : "") + String(nitro) + "x");
    nitroSSprite.pushToSprite(display, 300, 5, 0x0000);
  }

  if (blowData->cycleNumber==blowData->totalCycleNumber-1 && blowData->blowCount==blowData->totalLongBlowRepetitions+blowData->totalEqualBlowRepetitions-1 && !savedBlows) {
    savedBlows = true;
    prefs.putUInt("nitro", nitro);
    Serial.print("Saved ");
    Serial.print(nitro);
    Serial.println(" nitro.");
  }
}

void drawEqualBlowGame_racing(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  drawLongBlowGame_racing(display, blowData, errorMessage);
}

void drawTrampolineGame_racing(DISPLAY_T* display, JumpData* jumpData, String* errorMessage) {
  static bool saved=false;
  uint8_t earnedNitro = (jumpData->jumpCount / 100);
  int32_t bottleSpacingX = (nitroLSprite.width()+5);
  for (uint8_t currentBottle=0; currentBottle<earnedNitro; currentBottle++) {
    nitroLSprite.pushToSprite(display, 50 + bottleSpacingX*currentBottle, 70, 0x0000);
  }
  nitroLSprite.pushToSprite(display, 50 + bottleSpacingX*(earnedNitro), 70, 0x0000);
  display->fillRect(50+bottleSpacingX*(earnedNitro), 70, nitroLSprite.width(), nitroLSprite.height()*(100-(jumpData->jumpCount%100))/100, 0x0000);
  if (!saved && jumpData->msLeft<0) {
    saved = true;
    nitro += earnedNitro;
    prefs.putUInt("nitro", nitro);
    Serial.print("Saved ");
    Serial.print(nitro);
    Serial.println(" nitro.");
  } 
}

void drawInhalationGame_racing(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  // Not implemented
}

void drawInhalationBlowGame_racing(DISPLAY_T* display, BlowData* blowData, String* errorMessage) {
  // Not implemented
}

bool displayProgressionMenu_racing(DISPLAY_T *display, String *errorMessage) {
  initLua();
  luaProgressionMenuRunning = true;
  luaDisplay = display;
  String error = lua_dofile(racerGamePath + "progressionMenu.lua");
  if (!error.isEmpty()) {
    errorMessage->concat(error);
    return false;
  }
  return luaProgressionMenuRunning;
}