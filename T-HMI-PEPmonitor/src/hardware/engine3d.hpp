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

#define MESH3D_MAX_VERTICES 8192
#define MESH3D_MAX_FACES 4096
#define WORLD3D_MAX_TRIANGLES 4096

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

#endif /*__ENGINE3D_H__*/
