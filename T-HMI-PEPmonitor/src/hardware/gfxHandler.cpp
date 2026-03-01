#include "gfxHandler.hpp"
#include <SD_MMC.h>
#include <FS.h>
#include <SPI.h>
#include <SD.h>
#include <algorithm>
#include "hardware/touchHandler.h"
#include "hardware/sdHandler.h"
#include "hardware/serialHandler.h"
#include "hardware/powerHandler.h"
#include "hardware/MyFont.h"
#include "updateHandler.h"
#include "systemStateHandler.h"
#include "hardware/wifiHandler.h"

#define GFXFF 1
#define MYFONT5x7 &Font5x7Fixed

#define BMP16_ALPHA_FLAG_OFFSET 0x43

#define swap(x, y) do \
  { unsigned char swap_temp[sizeof(x) == sizeof(y) ? (signed)sizeof(x) : -1]; \
    memcpy(swap_temp, &y, sizeof(x)); \
    memcpy(&y, &x, sizeof(x)); \
    memcpy(&x, swap_temp, sizeof(x)); \
  } while(0)

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite spr = TFT_eSprite(&tft);
#define SYSTEM_STATS_SPRITE_COUNT 5
static TFT_eSprite* systemStatsSprites = nullptr;

void initGfxHandler() {
  tft.writecommand(0x11);
  tft.fillScreen(TFT_BLACK);
  spr.setColorDepth(16);
  spr.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT, 2);
  spr.frameBuffer(0);
  spr.fillSprite(TFT_BLACK);
  spr.frameBuffer(1);
  spr.fillSprite(TFT_BLACK);
  spr.setTextColor(TFT_WHITE);

  tft.setFreeFont(MYFONT5x7);
  spr.setFreeFont(MYFONT5x7);
}

static uint16_t read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read(); // MSB
  return result;
}


static uint32_t read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read(); // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read(); // MSB
  return result;
}

static void parseBitmapLine(File* bmpFS, uint8_t* lineBuffer, uint16_t bytesPerPixel, uint16_t w, bool hasAlpha, uint16_t padding, uint16_t maskingColor, bool enableDitherTransparency, bool oddRow) {
  uint8_t r, g, b, a;
  bmpFS->read(lineBuffer, w * bytesPerPixel);
  uint8_t*  bptr = lineBuffer;
  uint16_t* tptr = (uint16_t*)lineBuffer;
  // Convert 24 to 16 bit colours
  for (uint16_t col = 0; col < w; col++) {
    if (bytesPerPixel == 4) {
      b = *bptr++;
      g = *bptr++;
      r = *bptr++;
      a = *bptr++;
      if (a == 0 || (enableDitherTransparency && (col & 0x01) == oddRow)) {
        *tptr++ = maskingColor;
      } else {
        uint16_t res = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        if (res == maskingColor) {
          res = (res & 0xFFDF) | (~res & 0x0020); // flip lowest green bit
        }
        *tptr++ = res;
      }
    } else if (bytesPerPixel == 3) {
      if (enableDitherTransparency && (col & 0x01) == oddRow) {
        *tptr++ = maskingColor;
      } else {
        b = *bptr++;
        g = *bptr++;
        r = *bptr++;
        *tptr++ = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      }
    } else if (bytesPerPixel == 2) {
      if (hasAlpha) {
        uint16_t color = (*bptr++);
        color |= (*bptr++) << 8;
        a = (color & 0x8000) >> 15;
        if (a == 0 || (enableDitherTransparency && (col & 0x01) == oddRow)) {
          *tptr++ = maskingColor;
        } else {
          r = (color & 0xFC00) >> 10;
          g = (color & 0x03E0) >> 5;
          b = (color & 0x001F);
          if (color == maskingColor) {
            g = (g & 0xFFFE) | (~g & 0x1); // flip lowest green bit
          }
          *tptr++ = (r << 11) | (g << 6) | b;
        }
      } else {
        if (enableDitherTransparency && ((col & 0x01) == oddRow)) {
          bptr++;
          bptr++;
          *tptr++ = maskingColor;
        } else {
          uint16_t color = (*bptr++);
          color |= (*bptr++) << 8;
          *tptr++ = color;
        }
      }
    }
  }
  // Read any line padding
  if (padding) {
    bmpFS->read((uint8_t*)tptr, padding);
  }
}

bool getBmpDimensions(String filename, int16_t* w, int16_t* h) {
  File bmpFS;
  bmpFS = SD_MMC.open(filename);

  if (!bmpFS)
  {
    Serial.print("File not found: ");
    Serial.println(filename);
    Serial.println("#");
    return false;
  }
  uint16_t headerBytes = read16(bmpFS);
  if (headerBytes == 0x4D42) {
    read32(bmpFS);
    read32(bmpFS);
    read32(bmpFS);
    read32(bmpFS);
    *w = read32(bmpFS);
    *h = read32(bmpFS);
  } else {
    Serial.print("Wrong file format: ");
    Serial.println(headerBytes);
    bmpFS.close();
    return false;
  }
  bmpFS.close();
  return true;
}

bool loadBmp(DISPLAY_T* display, String filename) {
  return loadBmp(display, filename, 0);
}

bool loadBmp(DISPLAY_T* display, String filename, uint8_t options) {
  return loadBmpAnim(&display, filename, 1, options);
}

bool loadBmp(DISPLAY_T* display, String filename, uint8_t options, uint16_t maskingColor) {
  return loadBmpAnim(&display, filename, 1, options, maskingColor);
}

bool loadBmpAnim(DISPLAY_T** display, String filename, uint8_t animFrames) {
  return loadBmpAnim(display, filename, animFrames, 0);
}

bool loadBmpAnim(DISPLAY_T** displays, String filename, uint8_t animFrames, uint8_t options) {
  return loadBmpAnim(displays, filename, animFrames, options, TFT_BLACK);
}

/*
 * animFrames -> number of frames to export, 1 == no animation, still image
 */
