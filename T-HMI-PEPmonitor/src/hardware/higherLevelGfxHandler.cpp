#include "higherLevelGfxHandler.hpp"
#include "constants.h"

#define SCREEN_CENTER_F SCREEN_WIDTH * 0.5f

typedef struct {
  TFT_eSprite* display;
  TFT_eSprite* texture;
  Vector2D* cameraPos;
  float cameraHeight;
  float yawAngle;
  float zoom;
  float horizonHeight;
  int32_t startY;
  int32_t endY;
  volatile int32_t downwardsY;
  volatile int32_t upwardsY;
} Mode7TaskParameters;

static volatile Mode7TaskParameters mode7TaskParameters;
static TaskHandle_t mode7TaskHandle;

void doDrawMode7(DISPLAY_T* display,
  TFT_eSprite* texture,
  Vector2D* cameraPos,
  float cameraHeight,
  float yawAngle,
  float zoom,
  float horizonHeight,
  int32_t startY,
  int32_t endY, // radians
  bool drawDownwards
) {
  float cosYaw = std::cos(yawAngle);
  float sinYaw = std::sin(yawAngle);
  int32_t screenWidth = display->width();
  int32_t screenHeight = endY - startY;
  int32_t textureWidth = texture->width();

  uint16_t* screenBuffer = display->get16BitBuffer();
  uint16_t* textureBuffer = texture->get16BitBuffer();

  int32_t centerXi = screenWidth / 2;
  int32_t centerYi = screenHeight / 2;

  uint32_t textureWidthMask = textureWidth - 1;
  uint32_t textureHeightMask = texture->height() - 1;

  float scaling = 10*cameraHeight;

  for (int32_t iY = -centerYi; iY < centerYi; iY++) {
    int32_t y = drawDownwards ? iY : -iY;
    if (drawDownwards) {
      mode7TaskParameters.downwardsY = y;
    } else {
      mode7TaskParameters.upwardsY = y;
      if (mode7TaskParameters.downwardsY >= y) {
        return;
      }
    }
    int32_t screenY = y + centerYi + startY;
    int32_t screenYAdd = screenY * screenWidth;

    if (y + centerYi <= horizonHeight) {
      // Fill sky above horizon
      /*for (int32_t x = 0; x < screenWidth; x++) {
        screenBuffer[x + screenYAdd] = 0x7E7D; // sky color
      }*/
    } else {
      float fov = 120 / zoom;
      float py = fov;
      float pz = (y + horizonHeight);
      float sy = py / pz;

      float sYsin = -sy * sinYaw;
      float sYcos = sy * cosYaw;

      for (int32_t px = -centerXi; px < centerXi; px++) {
        float sx = px / pz;

        float worldX = sx * cosYaw + sYsin;
        float worldY = sx * sinYaw + sYcos;

        sx = worldX * scaling + cameraPos->x;
        sy = worldY * scaling + cameraPos->y;

        uint32_t texX = (uint32_t)sx & textureWidthMask;
        uint32_t texY = (uint32_t)sy & textureHeightMask;
        screenBuffer[px + centerXi + screenYAdd] = textureBuffer[texX + texY * textureWidth];
      }
    }
    if (drawDownwards && mode7TaskParameters.upwardsY<=y) {
      return;
    }
  }
  Serial.println(drawDownwards ? "Downwards ran out" : "Upwards ran out");
}

void drawMode7Task(void* parameter) {
  doDrawMode7(
    mode7TaskParameters.display,
    mode7TaskParameters.texture,
    mode7TaskParameters.cameraPos,
    mode7TaskParameters.cameraHeight,
    mode7TaskParameters.yawAngle,
    mode7TaskParameters.zoom,
    mode7TaskParameters.horizonHeight,
    mode7TaskParameters.startY,
    mode7TaskParameters.endY,
    false
  );
  vTaskDelete(NULL);
}

