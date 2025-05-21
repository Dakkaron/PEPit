#ifndef __GFXHANDLER_H__
#define __GFXHANDLER_H__
#include <Arduino.h>
#include <TFT_eSPI.h>

#define DISPLAY_T TFT_eSprite

extern TFT_eSPI tft;
extern TFT_eSprite spr;

#define FLIPPED_H 0x01
#define FLIPPED_V 0x02

void initGfxHandler();
bool getBmpDimensions(String filename, int16_t* w, int16_t* h);
bool drawBmp(String filename, int16_t x, int16_t y, bool debugLog = true);
bool drawBmpSlice(String filename, int16_t x, int16_t y, int16_t maxH, bool debugLog = true);
bool drawBmp(DISPLAY_T* sprite, String filename, int16_t x, int16_t y, bool debugLog = true);
bool loadBmp(DISPLAY_T* display, String filename);
bool loadBmp(DISPLAY_T* display, String filename, uint8_t flipped);
bool loadBmp(DISPLAY_T* display, String filename, uint8_t flipped, uint16_t maskingColor);
bool loadBmpAnim(DISPLAY_T** display, String filename, uint8_t animFrames);
bool loadBmpAnim(DISPLAY_T** display, String filename, uint8_t animFrames, uint8_t flipped);
bool loadBmpAnim(DISPLAY_T** displays, String filename, uint8_t animFrames, uint8_t flipped, uint16_t maskingColor);
void drawProgressBar(DISPLAY_T* display, uint16_t progress, uint16_t greenOffset, int16_t x, int16_t y, int16_t w, int16_t h);
void drawProgressBar(DISPLAY_T* display, uint16_t val, uint16_t maxVal, uint16_t greenOffset, int16_t x, int16_t y, int16_t w, int16_t h);
void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength, uint16_t textColor, uint16_t shadeColor);
void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength, uint16_t textColor);
void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength);
void printShaded(DISPLAY_T* display, String text);
void drawSystemStats(uint32_t ms, uint32_t lastMs);
void drawImageButton(DISPLAY_T* display, String path, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
void drawButton(DISPLAY_T* display, String text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
bool checkButton(int16_t x, int16_t y, int16_t w, int16_t h);
bool drawAndCheckImageButton(DISPLAY_T* display, String path, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
bool drawAndCheckButton(DISPLAY_T* display, String text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);
int16_t displayProfileSelection(DISPLAY_T* display, uint16_t nr, String* errorMessage);
int16_t displayGameSelection(DISPLAY_T* display, uint16_t nr, uint32_t requiredTaskTypes, String* errorMessage);
void drawKeyboard(DISPLAY_T* display, uint16_t keyColor, uint16_t textColor);
void checkKeyboard(DISPLAY_T* display, String* output, uint32_t maxCharacters, uint16_t keyColor, uint16_t textColor);
void checkFailWithMessage(String message);
void checkSoftFailWithMessage(String message, uint8_t textSize = 2);
void displayFullscreenMessage(String message, uint8_t textSize = 2);

#endif /*__GFXHANDLER_H__*/