bool loadBmpAnim(DISPLAY_T** displays, String filename, uint8_t animFrames, uint8_t options, uint16_t maskingColor) {
  Serial.print("File: ");
  Serial.println(filename);
  Serial.print("Options: ");
  Serial.println(options);
  Serial.println("#");

  File bmpFS;

  // Open requested file on SD card
  bmpFS = SD_MMC.open(filename);

  if (!bmpFS) {
    Serial.print("File not found: ");
    Serial.print(filename);
    Serial.println(", retrying.");
    bmpFS = SD_MMC.open(filename);
    if (!bmpFS) {
      Serial.print("Retry failed, file not found: ");
      Serial.println(filename);
      return false;
    }
  }

  uint32_t seekOffset;
  uint16_t w, h, row, frameNr, frameH;

  uint32_t startTime = millis();

  uint16_t headerBytes = read16(bmpFS);
  if (headerBytes == 0x4D42) {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);
    frameH = h / animFrames;

    uint16_t colorPanes = read16(bmpFS);
    uint16_t bitDepth = read16(bmpFS);
    uint16_t compression = read16(bmpFS);

    if ((colorPanes == 1) && (((bitDepth == 16) && (compression == 3)) || ((bitDepth == 24) && (compression == 0)) || ((bitDepth == 32) && (compression == 3)))) { // BITMAPINFOHEADER
      uint8_t bytesPerPixel = bitDepth/8;
      bool hasAlpha = (bitDepth == 32);
      
      for (uint8_t i=0; i < animFrames; i++) {
        displays[i]->setSwapBytes(true);
        displays[i]->fillSprite(TFT_BLACK);
      }
      uint16_t padding = 0;
      if (bitDepth == 16) {
        bmpFS.seek(BMP16_ALPHA_FLAG_OFFSET);
        hasAlpha = bmpFS.read() == 0x80;
        padding = (4 - ((w * 2) & 0b11)) & 0b11;
      } else if (bitDepth == 24) {
        padding = (4 - ((w * 3) & 0b11)) & 0b11;
      }
      bmpFS.seek(seekOffset);
      uint8_t lineBuffer[w * bytesPerPixel];

      for (frameNr = 0; frameNr < animFrames; frameNr++) {
        if (displays[frameNr]->created() && (displays[frameNr]->width()!=w || displays[frameNr]->height()!=frameH)) {
          displays[frameNr]->deleteSprite();
        }
        if (!displays[frameNr]->created()) {
          displays[frameNr]->setSwapBytes(true);
          displays[frameNr]->setColorDepth(16);
          displays[frameNr]->createSprite(w, frameH);
        }

        Serial.print("Loading frame: ");
        Serial.println(frameNr);
        for (row = 0; row < frameH; row++) {
          parseBitmapLine(&bmpFS, lineBuffer, bytesPerPixel, w, hasAlpha, padding, maskingColor, options & DITHER_TRANSPARENCY, row & 0x01);

          if (options & FLIPPED_H) {
            std::reverse((uint16_t*)lineBuffer, ((uint16_t*)lineBuffer)+w);
          }
          // Push the pixel row to screen, pushImage will crop the line if needed
          if (options & FLIPPED_V) {
            displays[frameNr]->pushImage(0, row, w, 1, (uint16_t*)lineBuffer);
          } else {
            displays[frameNr]->pushImage(0, frameH - 1 - row, w, 1, (uint16_t*)lineBuffer);
          }
        }
      }
      Serial.print("Loaded in "); Serial.print(millis() - startTime);
      Serial.println(" ms");
    } else {
      Serial.print("BMP format not recognized: ");
      Serial.print(colorPanes);
      Serial.print(" ");
      Serial.print(bitDepth);
      Serial.print(" ");
      Serial.println(compression);
      bmpFS.close();
      return false;
    }
  } else {
    Serial.print("Wrong file format: ");
    Serial.println(headerBytes);
    bmpFS.close();
    return false;
  }
  bmpFS.close();
  return true;
}

bool drawBmp(String filename, int16_t x, int16_t y, bool debugLog) {
  return drawBmpSlice(filename, x, y, -1, debugLog);
}

bool drawBmpSlice(String filename, int16_t x, int16_t y, int16_t maxH, bool debugLog) {
  if (debugLog) {
    Serial.print("File: ");
    Serial.println(filename);
    Serial.println("#");
  }

  File bmpFS;

  // Open requested file on SD card
  bmpFS = SD_MMC.open(filename);

  if (!bmpFS) {
    Serial.print("File not found: ");
    Serial.print(filename);
    Serial.println(", retrying.");
    bmpFS = SD_MMC.open(filename);
    if (!bmpFS) {
      Serial.print("Retry failed, file not found: ");
      Serial.println(filename);
      return false;
    }
  }

  uint32_t seekOffset;
  uint16_t w, h, row;

  uint32_t startTime = millis();

  uint16_t headerBytes = read16(bmpFS);
  if (headerBytes == 0x4D42) {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    uint16_t colorPanes = read16(bmpFS);
    uint16_t bitDepth = read16(bmpFS);
    uint16_t compression = read16(bmpFS);

    if ((colorPanes == 1) && (((bitDepth == 16) && (compression == 3)) || ((bitDepth == 24) && (compression == 0)) || ((bitDepth == 32) && (compression == 3)))) { // BITMAPINFOHEADER
      uint8_t bytesPerPixel = bitDepth/8;
      bool hasAlpha = (bitDepth == 32);
      
      tft.setSwapBytes(true);
      maxH = maxH == -1 ? h : maxH;

      uint16_t padding = 0;
      if (bitDepth == 16) {
        bmpFS.seek(BMP16_ALPHA_FLAG_OFFSET);
        hasAlpha = bmpFS.read() == 0x80;
        padding = (4 - ((w * 2) & 0b11)) & 0b11;
      } else if (bitDepth == 24) {
        padding = (4 - ((w * 3) & 0b11)) & 0b11;
      }
      bmpFS.seek(seekOffset);
      uint8_t lineBuffer[w * bytesPerPixel];

      for (row = h-maxH; row < h; row++) {
        parseBitmapLine(&bmpFS, lineBuffer, bytesPerPixel, w, hasAlpha, padding, TFT_BLACK, false, row & 0x01);

        // Push the pixel row to screen, pushImage will crop the line if needed
        tft.pushImage(x, y + h - 1 - row, w, 1, (uint16_t*)lineBuffer, 0x0000);
      }
      if (debugLog) {
        Serial.print("Loaded in "); Serial.print(millis() - startTime);
        Serial.println(" ms");
      }
    } else {
      Serial.print("BMP format not recognized: ");
      Serial.print(colorPanes);
      Serial.print(" ");
      Serial.print(bitDepth);
      Serial.print(" ");
      Serial.println(compression);
      bmpFS.close();
      return false;
    }
  } else {
    Serial.print("Wrong file format: ");
    Serial.println(headerBytes);
    bmpFS.close();
    return false;
  }
  bmpFS.close();
  return true;
}