void drawMode7(DISPLAY_T* display,
  TFT_eSprite* texture,
  Vector2D* cameraPos,
  float cameraHeight,
  float yawAngle,
  float zoom,
  float horizonHeight,
  int32_t startY,
  int32_t endY // radians
) {
  mode7TaskParameters.display = display;
  mode7TaskParameters.texture = texture;
  mode7TaskParameters.cameraPos = cameraPos;
  mode7TaskParameters.cameraHeight = cameraHeight;
  mode7TaskParameters.yawAngle = yawAngle;
  mode7TaskParameters.zoom = zoom;
  mode7TaskParameters.horizonHeight = horizonHeight;
  mode7TaskParameters.startY = startY;
  mode7TaskParameters.endY = endY;
  mode7TaskParameters.downwardsY = -10000;
  mode7TaskParameters.upwardsY = +10000;
  xTaskCreatePinnedToCore(drawMode7Task, "mode7draw", 10000, NULL, 23, &mode7TaskHandle, 0);
  doDrawMode7(
    display,
    texture,
    cameraPos,
    cameraHeight,
    yawAngle,
    zoom,
    horizonHeight,
    startY,
    endY,
    true
  );
}

void mode7WorldToScreen(
                Vector2D* worldPos,
                Vector2D* cameraPos,
                float cameraHeight,
                float yawAngle,
                float zoom,
                float horizonHeight,
                int32_t startY,
                int32_t endY,
                Vector3D* output) {
    float scaling = 1.0f/(10.0f*cameraHeight);

    float dx = (worldPos->x - cameraPos->x) * scaling;
    float dy = (worldPos->y - cameraPos->y) * scaling;

    float cosYaw = std::cos(yawAngle);
    float sinYaw = -std::sin(yawAngle);

    // Rotate world position into camera space
    float camX = dx * cosYaw - dy * sinYaw;
    float camY = dx * sinYaw + dy * cosYaw;

    // Perspective projection
    float fov = 120.0f / zoom;
    float px = camX;
    float py = fov;
    float pz = camY;

    if (pz <= 0.1f){ // Behind camera or too close
      output->x = -1000;
      output->y = -1000;
      output->z = -1000;
      return;
    }
    output->x = (px / pz) * (float)SCREEN_WIDTH / 2.0f + (float)SCREEN_WIDTH / 2.0f;
    output->y = (py / pz) + horizonHeight;
    output->z = pz;
}

void projectRoadPointToScreen(
    float roadX, // in pixel at baseline
    float roadZ, // in "meters"
    float horizonY,
    float baselineY,
    float roadXOffset, // determines how much the road turns. In pixel at horizon
    float* outScreenX,
    float* outScreenY,
    float* scale
) {
    if (outScreenX == NULL || outScreenY == NULL || scale == NULL) return;
    if (baselineY <= horizonY) baselineY = horizonY + 1.0f;
    if (roadZ <= 0.0f) roadZ = 0.0001f;

    const float z_near = 100.0f;
    const float zScale = 0.025f;

    float f = z_near * (baselineY - horizonY);

    const float eps = 1e-6f;
    float invz = 1.0f / (roadZ > eps ? roadZ : eps);
    float y = f * zScale / roadZ + horizonY;

    if (y < horizonY) {
      *outScreenX = -1000;
      *outScreenY = -1000;
      return;
    }

    float t = (y - horizonY) / (baselineY - horizonY);

    const float tInv = 1.0f - t;
    float centerOffset = tInv * tInv * roadXOffset;
    float x = SCREEN_CENTER_F + centerOffset;

    *outScreenX = x + roadX * t;
    *outScreenY = y;
    *scale = t;
}

void calculateRoadProperties(float y, float distance, float horizonY, float baselineY, float roadXOffset, float* x, float* w, int32_t* roadYOffset) {
  if (w == NULL || x == NULL || roadYOffset == NULL) return;
  if (baselineY <= horizonY) baselineY = horizonY + 1.0f;

  float t = (y - horizonY) / (baselineY - horizonY);
  if (t < 0.0f) t = 0.0f;
  if (t > 1.0f) t = 1.0f;

  *w = t;

  const float tInv = 1.0f - t;
  float centerOffset = tInv * tInv * roadXOffset;
  *x = SCREEN_CENTER_F + centerOffset;

  const float eps = 1e-4f;
  const float z_near = 100.0f;
  const float zScale = 0.025f;

  // focal length
  float f = z_near * (baselineY - horizonY);

  float dy = y - horizonY;
  if (dy < eps) dy = eps;
  float z = f / dy;

  *roadYOffset = (int32_t)floorf(distance + z * zScale);
}

