#ifndef __ENGINE3D_H__
#define __ENGINE3D_H__
#include <Arduino.h>
#include "hardware/gfxHandler.hpp"
#include "hardware/matrix.h"

/*
 * Software 3D engine for the Lua game engine, modelled after the Blitz3D
 * function reference. Meshes, textures, entities, cameras and lights are
 * referenced by integer handles, just like the 2D sprite handles.
 */

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

// Meshes: loaded from .obj files (with optional .mtl map_Kd texture) or procedural primitives
int16_t loadMesh3D(String path);
int16_t createMesh3DCube();
int16_t createMesh3DSphere(float radius, uint8_t segments);
int16_t createMesh3DPlane(float width, float height);
bool freeMesh3D(int16_t handle);
bool getMeshStats3D(int16_t handle, uint16_t* vertexCount, uint16_t* faceCount);
void setMeshColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b);
void setMeshTexture3D(int16_t meshHandle, int16_t textureHandle);
void setMeshFlipped3D(int16_t handle, bool flipped);

// Textures (BMP files, same loader as the 2D sprites)
int16_t loadTexture3D(String path);
bool freeTexture3D(int16_t handle);

// Entities: instances of a mesh with position/rotation/scale/color
int16_t createEntity3D(int16_t meshHandle);
bool freeEntity3D(int16_t handle);
void positionEntity3D(int16_t handle, float x, float y, float z);
void rotateEntity3D(int16_t handle, float pitch, float yaw, float roll);
void turnEntity3D(int16_t handle, float pitch, float yaw, float roll);
void scaleEntity3D(int16_t handle, float sx, float sy, float sz);
void entityColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b);
void hideEntity3D(int16_t handle, bool hidden);
bool linkEntity3D(int16_t childHandle, int16_t parentHandle);
void entityOrder3D(int16_t handle, int8_t order);

// Cameras: the camera with the lowest handle is the active one unless freed
int16_t createCamera3D();
bool freeCamera3D(int16_t handle);
void positionCamera3D(int16_t handle, float x, float y, float z);
void rotateCamera3D(int16_t handle, float pitch, float yaw, float roll);
void turnCamera3D(int16_t handle, float pitch, float yaw, float roll);
void cameraZoom3D(int16_t handle, float zoom);
void cameraRange3D(int16_t handle, float nearPlane, float farPlane);

// Lights
void ambientLight3D(uint8_t r, uint8_t g, uint8_t b);
int16_t createLight3D(uint8_t r, uint8_t g, uint8_t b, float x, float y, float z, float range, uint8_t type);
bool freeLight3D(int16_t handle);
void setLightDirection3D(int16_t handle, float dx, float dy, float dz);
void setLightCone3D(int16_t handle, float innerAngleDeg, float outerAngleDeg);

// Rendering: transforms, culls, sorts and lights every face into an internal draw list...
void updateWorld3D(DISPLAY_T* display);
// ...and rasterizes it with Gouraud shading and optional affine texture mapping
void drawWorld3D(DISPLAY_T* display);

// Perf instrumentation for the render pipeline (see updateWorld3D/drawWorld3D)
typedef struct {
  int32_t updateUs;      // microseconds spent transforming/lighting/sorting last frame
  int32_t drawUs;        // microseconds spent rasterizing last frame
  int32_t pushUs;        // microseconds spent pushing the frame buffer to the TFT
  int32_t xfUs;          // updateUs breakdown: vertex transform + projection + lighting
  int32_t faceUs;        // updateUs breakdown: face cull/clip/draw-list submit
  int32_t bucketUs;      // updateUs breakdown: depth bucket sort
  uint32_t vertsTransformed;
  uint32_t trisSubmitted;
  uint32_t trisClipped;
  uint32_t spans;        // scanline spans filled
  uint32_t pixels;       // pixels written by the rasterizer
  uint32_t frames;       // frames reported since the last reset
} World3DPerf3D;

void setWorld3DDebug3D(bool enabled);
void getWorld3DPerf3D(World3DPerf3D* out);
void resetWorld3DPerf3D();
// Call once per rendered frame with the time the display push took, in microseconds
void reportWorld3DFrame3D(int32_t pushUs);

// End-of-frame display push: pipelines the blit to core 0 when the frame drew
// 3D into a double-buffered sprite, otherwise the synchronous sprite push.
// quiesce waits out an in-flight push before code that writes the TFT directly.
void blitWorld3DFrame3D(DISPLAY_T* display);
void quiesceWorld3DBlit3D();

#endif /*__ENGINE3D_H__*/