static bool drawBmp(DISPLAY_T* sprite, String filename, int16_t x, int16_t y, uint16_t transp, bool enableTransp, bool debugLog) {
  if (debugLog) {
    Serial.print("File: ");
    Serial.println(filename);
    Serial.println("#");
  }

  File bmpFS;

  // Open requested file on SD card
  bmpFS = SD_MMC.open(filename);

  if (!bmpFS)
  {
    Serial.print("File not found: ");
    Serial.println(filename);
    Serial.println("#");
    return false;
  }

  uint32_t seekOffset;
  uint16_t w, h, row;

  uint32_t startTime = millis();

  uint16_t headerBytes = read16(bmpFS);
  if (headerBytes == 0x4D42) {
    read32(bmpFS);
    read32(bmpFS);
    seekOffset = read32(bmpFS);
    read32(bmpFS);
    w = read32(bmpFS);
    h = read32(bmpFS);

    uint16_t colorPanes = read16(bmpFS);
    uint16_t bitDepth = read16(bmpFS);
    uint16_t compression = read16(bmpFS);

    if ((colorPanes == 1) && (((bitDepth == 16) && (compression == 3)) || ((bitDepth == 24) && (compression == 0)) || ((bitDepth == 32) && (compression == 3)))) { // BITMAPINFOHEADER
      uint8_t bytesPerPixel = bitDepth/8;
      bool hasAlpha = (bitDepth == 32);
      
      sprite->setSwapBytes(true);
      bmpFS.seek(seekOffset);

      uint16_t padding = 0;
      if (bitDepth == 16) {
        bmpFS.seek(BMP16_ALPHA_FLAG_OFFSET);
        hasAlpha = bmpFS.read() == 0x80;
        padding = (4 - ((w * 2) & 0b11)) & 0b11;
      } else if (bitDepth == 24) {
        padding = (4 - ((w * 3) & 0b11)) & 0b11;
      }
      bmpFS.seek(seekOffset);
      uint8_t lineBuffer[w * bytesPerPixel];

      for (row = 0; row < h; row++) {
        parseBitmapLine(&bmpFS, lineBuffer, bytesPerPixel, w, hasAlpha, padding, transp, false, row & 0x01);

        // Push the pixel row to screen, pushImage will crop the line if needed
        if (enableTransp) {
          uint32_t currX = 0;
          for (uint16_t iX = 0; iX < w; iX++) {
            uint16_t color = ((uint16_t*)lineBuffer)[iX];
            if (color == transp) {
              sprite->pushImage(x + currX, y + h - 1 - row, iX-currX, 1, (uint16_t*)lineBuffer + currX, transp);
              currX = iX + 1;
            }
          }
          sprite->pushImage(x + currX, y + h - 1 - row, w-currX, 1, (uint16_t*)lineBuffer + currX, transp);
        } else {
          sprite->pushImage(x, y + h - 1 - row, w, 1, (uint16_t*)lineBuffer, 0x0000);
        }
      }
      if (debugLog) {
        Serial.print("Loaded in "); Serial.print(millis() - startTime);
        Serial.println(" ms");
      }
    } else {
      Serial.print("BMP format not recognized: ");
      Serial.print(colorPanes);
      Serial.print(" ");
      Serial.print(bitDepth);
      Serial.print(" ");
      Serial.println(compression);
      bmpFS.close();
      return false;
    }
  } else {
    Serial.print("Wrong file format: ");
    Serial.println(headerBytes);
    bmpFS.close();
    return false;
  }
  bmpFS.close();
  return true;
}

bool drawBmp(DISPLAY_T* sprite, String filename, int16_t x, int16_t y, bool debugLog) {
  return drawBmp(sprite, filename, x, y, TFT_BLACK, false, debugLog);
}

bool drawBmp(DISPLAY_T* sprite, String filename, int16_t x, int16_t y, uint16_t transp, bool debugLog) {
  return drawBmp(sprite, filename, x, y, transp, true, debugLog);
}

void fillTriangle(DISPLAY_T* display, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint16_t color) {
  fillQuad(display, x0, y0, x1, y1, x2, y2, x2, y2, color);
}

void fillQuad(DISPLAY_T* display, int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, uint16_t color) {
  if (y0 < y1) { swap(y0, y1); swap(x0, x1); }
  if (y2 < y3) { swap(y2, y3); swap(x2, x3); }
  if (y0 < y2) { swap(y0, y2); swap(x0, x2); }
  if (y1 < y2) { swap(y1, y2); swap(x1, x2); }
  int32_t x4 = x0 + (x2 - x0) * (y1 - y0) / (y2 - y0);
  int32_t x5 = x1 + (x3 - x1) * (y2 - y1) / (y3 - y1);
  if ((x5 > x2) == (x4 > x1)) {
    swap(x2, x5);
  }
  int32_t y, a, b;
  for (y = y0; y <= y1; y++) {
    a = x0 + (x4 - x0) * (y - y0) / (y1 - y0);
    b = x0 + (x1 - x0) * (y - y0) / (y1 - y0);
    if (a > b) swap(a, b);
    display->drawFastHLine(a, y, b-a, color);
  }
  for (; y <= y2; y++) {
    a = x4 + (x2 - x4) * (y - y1) / (y2 - y1);
    b = x1 + (x5 - x1) * (y - y1) / (y2 - y1);
    if (a > b) swap(a, b);
    display->drawFastHLine(a, y, b-a, color);
  }
  for (; y <= y3; y++) {
    a = x2 + (x3 - x2) * (y - y2) / (y3 - y2);
    b = x5 + (x3 - x5) * (y - y2) / (y3 - y2);
    if (a > b) swap(a, b);
    display->drawFastHLine(a, y, b-a, color);
  }
}

void drawSpriteTransformed(DISPLAY_T* display, TFT_eSprite* sprite, Vector2D* pos, Matrix2D* transform, uint32_t flags, uint16_t maskColor) {
  drawSpriteTransformed(display, sprite, pos, transform, flags, maskColor, sprite->width(), sprite->height(), 0);
}