/*
  Example road colors:
  
  RoadColors roadColors;
  roadColors.pavementA = 0x5aeb;
  roadColors.pavementB = 0x8410;
  roadColors.embankmentA = 0xf800;
  roadColors.embankmentB = 0xffff;
  roadColors.grassA = 0x94a0;
  roadColors.grassB = 0xce80;
  roadColors.ceilingA = 0x5aeb;
  roadColors.ceilingB = 0x8410;
  roadColors.wallA = 0xbdf7;
  roadColors.wallB = 0xce59;
  roadColors.railA = 0xbdf7;
  roadColors.railB = 0xce59;
  roadColors.lamp = 0xffff;
  roadColors.mountain = 0x7bef;
  roadColors.centerLine = 0xffff;
*/

static void drawRoadSurface(DISPLAY_T* display, float x, float y, float w, int32_t roadYOffset, RoadColors* colors, RoadDrawFlags* flags, RoadDimensions* roadDimensions) {
  float roadW = ceilf(roadDimensions->roadWidth*w);
  float halfRoadW = roadW*.5f;
  float embankmentW = ceilf(roadDimensions->embankmentWidth*w);
  float centerlineW = ceilf(roadDimensions->centerlineWidth*w);
  // Draw road surface
  display->drawFastHLine(x - halfRoadW, y, roadW, ((roadYOffset/6) % 2 ? colors->pavementA : colors->pavementB));
  // Draw embankment
  display->drawFastHLine(x - halfRoadW - embankmentW, y, embankmentW, (roadYOffset/3) % 2 ? colors->embankmentA: colors->embankmentB);
  display->drawFastHLine(x + halfRoadW, y, embankmentW, (roadYOffset/3) % 2 ? colors->embankmentA: colors->embankmentB);
  // Draw center line
  if (flags->drawCenterLine && (roadYOffset/4) % 2) {
    display->drawFastHLine(x - centerlineW*0.5f, y, centerlineW, colors->centerLine);
  }
}

void drawRaceOutdoor(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, float lastX, float lastW, RoadColors* colors, RoadDrawFlags* flags, RoadDimensions* roadDimensions) {
  drawRoadSurface(display, x, y, w, roadYOffset, colors, flags, roadDimensions);

  float railTopHeight = ceilf(roadDimensions->railingHeight*w);
  float railThickness = ceilf(roadDimensions->railingThickness*w);
  float lampHeight = ceilf(roadDimensions->lampHeight*w);
  float halfRoadWithEmbankmentW = (roadDimensions->embankmentWidth + roadDimensions->roadWidth*0.5f)*w;
  int32_t dxTotal = 5;//constrain(lastX - x, 0, 5)*2;
  display->drawFastHLine(0, y, x-halfRoadWithEmbankmentW, (roadYOffset/6) % 2 ? colors->grassA : colors->grassB);
  display->drawFastHLine(_max(0,x+halfRoadWithEmbankmentW), y, 320, (roadYOffset/6) % 2 ? colors->grassA : colors->grassB);

  if (flags->drawRails) {
    float centerToRailOffset = halfRoadWithEmbankmentW + roadDimensions->railingDistance*w;
    for (int8_t dx = 0; dx <= dxTotal; dx++) {
      display->drawFastVLine(x - centerToRailOffset + dx, y-railTopHeight, (roadYOffset % 12) == 6 ? railTopHeight : railThickness, (roadYOffset/6) % 2 ? colors->railA : colors->railB);
    }
    int32_t dxTotalR = constrain((lastX+lastW+lastW/5) - (x+w+w/5), -5, 0);
    for (int8_t dx = dxTotalR; dx <= 0; dx++) {
      display->drawFastVLine(x + centerToRailOffset + dx, y-railTopHeight, (roadYOffset % 12) == 6 ? railTopHeight : railThickness, (roadYOffset/6) % 2 ? colors->railA : colors->railB);
    }
  }

  // Draw lamp mast
  if (flags->drawLamps && ((roadYOffset) % 12) == 1) {
    for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
      display->drawFastVLine(x-w*0.2f + dx, y-lampHeight, lampHeight, (roadYOffset/6) % 2 ? colors->railA : colors->railB);
    }
    for (uint32_t i=0;i<(w*0.02);i++) {
      display->drawFastHLine(x-w*0.2f, y-lampHeight+i, w*(0.2f+0.5f+0.125f), (roadYOffset/6) % 2 ? colors->railA : colors->railB);
    }
  }

  // Draw lamp
  if (flags->drawLamps && ((roadYOffset) % 12) == 1) {
    for (uint32_t i=1;i<(w*0.05f)+1;i++) {
      display->drawFastHLine(x + w*(0.5f - 0.125f), y-lampHeight+i, w/4, colors->lamp);
    }
  }
}

