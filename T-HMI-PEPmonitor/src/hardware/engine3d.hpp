#ifndef __ENGINE3D_H__
#define __ENGINE3D_H__
#include <Arduino.h>
#include "hardware/gfxHandler.hpp"
#include "hardware/matrix.h"

#define MESH3D_LIMIT 32
#define TEXTURE3D_LIMIT 16
#define ENTITY3D_LIMIT 128
#define LIGHT3D_LIMIT 8
#define CAMERA3D_LIMIT 4

#define MESH3D_MAX_VERTICES 24576
#define MESH3D_MAX_FACES 8192
#define WORLD3D_MAX_TRIANGLES 8192

#define LIGHT3D_TYPE_POINT 1
#define LIGHT3D_TYPE_SPOT 2
#define LIGHT3D_TYPE_DIRECTIONAL 4

void initWorld3D();
void resetWorld3D();

int16_t loadMesh3D(String path);
int16_t createMesh3DCube();
int16_t createMesh3DSphere(float radius, uint8_t segments);
int16_t createMesh3DPlane(float width, float height);
bool freeMesh3D(int16_t handle);
bool getMeshStats3D(int16_t handle, uint16_t* vertexCount, uint16_t* faceCount);
void setMeshColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b);
void setMeshTexture3D(int16_t meshHandle, int16_t textureHandle);
void setMeshFlipped3D(int16_t handle, bool flipped);

int16_t loadTexture3D(String path);
bool freeTexture3D(int16_t handle);

int16_t createEntity3D(int16_t meshHandle);
bool freeEntity3D(int16_t handle);
void positionEntity3D(int16_t handle, float x, float y, float z);
void rotateEntity3D(int16_t handle, float pitch, float yaw, float roll);
void turnEntity3D(int16_t handle, float pitch, float yaw, float roll);
void scaleEntity3D(int16_t handle, float sx, float sy, float sz);
void entityColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b);
void hideEntity3D(int16_t handle, bool hidden);
// Unlinks automatically when the parent is freed.
bool linkEntity3D(int16_t childHandle, int16_t parentHandle);
void entityOrder3D(int16_t handle, int8_t order);

// The camera with the lowest handle is the active one.
int16_t createCamera3D();
bool freeCamera3D(int16_t handle);
void positionCamera3D(int16_t handle, float x, float y, float z);
void rotateCamera3D(int16_t handle, float pitch, float yaw, float roll);
void turnCamera3D(int16_t handle, float pitch, float yaw, float roll);
void cameraZoom3D(int16_t handle, float zoom);
void cameraRange3D(int16_t handle, float nearPlane, float farPlane);

void ambientLight3D(uint8_t r, uint8_t g, uint8_t b);
int16_t createLight3D(uint8_t r, uint8_t g, uint8_t b, float x, float y, float z, float range, uint8_t type);
bool freeLight3D(int16_t handle);
void setLightDirection3D(int16_t handle, float dx, float dy, float dz);
void setLightCone3D(int16_t handle, float innerAngleDeg, float outerAngleDeg);

void updateWorld3D(DISPLAY_T* display);
void drawWorld3D(DISPLAY_T* display);

typedef struct {
  int32_t updateUs;
  int32_t drawUs;
  int32_t pushUs;
  int32_t sensorUs;
  int32_t frameUs;       // wall time between end-of-frame blits (pipelined: overlaps update+draw)
  int32_t xfUs;          // updateUs breakdown: vertex transform + lighting
  int32_t faceUs;        // updateUs breakdown: face cull/clip/draw-list submit
  int32_t bucketUs;      // updateUs breakdown: depth bucket sort
  uint32_t vertsTransformed;
  uint32_t trisSubmitted;
  uint32_t trisClipped;
  uint32_t spans;
  uint32_t pixels;
  int32_t gapUs;
  uint32_t gapPx;
  uint32_t frames;
} World3DPerf3D;

void setWorld3DDebug3D(bool enabled);
void getWorld3DPerf3D(World3DPerf3D* out);
void resetWorld3DPerf3D();
// Call once per rendered frame with the time the display push took, in microseconds.
void reportWorld3DFrame3D(int32_t pushUs);

// quiesce waits out an in-flight pipelined push before code that writes the TFT directly.
void blitWorld3DFrame3D(DISPLAY_T* display);
void quiesceWorld3DBlit3D();
void setWorld3DPipeline3D(bool enabled);
void setWorld3DI80Push3D(bool enabled);
void setWorld3DSplitXf3D(bool enabled);
// Frame start in place of fillSprite(TFT_BLACK); in mask mode this only clears
// the coverage mask and the core 0 blit task gap-fills the uncovered pixels at
// push time. Mask mode only applies once the 3D world has been drawn.
void beginWorld3DFill3D(DISPLAY_T* display);
// Writers that do not go through begin/barrier (HUD code, Lua draw wrappers)
// call this before touching the frame buffer so the gap fill neither leaves
// stale pixels in the region nor erases their content.
void barrierWorld3DFill3D();
void claimFramebufferRect3D(DISPLAY_T* display, int16_t x, int16_t y, int16_t w, int16_t h);
void claimTextRect3D(DISPLAY_T* display, const char* text, int16_t x, int16_t y);
// False when the display is not mask-mode and the caller must fill the sprite itself.
bool fillFramebufferMasked3D(DISPLAY_T* display, uint16_t color);
bool world3DMaskMode3D(DISPLAY_T* display);
// Projection scale stays tied to the full display height, so the viewport only
// crops/shifts the image; default is the whole display.
void setWorld3DViewport3D(int16_t x, int16_t y, int16_t w, int16_t h);
void addWorld3DSensorUs3D(int32_t us);

#endif /*__ENGINE3D_H__*/