void drawSpriteTransformed(DISPLAY_T* display, TFT_eSprite* sprite, Vector2D* pos, Matrix2D* transform, uint32_t flags, uint16_t maskColor, int16_t frameWidth, int16_t frameHeight, int16_t frameNr) {
  const int32_t spriteWidth = frameWidth;
  const int32_t spriteHeight = frameHeight;
  int32_t frameOffset = frameNr * frameHeight * frameWidth;
  uint16_t* screenBuffer = display->get16BitBuffer();
  uint16_t* spriteBuffer = sprite->get16BitBuffer() + frameOffset;

  maskColor = maskColor << 8 | maskColor >> 8;

  int32_t xOffset = flags & ALIGN_H_CENTER ? -spriteWidth / 2 : (flags & ALIGN_H_RIGHT ? -spriteWidth : 0);
  int32_t yOffset = flags & ALIGN_V_CENTER ? -spriteHeight / 2 : (flags & ALIGN_V_BOTTOM ? -spriteHeight : 0);
  int32_t xIndexOffset = xOffset - spriteWidth / 2;
  int32_t yIndexOffset = yOffset - spriteHeight / 2;

  Matrix2D inv;
  invertM(transform, &inv);

  Vector2D corners[4] = {
    { (float)xOffset, (float)yOffset },
    { (float)(xOffset + spriteWidth), (float)yOffset },
    { (float)(xOffset + spriteWidth), (float)(yOffset + spriteHeight) },
    { (float)xOffset,  (float)(yOffset + spriteHeight) }
  };

  Vector2D screenCorners[4];
  for (int32_t i = 0; i < 4; ++i) {
    screenCorners[i].x = transform->a * corners[i].x + transform->b * corners[i].y + pos->x;
    screenCorners[i].y = transform->c * corners[i].x + transform->d * corners[i].y + pos->y;
  }

  float minX = screenCorners[0].x;
  float maxX = screenCorners[0].x;
  float minY = screenCorners[0].y;
  float maxY = screenCorners[0].y;
  for (int32_t i = 1; i < 4; ++i) {
    minX = fmin(minX, screenCorners[i].x);
    maxX = fmax(maxX, screenCorners[i].x);
    minY = fmin(minY, screenCorners[i].y);
    maxY = fmax(maxY, screenCorners[i].y);
  }

  int32_t startX = fmax(0, (int32_t)floor(minX));
  int32_t endX   = fmin(SCREEN_WIDTH - 1, (int32_t)ceil(maxX));
  int32_t startY = fmax(0, (int32_t)floor(minY));
  int32_t endY   = fmin(SCREEN_HEIGHT - 1, (int32_t)ceil(maxY));

  for (int32_t y = startY; y <= endY; ++y) {
    for (int32_t x = startX; x <= endX; ++x) {
      float dx = x - pos->x;
      float dy = y - pos->y;
      float sx = inv.a * dx + inv.b * dy - xOffset;
      float sy = inv.c * dx + inv.d * dy - yOffset;

      int32_t yAddScreen = y * SCREEN_WIDTH;
      int32_t yAddSprite = (int32_t)(sy) * spriteWidth;
      if (sx >= 0 && sx < spriteWidth && sy >= 0 && sy < spriteHeight) {
        uint16_t color = spriteBuffer[yAddSprite + (int32_t)sx];
        if (!(flags & TRANSP_MASK && color == maskColor)) {
          screenBuffer[yAddScreen + x] = color;
        }
      }
    }
  }
}

void drawSpriteScaled(DISPLAY_T* display, TFT_eSprite* sprite, Vector2D* position, Vector2D* scale, uint32_t flags, uint16_t maskColor) {
  drawSpriteScaled(display, sprite, position, scale, flags, maskColor, sprite->width(), sprite->height(), 0);
}

void drawSpriteScaled(DISPLAY_T* display, TFT_eSprite* sprite, Vector2D* position, Vector2D* scale, uint32_t flags, uint16_t maskColor, int16_t frameWidth, int16_t frameHeight, int16_t frameNr) {
  float spriteWScaled = std::abs(frameWidth * scale->x);
  float spriteHScaled = std::abs(frameHeight * scale->y);
  float drawPosX = position->x;
  float drawPosY = position->y;
  uint16_t* screenBuffer = display->get16BitBuffer();
  uint16_t* spriteBuffer = sprite->get16BitBuffer();
  if (flags & ALIGN_H_CENTER) {
    drawPosX -= spriteWScaled*0.5f;
  } else if (flags & ALIGN_H_RIGHT) {
    drawPosX -= spriteWScaled;
  }
  if (flags & ALIGN_V_CENTER) {
    drawPosY -= spriteHScaled*0.5f;
  } else if (flags & ALIGN_V_BOTTOM) {
    drawPosY -= spriteHScaled;
  }
  uint32_t displayW = display->width();
  uint32_t displayH = display->height();
  int32_t toX = _min(drawPosX+spriteWScaled, displayW);
  int32_t toY = _min(drawPosY+spriteHScaled, displayH);
  float inverseScaleX = std::abs(1/scale->x);
  float inverseScaleY = std::abs(1/scale->y);
  if (sprite->getSwapBytes()) {
    maskColor = maskColor << 8 | maskColor >> 8;
  }
  int32_t frameOffset = frameNr * frameHeight * frameWidth;
  for (int32_t y = _max(drawPosY, 0); y<toY; y++) {
    int32_t addYScreen = y * displayW;
    int32_t scaledSpriteY = constrain((int32_t)((y-drawPosY) * inverseScaleY), 0, frameHeight-1);
    int32_t addYSprite;
    if (scale->y >= 0) {
      addYSprite = scaledSpriteY * frameWidth + frameOffset;
    } else {
      addYSprite = (frameHeight - scaledSpriteY -1) * frameWidth + frameOffset;
    }
    for (int32_t x = _max(drawPosX, 0); x<toX; x++) {
      int32_t scaledSpriteX = constrain((int32_t)((x-drawPosX) * inverseScaleX), 0, frameWidth-1);
      uint16_t color;
      if (scale->x >= 0) {
        color = spriteBuffer[scaledSpriteX + addYSprite];
      } else {
        color = spriteBuffer[frameWidth - scaledSpriteX - 1 + addYSprite];
      }
      if (!(flags & TRANSP_MASK && color == maskColor)) {
        screenBuffer[x + addYScreen] = color;
      }
    }
  }
}

static void drawProgressBarCommon(DISPLAY_T* display, uint16_t percent, uint16_t greenOffset, int16_t x, int16_t y, int16_t w, int16_t h) {
  display->drawRect(x, y, w, h, TFT_WHITE);

  uint16_t fillWidth = (w * percent) / 100;

  uint16_t color;
  uint16_t percentOffset = abs(100-percent);
  if (percentOffset < greenOffset) {
    color = TFT_GREEN;
  } else if (percentOffset < 25) {
    color = TFT_YELLOW;
  } else if (percentOffset < 45) {
    color = TFT_ORANGE;
  } else {
    color = TFT_RED;
  }
  display->fillRect(x+1, y+1, fillWidth-2, h-2, color);
}

void drawProgressBar(DISPLAY_T* display, uint16_t percent, uint16_t greenOffset, int16_t x, int16_t y, int16_t w, int16_t h) {
  drawProgressBarCommon(display, percent, greenOffset, x, y, w, h);
  display->setTextSize(1);
  display->setTextColor(TFT_WHITE);
  display->setCursor(x, y - 11);
  printShaded(display, String(percent) + "%");
}

void drawProgressBar(DISPLAY_T* display, uint16_t val, uint16_t maxVal, uint16_t greenOffset, int16_t x, int16_t y, int16_t w, int16_t h) {
  uint16_t percent = (val * 100) / _max(1, maxVal);
  greenOffset = (greenOffset * 100) /_max(1, maxVal);
  drawProgressBarCommon(display, percent, greenOffset, x, y, w, h);
  display->setTextSize(1);
  display->setTextColor(TFT_WHITE);
  display->setCursor(x, y - 11);
  printShaded(display, String(val) + "/" + String(maxVal));
}

void printShaded(DISPLAY_T* display, String text) {
  printShaded(display, text, 1, 0xFFFF, 0x0000);
}