static float distanceFromY(float y, float y_horizon, float y_baseline, float z_near, float scale) {
  if (y <= y_horizon) return z_near * (y_baseline - y_horizon) * scale;
  const float eps = 1e-4f;
  float dy = y - y_horizon;
  float f = z_near * (y_baseline - y_horizon);
  float z = f / ((dy < eps) ? eps : dy);
  return z * scale;
}

void drawRaceTunnel(DISPLAY_T* display, float x, int32_t y, float w, int32_t roadYOffset, float lastX, float lastW, RoadColors* colors, RoadDrawFlags* flags, RoadDimensions* roadDimensions) {
  drawRoadSurface(display, x, y, w, roadYOffset, colors, flags, roadDimensions);

  float wallHeight = w;

  // Draw roof
  display->drawFastHLine(x-w*0.2f, y-wallHeight, w*1.4f, (roadYOffset/6) % 2 ? colors->pavementA : colors->pavementB);

  if (flags->drawMountain) {
    for (int32_t mY = 0; mY<y-wallHeight; mY++) {
      display->drawFastHLine(0, mY, 320, colors->mountain);
    }
    for (int32_t mY = y-wallHeight; mY<y; mY++) {
      display->drawFastHLine(0, mY, x-w*0.2f, colors->mountain);
      display->drawFastHLine(x+w*1.2f, mY, 320, colors->mountain);
    }
  }

  // Draw wall
  int32_t dxTotal = constrain((lastX-lastW/5) - (x-w/5), -5, 5);
  if (dxTotal>=0) { // Only draw left wall if visible
    for (int32_t dx = 0; dx <= dxTotal; dx++) {
      display->drawFastVLine(x-w*0.2f + dx, y-wallHeight, wallHeight, (roadYOffset/6) % 2 ? colors->wallA : colors->wallB);
    }
  }
  dxTotal = constrain((lastX+lastW+lastW/5) - (x+w+w/5), -5, 5);
  if (dxTotal<=0) { // Only draw right wall if visible
    for (int32_t dx = dxTotal; dx <= 0; dx++) {
      display->drawFastVLine(x+w*1.2f + dx, y-wallHeight, wallHeight, (roadYOffset/6) % 2 ? colors->wallA : colors->wallB);
    }

    // Draw emergency escape sign
    if (flags->drawTunnelDoors && (roadYOffset % 40) == 0) {
      for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
        display->drawFastVLine(x+w*1.2f + dx, y-w*0.5f, w/10, 0x0540);
        display->drawPixel(x+w*1.2f + dx, y-w*0.5f, 0x0000);
        display->drawPixel(x+w*1.2f + dx, y-w*0.4f, 0x0000);
      }
    }

    // Draw escape door
    if (flags->drawTunnelDoors && (roadYOffset % 40) > 1 && (roadYOffset % 40) < 4) {
      for (int32_t dx = _min(dxTotal, 0); dx < _max(dxTotal + 1, 1); dx++) {
        display->drawFastVLine(x+w*1.2f + dx, y-w*0.5f, w*0.5f, 0x3166);
      }
    }
  }

  // Draw lamp light
  /*if (((roadYOffset/2) % 6) <= 2) {
    display->drawFastHLine(x + w*(0.5 - 0.25 - 0.03125), y-w+2, w*(0.5 + 0.0625), 0xffed);
  }*/

  //Draw lamp
  if (flags->drawLamps && ((roadYOffset/2) % 6) == 1) {
    for (uint32_t i=1;i<(w*0.05f)+1;i++) {
      display->drawFastHLine(x + w*(0.5f - 0.25f), y-wallHeight+i, w*0.5f, colors->lamp);
    }
  }
}
