#ifndef __HIGHERLEVELGFXHANDLER_H__
#define __HIGHERLEVELGFXHANDLER_H__

#include <Arduino.h>
#include "hardware/gfxHandler.hpp"

struct RoadColors {
  uint16_t pavementA;
  uint16_t pavementB;
  uint16_t embankmentA;
  uint16_t embankmentB;
  uint16_t grassA;
  uint16_t grassB;
  uint16_t wallA;
  uint16_t wallB;
  uint16_t railA;
  uint16_t railB;
  uint16_t lamp;
  uint16_t mountain;
  uint16_t centerLine;
  uint16_t sky;
};

struct RoadDrawFlags {
  bool drawMountain;
  bool drawLamps;
  bool drawRails;
  bool drawCenterLine;
  bool drawTunnelDoors;
};

struct RoadDimensions {
  float roadWidth;
  float embankmentWidth;
  float centerlineWidth;
  float railingDistance;
  float railingHeight;
  float railingThickness;
  float lampHeight;
};

void drawMode7(DISPLAY_T* display, TFT_eSprite* texture, Vector2D* cameraPos, float cameraHeight, float yawAngle, float zoom, float horizonHeight, int32_t startY, int32_t endY);
void mode7WorldToScreen(Vector2D* worldPos, Vector2D* cameraPos, float cameraHeight, float yawAngle, float zoom, float horizonHeight, int32_t startY, int32_t endY, Vector3D* output);
void calculateRoadProperties(float y, float distance, float horizonY, float baselineY, float roadXOffset, float* x, float* w, int32_t* roadYOffset);
void drawRaceOutdoor(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, float lastX, float lastW, RoadColors* colors, RoadDrawFlags* flags, RoadDimensions* roadDimensions);
void drawRaceTunnel(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, float lastX, float lastW, RoadColors* colors, RoadDrawFlags* flags, RoadDimensions* roadDimensions);
void projectRoadPointToScreen(float roadX, float roadZ, float horizonY, float baselineY, float roadXOffset, float* outScreenX, float* outScreenY, float* scale);

#endif