void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength) {
  printShaded(display, text, shadeStrength, 0xFFFF, 0x0000);
}

void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength, uint16_t textColor) {
  printShaded(display, text, shadeStrength, textColor, 0x0000);
}

void printShaded(DISPLAY_T* display, String text, uint8_t shadeStrength, uint16_t textColor, uint16_t shadeColor) {
  int16_t x = display->getCursorX();
  int16_t y = display->getCursorY();
  display->setTextColor(shadeColor);
  for (int16_t xOffset = -(int8_t)shadeStrength; xOffset<=shadeStrength; xOffset+=2) {
    for (int16_t yOffset = -(int8_t)shadeStrength; yOffset<=shadeStrength; yOffset+=2) {
      display->setCursor(x+xOffset, y+yOffset);
      display->print(text);
    }
  }
  display->setTextColor(textColor);
  display->setCursor(x, y);
  display->print(text);
}

#define MENU_SPRITE_COUNT_LIMIT 11
static String menuSpritePaths[MENU_SPRITE_COUNT_LIMIT];
static TFT_eSprite* menuSprites = nullptr;

static void refreshMenuSprites() {
  if (menuSprites == nullptr) {
    menuSprites = (TFT_eSprite*) heap_caps_malloc(sizeof(TFT_eSprite) * MENU_SPRITE_COUNT_LIMIT, MALLOC_CAP_SPIRAM);
    if (!menuSprites) {
      Serial.println("Failed to allocate sprites in PSRAM!");
      checkFailWithMessage("init: Failed to allocate sprites in PSRAM!");
      return;
    }

    for (size_t i = 0; i < MENU_SPRITE_COUNT_LIMIT; ++i) {
      new (&menuSprites[i]) TFT_eSprite(&tft);
      menuSprites[i].setColorDepth(16);
    }
  }
}

static void refreshAndDrawMenuSprite(DISPLAY_T* display, uint32_t slotId, String path, int32_t x, int32_t y) {
  if (!menuSpritePaths[slotId].equals(path)) {
    Serial.print("(Re-)loading menu sprite ");
    Serial.print(path);
    Serial.print(" to slot ");
    Serial.print(slotId);
    Serial.println(".");
    if (menuSprites[slotId].created()) {
      menuSprites[slotId].deleteSprite();
    }
    if (!loadBmp(&menuSprites[slotId], path, 0, 0xf81f)) {
      Serial.println("Failed to load sprite " + path);
    }
    menuSpritePaths[slotId] = path;
  }
  menuSprites[slotId].pushToSprite(display, x - menuSprites[slotId].width()/2, y  - menuSprites[slotId].height()/2, 0xf81f);
}

static void drawStringWordWrapped(DISPLAY_T* display, String string, uint32_t charsPerLine, int32_t x, int32_t y) {
  if (string.length()<=charsPerLine) {
    display->drawString(string, x, y);
  } else {
    int32_t wordCount = 0;
    int32_t spacePos = 0;
    while (spacePos >= 0) {
      spacePos = string.indexOf(" ", spacePos+1);
      Serial.print("Spacepos ");
      Serial.println(spacePos);
      wordCount++;
    }
    spacePos = -1;
    for (int32_t i=1;i<=wordCount;i++) {
      Serial.print(i);
      Serial.print("/");
      Serial.println(wordCount);
      int32_t nextSpacePos = string.indexOf(" ", spacePos+1);
      if (nextSpacePos==-1) {
        nextSpacePos = string.length();
      }
      display->drawString(string.substring(spacePos+1, nextSpacePos), x, y + (i - wordCount)*9);
      spacePos = nextSpacePos;
    }
  }
}

static void drawProfileSelectionPage(DISPLAY_T* display, uint16_t startNr, uint16_t nr, bool drawArrows, uint8_t systemUpdateAvailableStatus, String* errorMessage) {
  refreshMenuSprites();
  int32_t columns = _min(4, nr);
  int32_t rows = nr>4 ? 2 : 1;
  int32_t cWidth = (290 - 10*columns) / columns;
  int32_t cHeight = rows==1 ? 200 : 95;
  for (int32_t c = 0; c<columns; c++) {
    for (int32_t r = 0; r<rows; r++) {
      int32_t profileId = c + r*columns;
      if (profileId < nr) {
        ProfileData profileData;
        readProfileData(profileId, &profileData, errorMessage);
        display->fillRect(20 + c*(cWidth + 10), 30+r*(cHeight+10), cWidth, cHeight, TFT_BLUE);
        refreshAndDrawMenuSprite(display, profileId, profileData.imagePath, 20 + c*(cWidth + 10) + cWidth/2, 30+r*(cHeight+10) + cHeight/2);
        uint8_t textDatumBackup = display->getTextDatum();
        display->setTextDatum(BC_DATUM);
        display->setTextSize(1);
        drawStringWordWrapped(display, profileData.name, 13, 20 + c*(cWidth + 10) + cWidth/2, 30+r*(cHeight+10) + cHeight - 3);
        display->setTextDatum(textDatumBackup);
      }
    }
  }
  refreshAndDrawMenuSprite(display, 8, "/gfx/progressionmenu.bmp", SCREEN_WIDTH - 16, 16);
  refreshAndDrawMenuSprite(display, 9, "/gfx/executionlist.bmp", SCREEN_WIDTH - 64, 16);
  if (systemUpdateAvailableStatus == FIRMWARE_UPDATE_AVAILABLE) {
    display->setTextDatum(BR_DATUM);
    display->setTextSize(1);
    display->drawString("System-Update verfügbar", SCREEN_WIDTH - 35, SCREEN_HEIGHT - 1, GFXFF);
    refreshAndDrawMenuSprite(display, 10, "/gfx/systemupdate.bmp", SCREEN_WIDTH - 16, SCREEN_HEIGHT - 16);
  } else if (systemUpdateAvailableStatus == FIRMWARE_UPDATE_CHECK_RUNNING) {
    display->setTextDatum(BR_DATUM);
    display->setTextSize(1);
    display->drawString("Suche System-Update...", SCREEN_WIDTH - 35, SCREEN_HEIGHT - 1, GFXFF);
  }
}

static void drawGameSelectionPage(DISPLAY_T* display, uint16_t startNr, uint16_t nr, bool drawArrows, uint32_t requiredTaskTypes, String* errorMessage) {
  int32_t columns = _min(4, nr);
  int32_t rows = nr>4 ? 2 : 1;
  int32_t cWidth = (290 - 10*columns) / columns;
  int32_t cHeight = rows==1 ? 200 : 95; 
  display->setTextDatum(BC_DATUM);
  display->setTextSize(1);
  GameConfig gameConfig;
  String ignoreErrors;
  for (int32_t c = 0; c<columns; c++) {
    for (int32_t r = 0; r<rows; r++) {
      if (c + r*columns < nr) {
        uint32_t gameId = c + r*columns;
        String gamePath = getGamePath(gameId, requiredTaskTypes, errorMessage);
        readGameConfig(gamePath, &gameConfig, &ignoreErrors);
        display->fillRect(20 + c*(cWidth + 10), 30+r*(cHeight+10), cWidth, cHeight, TFT_BLUE);
        refreshAndDrawMenuSprite(display, gameId, gamePath + "logo.bmp", 20 + c*(cWidth + 10) + cWidth/2, 30+r*(cHeight+10) + cHeight/2);
        drawStringWordWrapped(display, gameConfig.name, 13, 20 + c*(cWidth + 10) + cWidth/2, 30+r*(cHeight+10) + cHeight - 3);
      }
    }
  }
  display->setTextDatum(TL_DATUM);
}

static int16_t checkSelectionPageSelection(uint16_t startNr, uint16_t nr, bool drawArrows, bool progressMenuIcon, bool executionListIcon, bool systemupdateAvailable) {
  int32_t columns = _min(4, nr);
  int32_t rows = nr>4 ? 2 : 1;
  int32_t cWidth = (290 - 10*columns) / columns;
  int32_t cHeight = rows==1 ? 220 : 105; 
  if (progressMenuIcon && isTouchInZone(SCREEN_WIDTH - 32, 0, 32, 32)) {
    return PROGRESS_MENU_SELECTION_ID;
  }
  if (progressMenuIcon && isTouchInZone(SCREEN_WIDTH - 80, 0, 32, 32)) {
    return EXECUTION_LIST_SELECTION_ID;
  }
  if (systemupdateAvailable==FIRMWARE_UPDATE_AVAILABLE && isTouchInZone(SCREEN_WIDTH - 32, SCREEN_HEIGHT - 32, 32, 32)) {
    Serial.println("System update selected");
    return SYSTEM_UPDATE_SELECTION_ID;
  }
  for (uint32_t c = 0; c<columns; c++) {
    for (uint32_t r = 0; r<rows; r++) {
      if (isTouchInZone(20 + c*(cWidth + 10), 10+(r*(cHeight+10)), cWidth, cHeight)) {
        return startNr + c + r*columns;
      }
    }
  }
  return -1;
}

int16_t displayGameSelection(DISPLAY_T* display, uint16_t nr, uint32_t requiredTaskTypes, String* errorMessage) {
  uint16_t startNr = 0;
  uint32_t ms = millis();
  uint32_t lastMs = millis();

  for (uint32_t i = 0;i<2;i++) {
    display->fillSprite(TFT_BLACK);
    drawGameSelectionPage(display, startNr, _min(nr, 8), nr>8, requiredTaskTypes, errorMessage);
    display->pushSpriteFast(0, 0);
  }

  while (true) {
    lastMs = ms;
    ms = millis();
    handleSerial();
    int16_t selection = checkSelectionPageSelection(startNr, _min(nr, 8), nr>8, false, false, false);
    if (selection != -1 && selection<nr) {
      return selection;
    }
    display->fillRect(0,0,100,20,TFT_BLACK);
    doSystemTasks();
    display->pushSpriteFast(0,0);
    if (millis()>GAME_SELECTION_POWEROFF_TIMEOUT) {
      power_off();
    }
  }
}

int16_t displayProfileSelection(DISPLAY_T* display, uint16_t nr, String* errorMessage) {
  uint16_t startNr = 0;
  uint32_t ms = millis();
  uint32_t lastMs = millis();

  uint8_t systemupdateAvailableStatus = getSystemUpdateAvailableStatus();
  for (uint32_t i = 0;i<2;i++) {
    display->fillSprite(TFT_BLACK);
    drawProfileSelectionPage(display, startNr, _min(nr, 8), nr>8, systemupdateAvailableStatus, errorMessage);
    display->pushSpriteFast(0, 0);
  }

  while (true) {
    lastMs = ms;
    ms = millis();
    handleSerial();
    if (systemupdateAvailableStatus == FIRMWARE_UPDATE_CHECK_RUNNING) {
      String throbber = "";
      uint32_t throbNr = (millis()/300L)%4;
      switch (throbNr) {
        case 0:
          throbber = "|";break;
        case 1:
          throbber = "/";break;
        case 2:
          throbber = "-";break;
        case 3:
          throbber = "\\";break;
      }
      display->fillRect(SCREEN_WIDTH - 15, SCREEN_HEIGHT - 19, 15, 19, TFT_BLACK);
      display->setTextSize(2);
      display->setTextDatum(BR_DATUM);
      display->drawString(throbber, SCREEN_WIDTH - 2, SCREEN_HEIGHT - 2);

      uint8_t newSystemupdateAvailableStatus = getSystemUpdateAvailableStatus();
      if (newSystemupdateAvailableStatus != systemupdateAvailableStatus) {
        systemupdateAvailableStatus = newSystemupdateAvailableStatus;
        for (uint32_t i = 0;i<2;i++) {
          display->fillSprite(TFT_BLACK);
          drawProfileSelectionPage(display, startNr, _min(nr, 8), nr>8, systemupdateAvailableStatus, errorMessage);
          display->pushSpriteFast(0, 0);
        }
      }
    }

    int16_t selection = checkSelectionPageSelection(startNr, _min(nr, 8), nr>8, true, true, systemupdateAvailableStatus);
    if (selection != -1 && (selection<nr || selection == PROGRESS_MENU_SELECTION_ID || selection == SYSTEM_UPDATE_SELECTION_ID || selection == EXECUTION_LIST_SELECTION_ID)) {
      return selection;
    }
    display->fillRect(0,0,100,20,TFT_BLACK);
    doSystemTasks();
    display->pushSpriteFast(0,0);
    if (millis()>GAME_SELECTION_POWEROFF_TIMEOUT) {
      power_off();
    }
  }
}

static String leftPad(String s, uint16_t len, String c) {
  while (s.length()<len) {
    s = c + s;
  }
  return s;
}

static void initSystemStatsSprites() {
  if (systemStatsSprites == nullptr) {
    systemStatsSprites = (TFT_eSprite*) heap_caps_malloc(sizeof(TFT_eSprite)*SYSTEM_STATS_SPRITE_COUNT, MALLOC_CAP_SPIRAM);
    if (!systemStatsSprites) {
      Serial.println("Failed to allocate sprites in PSRAM!");
      checkFailWithMessage("init: Failed to allocate sprites in PSRAM!");
      return;
    }
    for (uint32_t i=0; i<SYSTEM_STATS_SPRITE_COUNT; i++) {
      new (&systemStatsSprites[i]) TFT_eSprite(&tft);
      systemStatsSprites[i].setColorDepth(16);
    }

    loadBmp(&systemStatsSprites[0], "/gfx/battery_low.bmp");
    loadBmp(&systemStatsSprites[1], "/gfx/battery_half.bmp");
    loadBmp(&systemStatsSprites[2], "/gfx/battery_full.bmp");
    loadBmp(&systemStatsSprites[3], "/gfx/wifi.bmp");
    loadBmp(&systemStatsSprites[4], "/gfx/nowifi.bmp");
  }
}

// Draws battery icon, battery voltage, FPS
void drawSystemStats() {
  initSystemStatsSprites();
  static int32_t lowBatteryCount = -1;
  static uint32_t lowBatteryWarningCount = 0;
  static uint32_t lastMs = millis();
  uint32_t ms = millis();
  uint32_t batteryVoltage = readBatteryVoltage();
  if (batteryVoltage < 3600) {
    systemStatsSprites[0].pushToSprite(&spr, 0, 0, 0x0000);
  } else if (batteryVoltage < 3800) {
    systemStatsSprites[1].pushToSprite(&spr, 0, 0, 0x0000);
  } else if (batteryVoltage < 4200) {
    systemStatsSprites[2].pushToSprite(&spr, 0, 0, 0x0000);
  } else if (batteryVoltage < 4400) {
    systemStatsSprites[0].pushToSprite(&spr, 0, 0, 0x0000);
  } else if (batteryVoltage < 4600) {
    systemStatsSprites[1].pushToSprite(&spr, 0, 0, 0x0000);
  } else {
    systemStatsSprites[2].pushToSprite(&spr, 0, 0, 0x0000);
  }
  if (batteryVoltage<BATTERY_LOW_WARNING_VOLTAGE ) {
    lowBatteryWarningCount++;
  } else {
    lowBatteryWarningCount = 0;
  }
  if (lowBatteryWarningCount>100 && batteryVoltage<BATTERY_LOW_WARNING_VOLTAGE && (ms/1000)&0x01) {
    systemStatsSprites[0].pushToSprite(&spr, 144, 110, 0x0000);
  }
  if (batteryVoltage<BATTERY_LOW_SHUTDOWN_VOLTAGE) {
    lowBatteryCount++;
  } else {
    lowBatteryCount = 0;
  }
  if (lowBatteryCount>100) {
    power_off();
  }
  if (getWifiStatus() == WIFI_CONNECTION_OK) {
    systemStatsSprites[3].pushToSprite(&spr, 34, 0, 0xf81f);
  } else if (getWifiStatus() == WIFI_CONNECTION_NOWIFI) {
    systemStatsSprites[4].pushToSprite(&spr, 34, 0, 0xf81f);
  } else if ((millis() >> 10) & 0x01) { // WIFI_CONNECTION_SEARCHING, blink once per second
    systemStatsSprites[3].pushToSprite(&spr, 34, 0, 0xf81f);
  }
  spr.setTextDatum(TL_DATUM);
  spr.setTextSize(1);
  spr.drawString(String(1000L/_max(1,ms-lastMs)), 64, 11); //FPS counter
  if ((millis()>>12)&0x01) { // switch between battery voltage and time every 4 seconds
    spr.drawString(String(batteryVoltage/1000) + "." + leftPad(String((batteryVoltage/10)%100), 2, "0") + "V", 64, 1); // Battery voltage
  } else {
    String timeString;
    String dateString;
    String errorMessage;
    getFormattedTime(&dateString, &timeString, &errorMessage);
    spr.drawString(timeString, 64, 1); // Battery voltage
  }
  lastMs = ms;
}

void drawImageButton(DISPLAY_T* display, String path, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, uint16_t textColor) {
  display->fillRect(x, y, w, h, color);
  int16_t bmpW, bmpH;
  getBmpDimensions(path, &bmpW, &bmpH);
  drawBmp(display, path, x + w/2 - bmpW/2, y + h/2 - bmpH/2);
}

void drawButton(DISPLAY_T* display, String text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color, uint16_t textColor) {
  display->fillRect(x, y, w, h, color);
  uint8_t textDatumBak = display->getTextDatum();
  display->setTextDatum(CC_DATUM);
  uint32_t textColorBak = display->textcolor;
  display->setTextColor(textColor);
  display->drawString(text, x + w/2, y + h/2);
  display->setTextDatum(textDatumBak);
  display->setTextColor(textColorBak);
}


bool checkButton(int16_t x, int16_t y, int16_t w, int16_t h) {
  return isTouchInZone(x, y, w, h);
}


bool drawAndCheckImageButton(DISPLAY_T* display, String path, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  drawImageButton(display, path, x, y, w, h, color);
  return isTouchInZone(x, y, w, h);
}


bool drawAndCheckButton(DISPLAY_T* display, String text, int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
  drawButton(display, text, x, y, w, h, color);
  return isTouchInZone(x, y, w, h);
}



#define KEYBOARD_PADDING 2
#define KEYBOARD_TYPING_AREA_HEIGHT 50
#define KEYBOARD_LAYERS 1
#define KEYBOARD_ROWS 4
#define KEYBOARD_COLS 13
#define KEY_BACKSPACE 1
#define KEY_ENTER 2
#define KEY_SHIFT 3
static char keyboardMatrix[KEYBOARD_LAYERS][KEYBOARD_ROWS][KEYBOARD_COLS] = {
  { // Normal layer
    {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '[', ']', KEY_BACKSPACE},
    {'q', 'w', 'e', 'r', 't', 'z', 'u', 'i', 'o', 'p', '+', '{', '}'},
    {'a', 's', 'd', 'f', 'g', 'h', 'i', 'j', 'k', 'l', '@', '~', KEY_ENTER},
    {'y', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '-', ' ', '\0', KEY_SHIFT},
  }
}; 
static uint32_t currentKeyboardLayer = 0;
void drawKeyboard(DISPLAY_T* display, uint16_t keyColor, uint16_t textColor) {
  int16_t keyW = SCREEN_WIDTH/KEYBOARD_COLS;
  int16_t keyH = (SCREEN_HEIGHT-KEYBOARD_TYPING_AREA_HEIGHT)/KEYBOARD_ROWS;
  for (int16_t row=0; row<KEYBOARD_ROWS; row++) {
    for (int16_t col=0; col<KEYBOARD_COLS; col++) {
      char c = keyboardMatrix[currentKeyboardLayer][row][col];
      if (c>32) {
        String character = " ";
        character.setCharAt(0, c);
        drawButton(display, character, keyW*col+KEYBOARD_PADDING, KEYBOARD_TYPING_AREA_HEIGHT+keyH*row+KEYBOARD_PADDING, keyW-2*KEYBOARD_PADDING, keyH-2*KEYBOARD_PADDING, keyColor, textColor);
      } else if (c==KEY_BACKSPACE) {
        drawButton(display, "<-", keyW*col+KEYBOARD_PADDING, KEYBOARD_TYPING_AREA_HEIGHT+keyH*row+KEYBOARD_PADDING, keyW-2*KEYBOARD_PADDING, keyH-2*KEYBOARD_PADDING, keyColor, textColor);
      }
    }
  }
}

void checkKeyboard(DISPLAY_T* display, String* output, uint32_t maxCharacters, uint16_t keyColor, uint16_t textColor) {
  *output = "";
  int16_t keyW = SCREEN_WIDTH/KEYBOARD_COLS;
  int16_t keyH = (SCREEN_HEIGHT-KEYBOARD_TYPING_AREA_HEIGHT)/KEYBOARD_ROWS;
  while (output->length()<maxCharacters) {
    for (int16_t row=0; row<KEYBOARD_ROWS; row++) {
      for (int16_t col=0; col<KEYBOARD_COLS; col++) {
        if (checkButton(keyW*col, KEYBOARD_TYPING_AREA_HEIGHT+keyH*row, keyW, keyH)) {
          display->fillSprite(TFT_BLACK);
          drawKeyboard(display, keyColor, textColor);
          *output = *output + keyboardMatrix[currentKeyboardLayer][row][col];
          uint8_t textDatumBak = display->getTextDatum();
          display->setTextDatum(TL_DATUM);
          uint32_t textColorBak = display->textcolor;
          display->setTextColor(textColor);
          display->drawString(*output, 10, 10);
          Serial.print("Drawing output: ");
          Serial.println(*output);
          display->setTextDatum(textDatumBak);
          display->setTextColor(textColorBak);
          display->pushSpriteFast(0,0);
          while (touch.pressed()) {}
        }
      }
    }
  }
}

void checkFailWithMessage(String message) {
  if (!message.isEmpty()) {
    tft.fillScreen(TFT_BLACK);
    while (true) {
      spr.fillSprite(TFT_BLACK);
      spr.setCursor(1, 32);
      spr.setTextSize(1);
      spr.println("FEHLER:");
      spr.println(message);
      doSystemTasks();
      spr.pushSprite(0, 0);
      handleSerial();
    };
  }
}

void checkSoftFailWithMessage(String message, uint8_t textSize) {
  if (!message.isEmpty()) {
    tft.fillScreen(TFT_BLACK);
    spr.fillSprite(TFT_BLACK);
    spr.setCursor(1, 30);
    spr.setTextSize(textSize);
    spr.println("FEHLER:");
    spr.println(message);
    spr.pushSpriteFast(0, 0);
    delay(5000);
  }
}

void displayFullscreenMessage(String message, uint8_t textSize) {
  spr.fillSprite(TFT_BLACK);
  spr.setCursor(1, 30);
  spr.setTextSize(textSize);
  spr.println(message);
  spr.pushSpriteFast(0, 0);
}

static bool touchPressed = false;
static void displayDigitSelector(int32_t* digit, int32_t min, int32_t max, uint32_t digits, int32_t x, int32_t y) {
  *digit = _max(_min(*digit, max), min);
  String digitString = String(*digit);
  while (digitString.length()<digits) {
    digitString = "0"+digitString;
  }
  spr.setTextDatum(CC_DATUM);
  spr.setTextSize(2);
  spr.setTextColor(COLOR_BUTTON_PRIMARY_TEXT);
  spr.fillRect(x, y, 50, 50, COLOR_BUTTON_PRIMARY_FRAME);
  spr.fillRect(x+2, y+2, 46, 46, COLOR_BUTTON_PRIMARY);
  spr.drawString(digitString, x+25, y+17);
  if (*digit < max) {
    spr.fillTriangle(x, y-5, x+25, y-35, x+50, y-5, COLOR_BUTTON_PRIMARY_FRAME);
    spr.fillTriangle(x+4, y-7, x+25, y-32, x+46, y-7, COLOR_BUTTON_PRIMARY);
    if (!touchPressed && isTouchInZone(x, y-35, 50, 25)) {
      (*digit)++;
      touchPressed = true;
    }
  }
  if (*digit > min) {
    spr.fillTriangle(x, y+55, x+25, y+80, x+50, y+55, COLOR_BUTTON_PRIMARY_FRAME);
    spr.fillTriangle(x+4, y+57, x+25, y+78, x+46, y+57, COLOR_BUTTON_PRIMARY);
    if (!touchPressed && isTouchInZone(x, y+55, 50, 25)) {
      (*digit)--;
      touchPressed = true;
    }
  }
}

static uint32_t getDaysInMonth(int32_t month, int32_t year) {
  if (month == 1 || month == 3 || month == 5 || month == 7 || month == 8 || month == 10 || month == 12) {
    return 31;
  } else if (month == 2) {
    if (year % 400 == 0) {
      return 29;
    } else if (year % 100 == 0) {
      return 28;
    } else if (year % 4 == 0) {
      return 29;
    } else {
      return 28;
    }
  } else {
    return 30;
  }
}

void displayDateTimeSelection() {
  int32_t hour = 0;
  int32_t minute = 0;
  int32_t day = 1;
  int32_t month = 1;
  int32_t year = 2026;
  while (true) {
    spr.fillSprite(TFT_BLACK);
    doSystemTasks();
    displayDigitSelector(&hour, 0, 23, 2, 10, 105);
    displayDigitSelector(&minute, 0, 59, 2, 65, 105);

    displayDigitSelector(&day, 1, getDaysInMonth(month, year), 2, 140, 105);
    displayDigitSelector(&month, 1, 12, 2, 195, 105);
    displayDigitSelector(&year, 2026, 2100, 4, 250, 105);

    spr.setTextDatum(CC_DATUM);
    spr.setTextSize(2);
    spr.drawString("Uhrzeit", 60, 40);
    spr.drawString("Datum", 220, 40);
    spr.setTextColor(COLOR_BUTTON_PRIMARY_TEXT);
    spr.fillRect(120, 200, 80, 40, COLOR_BUTTON_PRIMARY_FRAME);
    spr.fillRect(125, 205, 70, 30, COLOR_BUTTON_PRIMARY);
    spr.drawString("OK", 160, 212);
    
    if (isTouchInZone(120, 200, 80, 40)) {
      break;
    } else if (!isTouchInZone(0, 0, 320, 240)) {
      touchPressed = false;
    }
    
    spr.pushSpriteFast(0, 0);
  }
  struct tm tm;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_mday = day;
  tm.tm_mon = month-1;
  tm.tm_year = year-1900;
  tm.tm_isdst = -1;
  time_t t = mktime(&tm);
  if (t == (time_t)-1) {
    Serial.println("mktime failed!");
    return;
  }
  
  struct timeval tv;
  tv.tv_sec = t;
  tv.tv_usec = 0;
  if (settimeofday(&tv, NULL) != 0) {
    Serial.println("settimeofday failed!");
    return;
  }
  spr.setTextDatum(TL_DATUM);
}
