#include "engine3d.hpp"
#include "hardware/sdHandler.h"
#include <esp_cache.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

/*
 * Software 3D engine for the Lua game engine, modelled after the Blitz3D
 * function reference.
 *
 * Pipeline: entities are transformed to view space, per-vertex Gouraud lighting
 * is evaluated against ambient/point/spot/directional lights, faces are frustum
 * and backface culled, clipped against the near plane, bucket sorted by depth
 * (painter's algorithm) and finally rasterized with integer span filling.
 *
 * View space convention: +x right, +y up on screen, +z forward (into the scene).
 * Faces are front facing when the cross product of their winding points towards
 * the camera, which matches the standard counter-clockwise winding of OBJ files.
 *
 * Screen coordinates are Q8 fixed point, texture coordinates are Q16 fixed point.
 * All hot loops use hardware float for transforms and int32 for rasterizing.
 */

#define BUCKET3D_COUNT 4096
#define DEG3D_TO_RAD 0.01745329252f
// Vertical field of view at CameraZoom 1 is 60 degrees: 0.5/tan(30deg)
#define PROJ3D_FACTOR 0.8660254f
#define SPOT3D_DEFAULT_INNER_DEG 12.0f
#define SPOT3D_DEFAULT_OUTER_DEG 18.0f
#define LIGHT3D_MAX_RANGE 1000000000000.0f

typedef struct { float m[9]; } Mat3;

typedef struct {
  uint16_t a, b, c;
} Face3D;

/*
 * Per-vertex view-space scratch in one 32-byte record. The update loop writes
 * it as a single PSRAM stream and the face loop's random vertex access then
 * needs one cache line per vertex instead of one per scratch array: line
 * transfers to PSRAM dominate both phases on the ESP32-S3.
 */
typedef struct {
  float x, y, z;     // view-space position
  float iz;          // 1/viewZ (fastRecip3D), reused by the face loop for texture coords
  int32_t sx, sy;    // Q8 screen projection, valid when z >= near plane
  uint32_t color;    // Gouraud color 0x00RRGGBB
  uint32_t pad;      // 28-byte records straddle 64-byte PSRAM cache lines, so every
                     // write miss fetches the line before updating it; padding to 32
                     // tiles records exactly into lines and removes that read.
} ViewVertex3D;

/* Stored vertex record: position and normal in one record so the update
 * loop streams both from the same PSRAM cache lines per vertex instead of
 * following two separate arrays. */
typedef struct {
  Vector3D pos;
  Vector3D nrm;
} MeshVertex3D;

typedef struct {
  bool inUse;
  uint16_t vertexCount;
  uint16_t faceCount;
  MeshVertex3D* vtx;
  int32_t* uvQ; // Q16 uv pairs, null when the mesh has no texture coords
  Face3D* faces;
  uint8_t color[3];
  int16_t textureHandle;
  uint8_t flipped;
  Vector3D boundsCenter;
  float boundsRadius;
  ViewVertex3D* vs; // per-frame scratch, sized to vertexCount
} Mesh3D;

typedef struct {
  bool inUse;
  const uint16_t* pixels;
  int16_t w;
  int16_t h;
  int16_t mask; // w-1 when w == h and w is a power of two, else -1
} Texture3D;

typedef struct {
  bool inUse;
  int16_t meshHandle;
  int16_t parentHandle;
  float x, y, z;
  float pitch, yaw, roll;
  float sx, sy, sz;
  uint8_t color[3];
  uint8_t hidden;
  int8_t order;
} Entity3D;

typedef struct {
  bool inUse;
  float x, y, z;
  float pitch, yaw, roll;
  float zoom, nearPlane, farPlane;
} Camera3D;

typedef struct {
  bool inUse;
  uint8_t type;
  float r, g, b;        // 0..1
  float x, y, z;
  float range, invRange, rangeSq; // range<=0 means infinite range
  float dx, dy, dz;     // normalized beam direction (spot and directional)
  float cosInner, cosOuter, cosInvDelta;
  // View space caches, refreshed every updateWorld3D()
  float vx, vy, vz;     // position
  float bx, by, bz;     // beam direction
  float tx, ty, tz;     // direction towards the light (directional lights)
} Light3D;

typedef struct {
  int32_t sx[4], sy[4];   // Q8 screen coordinates
  float ru[4], rv[4];     // Q16 texture coords times 1/viewZ (screen-linear)
  float rz[4];            // 1/viewZ; dividing ru/rz gives perspective-correct uv
  uint32_t color[4];      // per-vertex Gouraud color, 0x00RRGGBB
  const uint16_t* tex;    // null unless textured
  int16_t texW, texH, texMask;
  int16_t next;           // next triangle in the depth bucket
  float zKey;             // average camera-space depth; bucketed in a second pass
  int8_t orderOff;        // entity draw-order bias applied at bucketing time
  uint32_t flatColor;     // uniform color for modes 0 and 3, 0x00RRGGBB
  uint8_t vertexCount;    // 3 normally, 4 after near plane clipping
  uint8_t mode;           // 0 flat, 1 gouraud, 2 gouraud+texture, 3 texture uniform light
} TriDraw3D;

/*
 * Splitting the xf pass over both cores shares these arrays with a core 0
 * helper task. The S3's per-core data caches are not coherent and
 * esp_cache_msync's M2C (invalidate) direction only accepts ranges aligned
 * exactly to cache lines, so everything the split hand-off touches lives in
 * one 64-byte-aligned block whose total size is a whole number of cache
 * lines: a single msync leg per hand-off then cannot bleed into unrelated
 * core-1-dirtied static data. See the split section near updateWorld3D.
 */
typedef struct {
  Mat3 basis;
  float camX, camY, camZ;
  float nearPlane, farPlane;
  float projScale, vpCx, vpCy, tanHalfX, tanHalfY;
  float ambient[3];
  int8_t activeLights[LIGHT3D_LIMIT];
  int8_t activeLightCount;
  bool dynamicLighting;
} XfFrameCtx3D;

typedef struct {
  int32_t verts;
  uint64_t culledLo, culledHi;
} XfHelperResult3D;

typedef struct __attribute__((aligned(64))) {
  XfFrameCtx3D ctx;
  XfHelperResult3D res;
  ViewVertex3D* scratch[ENTITY3D_LIMIT];
  Mesh3D meshes[MESH3D_LIMIT];
  Entity3D entities[ENTITY3D_LIMIT];
  Light3D lights[LIGHT3D_LIMIT];
} W3dSplitShared3D;
static W3dSplitShared3D w3dSplitShared3D;
static_assert(sizeof(W3dSplitShared3D) % 64 == 0 && alignof(W3dSplitShared3D) % 64 == 0,
              "split hand-off block must span whole 64-byte cache lines");

#define meshes3D (w3dSplitShared3D.meshes)
#define entities3D (w3dSplitShared3D.entities)
#define lights3D (w3dSplitShared3D.lights)
static Texture3D textures3D[TEXTURE3D_LIMIT];
static Camera3D cameras3D[CAMERA3D_LIMIT];

// Scratch and vtx allocations are rounded up to whole 64-byte cache lines so
// every split msync leg on them is line-exact (see the split section near
// updateWorld3D).
static inline size_t viewVertsSpan3D(uint16_t vc) {
  return ((size_t)vc * sizeof(ViewVertex3D) + 63) & ~(size_t)63;
}
static inline size_t meshVtxSpan3D(uint16_t vc) {
  return ((size_t)vc * sizeof(MeshVertex3D) + 63) & ~(size_t)63;
}
static TFT_eSprite* textureSprites3D = nullptr;
static TriDraw3D* drawList3D = nullptr;
static int16_t* bucketHeads3D = nullptr;
static int32_t drawCount3D = 0;
static float ambient3D[3] = {0.5f, 0.5f, 0.5f};
static bool world3DReady = false;
// True while frames are rendered through drawWorld3D: enables the core 0
// background blanking described in the pipelined blit section further down.
static bool w3d3DFrameActive3D = false;
// Slot 0 starts as an implicit camera so meshes render before the game makes
// one; the first CreateCamera replaces it so game camera settings take effect.
static bool implicitCamera3D = false;

static uint16_t* fb3D = nullptr;
static int32_t fbW3D = 0, fbH3D = 0;

// Mask-mode coverage bookkeeping (design: pipelined blit section further down)
static bool w3dMaskActive3D = false;
static uint32_t* w3dDrawMask3D = nullptr; // mask of the current draw target

/* Render viewport: a sub-rectangle of the display that UpdateWorld/DrawWorld
 * project into. The camera axis hits the viewport center, and the projection
 * scale stays tied to the full display height, so the world image is the same
 * one the full-screen projection produces, just windowed: moving a viewport
 * of unchanged size shifts the picture by the same amount. Anything outside
 * the viewport is neither projected, depth-sorted, nor rasterized; the
 * coverage-mask gap fill blacks those rows like any other untouched pixel. */
static int16_t vpX3D = 0, vpY3D = 0, vpW3D = 0, vpH3D = 0; // requested; w/h 0 = whole display
static int16_t vpL3D = 0, vpT3D = 0, vpR3D = 0, vpB3D = 0; // resolved, right/bottom exclusive
static float vpCx3D = 0, vpCy3D = 0;                       // resolved center

void setWorld3DViewport3D(int16_t x, int16_t y, int16_t w, int16_t h) {
  vpX3D = x;
  vpY3D = y;
  vpW3D = w;
  vpH3D = h;
}

static void resolveViewport3D(int32_t sw, int32_t sh) {
  int32_t x = vpX3D, y = vpY3D, w = vpW3D, h = vpH3D;
  if (w <= 0 || h <= 0) { x = 0; y = 0; w = sw; h = sh; }
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x >= sw) x = sw - 1;
  if (y >= sh) y = sh - 1;
  if (x + w > sw) w = sw - x;
  if (y + h > sh) h = sh - y;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  vpL3D = x;
  vpT3D = y;
  vpR3D = x + w;
  vpB3D = y + h;
  vpCx3D = x + w * 0.5f;
  vpCy3D = y + h * 0.5f;
}

// Phase timings and workload counters. They only cost anything while the
// debugLogRender3D system config flag turns them on; the game loop reports the
// frame through reportWorld3DFrame3D() once the sprite has been pushed.
static bool perf3DEnabled = false;
static World3DPerf3D perf3D = {};
static World3DPerf3D perf3DReportBase3D = {};
static uint32_t perf3DReportMs3D = 0;
static int64_t w3dPerfFrameT3D = 0;

// Blit mode reported in the perf line ("full" pipelined, "sync" diff push)
static const char* w3dBlitMode3D = "sync";

static void setupDefaultCamera3D() {
  memset(cameras3D, 0, sizeof(cameras3D));
  cameras3D[0].inUse = true;
  implicitCamera3D = true;
  cameras3D[0].x = 0;
  cameras3D[0].y = 0;
  cameras3D[0].z = -100;
  cameras3D[0].zoom = 1.0f;
  cameras3D[0].nearPlane = 1.0f;
  cameras3D[0].farPlane = 5000.0f;
}

static bool ensureWorld3D() {
  if (world3DReady) return true;
  drawList3D = (TriDraw3D*)heap_caps_malloc(sizeof(TriDraw3D) * WORLD3D_MAX_TRIANGLES, MALLOC_CAP_SPIRAM);
  bucketHeads3D = (int16_t*)heap_caps_malloc(sizeof(int16_t) * BUCKET3D_COUNT, MALLOC_CAP_SPIRAM);
  textureSprites3D = (TFT_eSprite*)heap_caps_malloc(sizeof(TFT_eSprite) * TEXTURE3D_LIMIT, MALLOC_CAP_SPIRAM);
  if (!drawList3D || !bucketHeads3D || !textureSprites3D) {
    Serial.println("engine3d: failed to allocate world memory in PSRAM!");
    return false;
  }
  for (int16_t i = 0; i < TEXTURE3D_LIMIT; i++) {
    new (&textureSprites3D[i]) TFT_eSprite(&tft);
  }
  setupDefaultCamera3D();
  world3DReady = true;
  return true;
}

static void* grow3D(void* ptr, size_t newBytes) {
  return heap_caps_realloc(ptr, newBytes, MALLOC_CAP_SPIRAM);
}

static void mat3Mul3D(const Mat3* a, const Mat3* b, Mat3* out) {
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      out->m[i*3+j] = a->m[i*3]*b->m[j] + a->m[i*3+1]*b->m[3+j] + a->m[i*3+2]*b->m[6+j];
    }
  }
}

// Rotation local->world: yaw about +y, then pitch about +x (positive pitch looks up),
// then roll about +z. All angles in degrees.
static void mat3FromEuler3D(float pitchDeg, float yawDeg, float rollDeg, Mat3* out) {
  float p = pitchDeg * DEG3D_TO_RAD;
  float y = yawDeg * DEG3D_TO_RAD;
  float r = rollDeg * DEG3D_TO_RAD;
  float cy = cosf(y), sy = sinf(y);
  float cp = cosf(p), sp = sinf(p);
  float cr = cosf(r), sr = sinf(r);
  float x00 = cr,   x01 = -sr,  x02 = 0;
  float x10 = cp*sr, x11 = cp*cr, x12 = sp;
  float x20 = -sp*sr, x21 = -sp*cr, x22 = cp;
  out->m[0] = cy*x00 + sy*x20;
  out->m[1] = cy*x01 + sy*x21;
  out->m[2] = cy*x02 + sy*x22;
  out->m[3] = x10;
  out->m[4] = x11;
  out->m[5] = x12;
  out->m[6] = -sy*x00 + cy*x20;
  out->m[7] = -sy*x01 + cy*x21;
  out->m[8] = -sy*x02 + cy*x22;
}

static void mat3Transpose3D(const Mat3* a, Mat3* out) {
  out->m[0] = a->m[0]; out->m[1] = a->m[3]; out->m[2] = a->m[6];
  out->m[3] = a->m[1]; out->m[4] = a->m[4]; out->m[5] = a->m[7];
  out->m[6] = a->m[2]; out->m[7] = a->m[5]; out->m[8] = a->m[8];
}

static inline uint16_t rgbTo5653D(uint32_t rgb) {
  uint32_t r = (rgb >> 16) & 0xFF;
  uint32_t g = (rgb >> 8) & 0xFF;
  uint32_t b = rgb & 0xFF;
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static inline void unpackColor3D(uint32_t c, int32_t* r, int32_t* g, int32_t* b) {
  *r = (int32_t)((c >> 16) & 0xFF) << 8;
  *g = (int32_t)((c >> 8) & 0xFF) << 8;
  *b = (int32_t)(c & 0xFF) << 8;
}

static inline int32_t clamp3DI(int32_t v, int32_t lo, int32_t hi) {
  if (v < lo) return lo;
  if (v > hi) return hi;
  return v;
}

/*
 * The ESP32-S3 FPU runs add/sub/mul but has no divide or square root instruction,
 * so 1.0f/x and sqrtf() compile to library calls (__divsf3 and sqrtf relocations
 * are visible in this file's own firmware object). Those calls dominated the hot
 * loops, hence the bit-hack seed plus Newton iterations below: the hot path stays
 * entirely in FPU instructions (wfr + mul.s/msub.s in the firmware disassembly).
 * The bit view uses a union pun because the memcpy idiom is not equivalent on this
 * target — the Xtensa -Os compiler emits real memcpy calls for it. Measured
 * worst-case relative error over 0.001..3000 is
 * 1.6e-6 for the reciprocal and 4.7e-6 for the reciprocal square root, i.e. under
 * 0.001 px of screen movement and under 0.01 texels — below RGB565 resolution.
 */
union FloatBits32 { float f; uint32_t u; };

static inline float fastRecip3D(float x) {
  FloatBits32 b;
  b.f = x;
  uint32_t exponent = (b.u >> 23) & 0xFF;
  if (exponent == 0 || exponent >= 254) return 1.0f / x; // zero, denormal, inf, nan
  b.u = 0x7ED00000u - b.u;
  float y = b.f;
  y *= 2.0f - x * y;
  y *= 2.0f - x * y;
  y *= 2.0f - x * y;
  return y;
}

static inline float fastInvSqrt3D(float x) {
  FloatBits32 b;
  b.f = x;
  uint32_t exponent = (b.u >> 23) & 0xFF;
  if (exponent == 0 || exponent >= 254) return 1.0f / sqrtf(x);
  b.u = 0x5F3759DFu - (b.u >> 1);
  float y = b.f;
  float half = 0.5f * x;
  y *= 1.5f - half * y * y;
  y *= 1.5f - half * y * y;
  return y;
}

static void freeMeshArrays3D(Mesh3D* m) {
  if (m->vtx) free(m->vtx);
  if (m->uvQ) free(m->uvQ);
  if (m->faces) free(m->faces);
  if (m->vs) free(m->vs);
  memset(m, 0, sizeof(Mesh3D));
}

static int16_t allocMeshSlot3D() {
  for (int16_t i = 0; i < MESH3D_LIMIT; i++) {
    if (!meshes3D[i].inUse) {
      Mesh3D* m = &meshes3D[i];
      memset(m, 0, sizeof(Mesh3D));
      m->inUse = true;
      m->textureHandle = -1;
      m->color[0] = 255; m->color[1] = 255; m->color[2] = 255;
      return i;
    }
  }
  Serial.println("engine3d: no free mesh slot!");
  return -1;
}

static void normalizeNormals3D(Mesh3D* m, const uint8_t* hasNrm) {
  if (!m->vtx) return;
  // Faces must contribute their normal to merged vertices that have no OBJ normal
  if (hasNrm && m->faces && m->vtx) {
    for (uint16_t fi = 0; fi < m->faceCount; fi++) {
      Face3D* f = &m->faces[fi];
      if (hasNrm[f->a] && hasNrm[f->b] && hasNrm[f->c]) continue;
      Vector3D* p0 = &m->vtx[f->a].pos;
      Vector3D* p1 = &m->vtx[f->b].pos;
      Vector3D* p2 = &m->vtx[f->c].pos;
      float e1x = p1->x - p0->x, e1y = p1->y - p0->y, e1z = p1->z - p0->z;
      float e2x = p2->x - p0->x, e2y = p2->y - p0->y, e2z = p2->z - p0->z;
      float nx = e1y*e2z - e1z*e2y;
      float ny = e1z*e2x - e1x*e2z;
      float nz = e1x*e2y - e1y*e2x;
      if (!hasNrm[f->a]) { m->vtx[f->a].nrm.x += nx; m->vtx[f->a].nrm.y += ny; m->vtx[f->a].nrm.z += nz; }
      if (!hasNrm[f->b]) { m->vtx[f->b].nrm.x += nx; m->vtx[f->b].nrm.y += ny; m->vtx[f->b].nrm.z += nz; }
      if (!hasNrm[f->c]) { m->vtx[f->c].nrm.x += nx; m->vtx[f->c].nrm.y += ny; m->vtx[f->c].nrm.z += nz; }
    }
  }
  for (uint16_t vi = 0; vi < m->vertexCount; vi++) {
    Vector3D* n = &m->vtx[vi].nrm;
    float len = sqrtf(n->x*n->x + n->y*n->y + n->z*n->z);
    if (len > 1e-7f) {
      float inv = 1.0f / len;
      n->x *= inv; n->y *= inv; n->z *= inv;
    } else {
      n->x = 0; n->y = 1; n->z = 0;
    }
  }
}

static bool buildMeshScratch3D(Mesh3D* m) {
  uint16_t vc = m->vertexCount;
  if (vc == 0) return false;
  float minX = 1e30f, minY = 1e30f, minZ = 1e30f;
  float maxX = -1e30f, maxY = -1e30f, maxZ = -1e30f;
  for (uint16_t vi = 0; vi < vc; vi++) {
    Vector3D* p = &m->vtx[vi].pos;
    if (p->x < minX) minX = p->x;
    if (p->y < minY) minY = p->y;
    if (p->z < minZ) minZ = p->z;
    if (p->x > maxX) maxX = p->x;
    if (p->y > maxY) maxY = p->y;
    if (p->z > maxZ) maxZ = p->z;
  }
  m->boundsCenter.x = (minX + maxX) * 0.5f;
  m->boundsCenter.y = (minY + maxY) * 0.5f;
  m->boundsCenter.z = (minZ + maxZ) * 0.5f;
  float radius = 0;
  for (uint16_t vi = 0; vi < vc; vi++) {
    Vector3D* p = &m->vtx[vi].pos;
    float dx = p->x - m->boundsCenter.x;
    float dy = p->y - m->boundsCenter.y;
    float dz = p->z - m->boundsCenter.z;
    float d2 = dx*dx + dy*dy + dz*dz;
    if (d2 > radius) radius = d2;
  }
  m->boundsRadius = sqrtf(radius);
  m->vs = (ViewVertex3D*)heap_caps_aligned_alloc(64, viewVertsSpan3D(vc), MALLOC_CAP_SPIRAM);
  if (!m->vs) return false;
  return true;
}
// ---------------------------------------------------------------------------
// OBJ loading
// ---------------------------------------------------------------------------

typedef struct {
  int32_t vi, vti, vni;
  int32_t out; // merged vertex index, -1 when the slot is free
} HashEntry3D;

typedef struct {
  Mesh3D* mesh;
  float* rawV; uint32_t rawVCount, rawVCap;
  float* rawT; uint32_t rawTCount, rawTCap;
  float* rawN; uint32_t rawNCount, rawNCap;
  MeshVertex3D* vtx; float* uvs; uint8_t* hasNrm;
  uint32_t mergedCount, mergedCap;
  Face3D* faces; uint32_t faceCount, faceCap;
  HashEntry3D* hash; uint32_t hashCap, hashCount;
  int32_t* cornerBuf; uint32_t cornerCap; // scratch corner list for one face record
  bool hasUV;
  bool overflowWarned;
} ObjLoad3D;

static bool growRaw3D(float** arr, uint32_t* cap, uint32_t neededFloats) {
  if (neededFloats <= *cap) return true;
  uint32_t newCap = (*cap == 0) ? 256 : *cap * 2;
  while (newCap < neededFloats) newCap *= 2;
  float* np = (float*)grow3D(*arr, newCap * sizeof(float));
  if (!np) return false;
  *arr = np;
  *cap = newCap;
  return true;
}

static uint32_t hashKey3D(int32_t vi, int32_t vti, int32_t vni) {
  return ((uint32_t)vi * 73856093u) ^ ((uint32_t)vti * 19349663u) ^ ((uint32_t)vni * 83492791u);
}

static bool ensureHash3D(ObjLoad3D* ctx) {
  if (ctx->hash && ctx->hashCount * 2 < ctx->hashCap) return true;
  uint32_t newCap = ctx->hashCap ? ctx->hashCap * 2 : 1024;
  HashEntry3D* newTable = (HashEntry3D*)grow3D(nullptr, newCap * sizeof(HashEntry3D));
  if (!newTable) return false;
  for (uint32_t i = 0; i < newCap; i++) {
    newTable[i].vi = INT32_MIN;
    newTable[i].out = -1;
  }
  if (ctx->hash) {
    uint32_t mask = newCap - 1;
    for (uint32_t i = 0; i < ctx->hashCap; i++) {
      HashEntry3D* e = &ctx->hash[i];
      if (e->vi != INT32_MIN) {
        uint32_t h = hashKey3D(e->vi, e->vti, e->vni);
        while (newTable[h & mask].vi != INT32_MIN) h++;
        newTable[h & mask] = *e;
      }
    }
    free(ctx->hash);
  }
  ctx->hash = newTable;
  ctx->hashCap = newCap;
  return true;
}

static HashEntry3D* hashSlot3D(ObjLoad3D* ctx, int32_t vi, int32_t vti, int32_t vni) {
  if (!ensureHash3D(ctx)) return nullptr;
  uint32_t mask = ctx->hashCap - 1;
  uint32_t h = hashKey3D(vi, vti, vni);
  while (true) {
    HashEntry3D* e = &ctx->hash[h & mask];
    if (e->vi == INT32_MIN) return e;
    if (e->vi == vi && e->vti == vti && e->vni == vni) return e;
    h++;
  }
}

static int32_t getMergedVertex3D(ObjLoad3D* ctx, int32_t vi, int32_t vti, int32_t vni) {
  if (vi < 0 || vi * 3 + 2 >= (int32_t)ctx->rawVCount) return -1;
  HashEntry3D* slot = hashSlot3D(ctx, vi, vti, vni);
  if (!slot) return -1;
  if (slot->vi != INT32_MIN) return slot->out;
  if (ctx->mergedCount >= MESH3D_MAX_VERTICES) {
    if (!ctx->overflowWarned) {
      ctx->overflowWarned = true;
      Serial.println("engine3d: mesh exceeds MESH3D_MAX_VERTICES, model truncated!");
    }
    return -1;
  }
  if (ctx->mergedCount >= ctx->mergedCap) {
    uint32_t newCap = ctx->mergedCap ? ctx->mergedCap * 2 : 64;
    MeshVertex3D* nvtx = (MeshVertex3D*)grow3D(ctx->vtx, newCap * sizeof(MeshVertex3D));
    float* nuvs = (float*)grow3D(ctx->uvs, newCap * 2 * sizeof(float));
    uint8_t* nnrmF = (uint8_t*)grow3D(ctx->hasNrm, newCap);
    if (!nvtx || !nuvs || !nnrmF) return -1;
    ctx->vtx = nvtx; ctx->uvs = nuvs; ctx->hasNrm = nnrmF;
    ctx->mergedCap = newCap;
  }
  uint32_t idx = ctx->mergedCount;
  slot->vi = vi; slot->vti = vti; slot->vni = vni; slot->out = (int32_t)idx;
  ctx->hashCount++;
  ctx->vtx[idx].pos.x = ctx->rawV[vi*3];
  ctx->vtx[idx].pos.y = ctx->rawV[vi*3+1];
  ctx->vtx[idx].pos.z = ctx->rawV[vi*3+2];
  if (vni >= 0 && vni * 3 + 2 < (int32_t)ctx->rawNCount) {
    ctx->vtx[idx].nrm.x = ctx->rawN[vni*3];
    ctx->vtx[idx].nrm.y = ctx->rawN[vni*3+1];
    ctx->vtx[idx].nrm.z = ctx->rawN[vni*3+2];
    ctx->hasNrm[idx] = 1;
  } else {
    ctx->vtx[idx].nrm.x = 0; ctx->vtx[idx].nrm.y = 0; ctx->vtx[idx].nrm.z = 0;
    ctx->hasNrm[idx] = 0;
  }
  if (vti >= 0 && vti * 2 + 1 < (int32_t)ctx->rawTCount) {
    // OBJ v origin is bottom left, texture rows start at the top
    ctx->uvs[idx*2] = ctx->rawT[vti*2];
    ctx->uvs[idx*2+1] = 1.0f - ctx->rawT[vti*2+1];
    ctx->hasUV = true;
  } else {
    ctx->uvs[idx*2] = 0; ctx->uvs[idx*2+1] = 0;
  }
  ctx->mergedCount++;
  return (int32_t)idx;
}

static void addFace3D(ObjLoad3D* ctx, uint16_t a, uint16_t b, uint16_t c) {
  if (ctx->faceCount >= MESH3D_MAX_FACES) {
    if (!ctx->overflowWarned) {
      ctx->overflowWarned = true;
      Serial.println("engine3d: mesh exceeds MESH3D_MAX_FACES, model truncated!");
    }
    return;
  }
  if (ctx->faceCount >= ctx->faceCap) {
    uint32_t newCap = ctx->faceCap ? ctx->faceCap * 2 : 64;
    Face3D* nf = (Face3D*)grow3D(ctx->faces, newCap * sizeof(Face3D));
    if (!nf) return;
    ctx->faces = nf;
    ctx->faceCap = newCap;
  }
  ctx->faces[ctx->faceCount].a = a;
  ctx->faces[ctx->faceCount].b = b;
  ctx->faces[ctx->faceCount].c = c;
  ctx->faceCount++;
}

static char* nextObjToken3D() {
  return strtok(NULL, " \t\r\n");
}

static bool readObjFloat3D(char** tok, float* out) {
  if (!*tok) return false;
  *out = strtof(*tok, nullptr);
  *tok = nextObjToken3D();
  return true;
}

static void parseObj3D(ObjLoad3D* ctx, char* buf, char* mtllibName) {
  char* token = strtok(buf, " \t\r\n");
  while (token) {
    if (token[0] == 'v') {
      if (token[1] == 0) {
        token = nextObjToken3D();
        float x, y, z;
        bool ok = readObjFloat3D(&token, &x);
        ok = readObjFloat3D(&token, &y) && ok;
        ok = readObjFloat3D(&token, &z) && ok;
        if (ok && growRaw3D(&ctx->rawV, &ctx->rawVCap, ctx->rawVCount + 3)) {
          ctx->rawV[ctx->rawVCount++] = x;
          ctx->rawV[ctx->rawVCount++] = y;
          ctx->rawV[ctx->rawVCount++] = z;
        }
      } else if (token[1] == 't') {
        token = nextObjToken3D();
        float u, v;
        bool ok = readObjFloat3D(&token, &u);
        ok = readObjFloat3D(&token, &v) && ok;
        if (ok && growRaw3D(&ctx->rawT, &ctx->rawTCap, ctx->rawTCount + 2)) {
          ctx->rawT[ctx->rawTCount++] = u;
          ctx->rawT[ctx->rawTCount++] = v;
        }
      } else if (token[1] == 'n') {
        token = nextObjToken3D();
        float x, y, z;
        bool ok = readObjFloat3D(&token, &x);
        ok = readObjFloat3D(&token, &y) && ok;
        ok = readObjFloat3D(&token, &z) && ok;
        if (ok && growRaw3D(&ctx->rawN, &ctx->rawNCap, ctx->rawNCount + 3)) {
          ctx->rawN[ctx->rawNCount++] = x;
          ctx->rawN[ctx->rawNCount++] = y;
          ctx->rawN[ctx->rawNCount++] = z;
        }
      } else {
        token = nextObjToken3D(); // vc, vp and other unsupported records
      }
    } else if (token[0] == 'f' && token[1] == 0) {
      token = nextObjToken3D();
      int32_t* corners = ctx->cornerBuf;
      uint32_t cornerCap = ctx->cornerCap;
      uint32_t cornerCount = 0;
      while (token &&
             (isdigit((unsigned char)token[0]) || token[0] == '-' || token[0] == '+')) {
        int32_t vi = -1, vti = -1, vni = -1;
        bool hasVti = false, hasVni = false;
        char* p = token;
        vi = (int32_t)strtol(p, &p, 10);
        if (*p == '/') {
          p++;
          if (*p != '/' && *p != 0) {
            vti = (int32_t)strtol(p, &p, 10);
            hasVti = true;
          }
          if (*p == '/') {
            p++;
            if (*p != 0) {
              vni = (int32_t)strtol(p, &p, 10);
              hasVni = true;
            }
          }
        }
        if (vi < 0) {
          vi += (int32_t)ctx->rawVCount / 3;
        } else if (vi > 0) {
          vi--;  // OBJ face indices are 1-based
        }
        if (hasVti) {
          if (vti < 0) vti += (int32_t)ctx->rawTCount / 2;
          else if (vti > 0) vti--;
        } else vti = -1;
        if (hasVni) {
          if (vni < 0) vni += (int32_t)ctx->rawNCount / 3;
          else if (vni > 0) vni--;
        } else vni = -1;
        if (vti < 0) vti = -1;
        if (vni < 0) vni = -1;
        int32_t merged = getMergedVertex3D(ctx, vi, vti, vni);
        if (merged >= 0) {
          if (cornerCount >= cornerCap) {
            uint32_t newCap = cornerCap ? cornerCap * 2 : 32;
            int32_t* nc = (int32_t*)grow3D(ctx->cornerBuf, newCap * sizeof(int32_t));
            if (!nc) break;  // out of memory: fan-triangulate the corners kept so far
            ctx->cornerBuf = corners = nc;
            ctx->cornerCap = cornerCap = newCap;
          }
          corners[cornerCount++] = merged;
        }
        token = nextObjToken3D();
      }
      for (uint32_t i = 1; i + 1 < cornerCount; i++) {
        addFace3D(ctx, corners[0], corners[i], corners[i+1]);
      }
      // token already points at the next record
    } else if (strcmp(token, "mtllib") == 0) {
      token = nextObjToken3D();
      if (token && mtllibName[0] == 0) {
        strncpy(mtllibName, token, 255);
        mtllibName[255] = 0;
      }
      token = token ? nextObjToken3D() : NULL;
    } else {
      token = nextObjToken3D(); // o, g, s, usemtl, Ka/Kd/... and unknown records
    }
  }
}

static String parentDir3D(const String& path) {
  int16_t slash = path.lastIndexOf('/');
  if (slash < 0) return String("");
  return path.substring(0, slash + 1);
}

static void tryMtlTexture3D(const String& objPath, const char* mtlName, Mesh3D* m) {
  String dir = parentDir3D(objPath);
  String mtlPath = dir + String(mtlName);
  File mtl = SD_MMC.open(mtlPath);
  if (!mtl) {
    Serial.println("engine3d: failed to open material file " + mtlPath);
    return;
  }
  char line[512];
  uint16_t len = 0;
  char texFile[256] = "";
  bool found = false;
  while (mtl.available()) {
    char c = mtl.read();
    if (c == '\n' || c == '\r') {
      line[len] = 0;
      if (strncmp(line, "map_Kd", 6) == 0) {
        char* p = line + 6;
        while (*p == ' ' || *p == '\t') p++;
        if (*p != 0) {
          strncpy(texFile, p, 255);
          texFile[255] = 0;
          char* end = texFile + strlen(texFile) - 1;
          while (end > texFile && (*end == ' ' || *end == '\t')) *end-- = 0;
          if (texFile[0] == '"' && end > texFile) {
            memmove(texFile, texFile + 1, strlen(texFile));
            end = texFile + strlen(texFile) - 1;
            if (*end == '"') *end = 0;
          }
          found = true;
          break;
        }
      }
      len = 0;
    } else if (len < 511) {
      line[len++] = c;
    }
  }
  mtl.close();
  if (!found || texFile[0] == 0) return;
  String texPath = parentDir3D(mtlPath) + String(texFile);
  int16_t texHandle = loadTexture3D(texPath);
  if (texHandle >= 0) {
    m->textureHandle = texHandle;
    Serial.println("engine3d: auto-textured " + objPath + " with " + texPath);
  }
}

int16_t loadMesh3D(String path) {
  if (!ensureWorld3D()) return -1;
  char* buf = readFileToNewPSBuffer(path.c_str());
  if (!buf) {
    Serial.println("engine3d: failed to read model file " + path);
    return -1;
  }
  int16_t slot = allocMeshSlot3D();
  if (slot < 0) {
    free(buf);
    return -1;
  }
  Mesh3D* m = &meshes3D[slot];
  ObjLoad3D ctx;
  memset(&ctx, 0, sizeof(ObjLoad3D));
  ctx.mesh = m;
  char mtllib[256] = "";
  parseObj3D(&ctx, buf, mtllib);
  free(buf);
  free(ctx.rawV);
  free(ctx.rawT);
  free(ctx.rawN);
  free(ctx.hash);
  free(ctx.cornerBuf);

  bool needSmoothedNormals = false;
  for (uint32_t i = 0; i < ctx.mergedCount; i++) {
    if (!ctx.hasNrm[i]) { needSmoothedNormals = true; break; }
  }

  m->vertexCount = (uint16_t)ctx.mergedCount;
  m->faceCount = (uint16_t)ctx.faceCount;
  m->vtx = ctx.vtx;
  m->faces = ctx.faces;
  if (ctx.hasUV && ctx.uvs) {
    m->uvQ = (int32_t*)heap_caps_malloc(sizeof(int32_t) * 2 * m->vertexCount, MALLOC_CAP_SPIRAM);
    if (m->uvQ) {
      for (uint16_t i = 0; i < m->vertexCount; i++) {
        float u = ctx.uvs[i*2], v = ctx.uvs[i*2+1];
        // Keep Q16 texture coords and their interpolation deltas well inside int32 range
        if (u > 16.0f) u = 16.0f; else if (u < -16.0f) u = -16.0f;
        if (v > 16.0f) v = 16.0f; else if (v < -16.0f) v = -16.0f;
        m->uvQ[i*2] = (int32_t)(u * 65536.0f);
        m->uvQ[i*2+1] = (int32_t)(v * 65536.0f);
      }
    }
  }
  free(ctx.uvs);
  if (!m->vtx || !m->faces || m->vertexCount == 0 || m->faceCount == 0) {
    Serial.println("engine3d: no usable faces in model " + path);
    free(ctx.hasNrm);
    freeMeshArrays3D(m);
    return -1;
  }
  normalizeNormals3D(m, needSmoothedNormals ? ctx.hasNrm : nullptr);
  free(ctx.hasNrm);
  if (!buildMeshScratch3D(m)) {
    Serial.println("engine3d: failed to allocate scratch buffers for model " + path);
    freeMeshArrays3D(m);
    return -1;
  }
  if (mtllib[0] != 0) {
    tryMtlTexture3D(path, mtllib, m);
  }
  Serial.print("engine3d: loaded model ");
  Serial.print(path);
  Serial.print(" (");
  Serial.print(m->vertexCount);
  Serial.print(" verts, ");
  Serial.print(m->faceCount);
  Serial.println(" faces)");
  return slot;
}
// ---------------------------------------------------------------------------
// Textures
// ---------------------------------------------------------------------------

int16_t loadTexture3D(String path) {
  if (!ensureWorld3D()) return -1;
  for (int16_t i = 0; i < TEXTURE3D_LIMIT; i++) {
    if (textures3D[i].inUse) continue;
    TFT_eSprite* s = &textureSprites3D[i];
    if (s->created()) s->deleteSprite();
    s->setColorDepth(16);
    // Rasterizer texture sampling is random access: texture pixel buffers
    // live in internal RAM so that traffic never touches the PSRAM bus the
    // mesh streams and the core 0 blit task already saturate.
    s->setPsram(false);
    if (!loadBmp(s, path) && !s->created()) {
      s->setPsram(true); // internal heap exhausted: fall back to PSRAM
      if (!loadBmp(s, path)) {
        Serial.println("engine3d: failed to load texture " + path);
        return -1;
      }
    }
    const uint16_t* pixels = (const uint16_t*)s->getPointer();
    int16_t w = s->width();
    int16_t h = s->height();
    if (!pixels || w < 1 || h < 1) {
      s->deleteSprite();
      Serial.println("engine3d: texture has no usable pixel buffer: " + path);
      return -1;
    }
    textures3D[i].pixels = pixels;
    textures3D[i].w = w;
    textures3D[i].h = h;
    textures3D[i].mask = (w == h && (w & (w - 1)) == 0) ? (w - 1) : -1;
    textures3D[i].inUse = true;
    Serial.print("engine3d: loaded texture ");
    Serial.println(path);
    return i;
  }
  Serial.println("engine3d: no free texture slot!");
  return -1;
}

bool freeTexture3D(int16_t handle) {
  if (handle < 0 || handle >= TEXTURE3D_LIMIT || !textures3D[handle].inUse) return false;
  TFT_eSprite* s = &textureSprites3D[handle];
  if (s->created()) s->deleteSprite();
  memset(&textures3D[handle], 0, sizeof(Texture3D));
  for (int16_t mi = 0; mi < MESH3D_LIMIT; mi++) {
    if (meshes3D[mi].inUse && meshes3D[mi].textureHandle == handle) {
      meshes3D[mi].textureHandle = -1;
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Mesh management and procedural primitives
// ---------------------------------------------------------------------------

bool freeMesh3D(int16_t handle) {
  if (handle < 0 || handle >= MESH3D_LIMIT || !meshes3D[handle].inUse) return false;
  freeMeshArrays3D(&meshes3D[handle]);
  for (int16_t ei = 0; ei < ENTITY3D_LIMIT; ei++) {
    if (entities3D[ei].inUse && entities3D[ei].meshHandle == handle) {
      entities3D[ei].meshHandle = -1;
    }
  }
  return true;
}

bool getMeshStats3D(int16_t handle, uint16_t* vertexCount, uint16_t* faceCount) {
  if (handle < 0 || handle >= MESH3D_LIMIT || !meshes3D[handle].inUse) return false;
  if (vertexCount) *vertexCount = meshes3D[handle].vertexCount;
  if (faceCount) *faceCount = meshes3D[handle].faceCount;
  return true;
}

void setMeshColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b) {
  if (handle < 0 || handle >= MESH3D_LIMIT || !meshes3D[handle].inUse) return;
  meshes3D[handle].color[0] = r;
  meshes3D[handle].color[1] = g;
  meshes3D[handle].color[2] = b;
}

void setMeshTexture3D(int16_t meshHandle, int16_t textureHandle) {
  if (meshHandle < 0 || meshHandle >= MESH3D_LIMIT || !meshes3D[meshHandle].inUse) return;
  if (textureHandle >= 0 && (textureHandle >= TEXTURE3D_LIMIT || !textures3D[textureHandle].inUse)) return;
  meshes3D[meshHandle].textureHandle = textureHandle;
}

void setMeshFlipped3D(int16_t handle, bool flipped) {
  if (handle < 0 || handle >= MESH3D_LIMIT || !meshes3D[handle].inUse) return;
  meshes3D[handle].flipped = flipped ? 1 : 0;
}

static bool setupPrimitiveMesh3D(Mesh3D* m, uint16_t vc, uint16_t fc) {
  m->vtx = (MeshVertex3D*)heap_caps_aligned_alloc(64, meshVtxSpan3D(vc), MALLOC_CAP_SPIRAM);
  m->uvQ = (int32_t*)heap_caps_malloc(sizeof(int32_t) * 2 * vc, MALLOC_CAP_SPIRAM);
  m->faces = (Face3D*)heap_caps_malloc(sizeof(Face3D) * fc, MALLOC_CAP_SPIRAM);
  if (!m->vtx || !m->uvQ || !m->faces) {
    freeMeshArrays3D(m);
    return false;
  }
  m->vertexCount = vc;
  m->faceCount = fc;
  if (!buildMeshScratch3D(m)) {
    freeMeshArrays3D(m);
    return false;
  }
  return true;
}

// Each quad is wound so its winding cross product points outwards, matching
// the standard OBJ front facing convention.
int16_t createMesh3DCube() {
  if (!ensureWorld3D()) return -1;
  int16_t slot = allocMeshSlot3D();
  if (slot < 0) return -1;
  Mesh3D* m = &meshes3D[slot];
  const uint16_t vc = 24, fc = 12;
  if (!setupPrimitiveMesh3D(m, vc, fc)) return -1;
  static const float quadPos[6][4][3] = {
    {{25,25,-25},{25,-25,-25},{-25,-25,-25},{-25,25,-25}},
    {{-25,-25,25},{25,-25,25},{25,25,25},{-25,25,25}},
    {{25,-25,-25},{25,25,-25},{25,25,25},{25,-25,25}},
    {{-25,25,-25},{-25,-25,-25},{-25,-25,25},{-25,25,25}},
    {{-25,25,25},{25,25,25},{25,25,-25},{-25,25,-25}},
    {{25,-25,25},{-25,-25,25},{-25,-25,-25},{25,-25,-25}}
  };
  static const float quadNrm[6][3] = {
    {0,0,-1},{0,0,1},{1,0,0},{-1,0,0},{0,1,0},{0,-1,0}
  };
  static const float quadUV[4][2] = {{0,0},{1,0},{1,1},{0,1}};
  for (int fi = 0; fi < 6; fi++) {
    uint16_t base = fi * 4;
    for (int qi = 0; qi < 4; qi++) {
      m->vtx[base+qi].pos.x = quadPos[fi][qi][0];
      m->vtx[base+qi].pos.y = quadPos[fi][qi][1];
      m->vtx[base+qi].pos.z = quadPos[fi][qi][2];
      m->vtx[base+qi].nrm.x = quadNrm[fi][0];
      m->vtx[base+qi].nrm.y = quadNrm[fi][1];
      m->vtx[base+qi].nrm.z = quadNrm[fi][2];
      m->uvQ[(base+qi)*2] = (int32_t)(quadUV[qi][0] * 65536.0f);
      m->uvQ[(base+qi)*2+1] = (int32_t)(quadUV[qi][1] * 65536.0f);
    }
    m->faces[fi*2] = {base, (uint16_t)(base+1), (uint16_t)(base+2)};
    m->faces[fi*2+1] = {base, (uint16_t)(base+2), (uint16_t)(base+3)};
  }
  return slot;
}

int16_t createMesh3DSphere(float radius, uint8_t segments) {
  if (!ensureWorld3D()) return -1;
  if (radius <= 0.0f) radius = 1.0f;
  if (segments < 4) segments = 4;
  if (segments > 30) segments = 30;
  uint16_t S = segments, L = segments;
  uint16_t vc = (S + 1) * (L + 1);
  uint16_t fc = 2 * S * L;
  if (vc > MESH3D_MAX_VERTICES || fc > MESH3D_MAX_FACES) return -1;
  int16_t slot = allocMeshSlot3D();
  if (slot < 0) return -1;
  Mesh3D* m = &meshes3D[slot];
  if (!setupPrimitiveMesh3D(m, vc, fc)) return -1;
  for (uint16_t i = 0; i <= L; i++) {
    float phi = (float)i * (M_PI / (float)L);
    float sinPhi = sinf(phi), cosPhi = cosf(phi);
    for (uint16_t j = 0; j <= S; j++) {
      float theta = (float)j * (2.0f * M_PI / (float)S);
      uint16_t idx = i * (S + 1) + j;
      m->vtx[idx].pos.x = radius * sinPhi * cosf(theta);
      m->vtx[idx].pos.y = radius * cosPhi;
      m->vtx[idx].pos.z = radius * sinPhi * sinf(theta);
      if (sinPhi > 1e-6f) {
        m->vtx[idx].nrm.x = sinPhi * cosf(theta);
        m->vtx[idx].nrm.y = cosPhi;
        m->vtx[idx].nrm.z = sinPhi * sinf(theta);
      } else {
        m->vtx[idx].nrm.x = 0;
        m->vtx[idx].nrm.y = cosPhi >= 0 ? 1 : -1;
        m->vtx[idx].nrm.z = 0;
      }
      m->uvQ[idx*2] = (int32_t)((float)j / (float)S * 65536.0f);
      m->uvQ[idx*2+1] = (int32_t)((float)i / (float)L * 65536.0f);
    }
  }
  uint16_t faceIdx = 0;
  for (uint16_t i = 0; i < L; i++) {
    for (uint16_t j = 0; j < S; j++) {
      uint16_t p0 = i * (S + 1) + j;
      uint16_t p1 = i * (S + 1) + j + 1;
      uint16_t p2 = (i + 1) * (S + 1) + j + 1;
      uint16_t p3 = (i + 1) * (S + 1) + j;
      m->faces[faceIdx++] = {p0, p1, p2};
      m->faces[faceIdx++] = {p0, p2, p3};
    }
  }
  return slot;
}

int16_t createMesh3DPlane(float width, float height) {
  if (!ensureWorld3D()) return -1;
  if (width <= 0.0f) width = 1.0f;
  if (height <= 0.0f) height = 1.0f;
  int16_t slot = allocMeshSlot3D();
  if (slot < 0) return -1;
  Mesh3D* m = &meshes3D[slot];
  if (!setupPrimitiveMesh3D(m, 4, 2)) return -1;
  float hw = width * 0.5f, hh = height * 0.5f;
  static const float cornerUV[4][2] = {{0,0},{1,0},{1,1},{0,1}};
  m->vtx[0].pos = {-hw, 0, hh};
  m->vtx[1].pos = {hw, 0, hh};
  m->vtx[2].pos = {hw, 0, -hh};
  m->vtx[3].pos = {-hw, 0, -hh};
  for (int i = 0; i < 4; i++) {
    m->vtx[i].nrm = {0, 1, 0};
    m->uvQ[i*2] = (int32_t)(cornerUV[i][0] * 65536.0f);
    m->uvQ[i*2+1] = (int32_t)(cornerUV[i][1] * 65536.0f);
  }
  m->faces[0] = {0, 1, 2};
  m->faces[1] = {0, 2, 3};
  return slot;
}

// ---------------------------------------------------------------------------
// Entities
// ---------------------------------------------------------------------------

int16_t createEntity3D(int16_t meshHandle) {
  if (!ensureWorld3D()) return -1;
  if (meshHandle < 0 || meshHandle >= MESH3D_LIMIT || !meshes3D[meshHandle].inUse) {
    Serial.println("engine3d: CreateEntity got an invalid mesh handle!");
    return -1;
  }
  for (int16_t i = 0; i < ENTITY3D_LIMIT; i++) {
    if (entities3D[i].inUse) continue;
    Entity3D* e = &entities3D[i];
    memset(e, 0, sizeof(Entity3D));
    e->inUse = true;
    e->meshHandle = meshHandle;
    e->parentHandle = -1;
    e->sx = 1.0f; e->sy = 1.0f; e->sz = 1.0f;
    e->color[0] = 255; e->color[1] = 255; e->color[2] = 255;
    return i;
  }
  Serial.println("engine3d: no free entity slot!");
  return -1;
}

bool freeEntity3D(int16_t handle) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return false;
  entities3D[handle].inUse = false;
  memset(&entities3D[handle], 0, sizeof(Entity3D));
  for (int16_t i = 0; i < ENTITY3D_LIMIT; i++) {
    if (entities3D[i].inUse && entities3D[i].parentHandle == handle) {
      entities3D[i].parentHandle = -1;
    }
  }
  return true;
}

void positionEntity3D(int16_t handle, float x, float y, float z) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].x = x;
  entities3D[handle].y = y;
  entities3D[handle].z = z;
}

void rotateEntity3D(int16_t handle, float pitch, float yaw, float roll) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].pitch = pitch;
  entities3D[handle].yaw = yaw;
  entities3D[handle].roll = roll;
}

void turnEntity3D(int16_t handle, float pitch, float yaw, float roll) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].pitch += pitch;
  entities3D[handle].yaw += yaw;
  entities3D[handle].roll += roll;
}

void scaleEntity3D(int16_t handle, float sx, float sy, float sz) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].sx = sx;
  entities3D[handle].sy = sy;
  entities3D[handle].sz = sz;
}

void entityColor3D(int16_t handle, uint8_t r, uint8_t g, uint8_t b) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].color[0] = r;
  entities3D[handle].color[1] = g;
  entities3D[handle].color[2] = b;
}

void hideEntity3D(int16_t handle, bool hidden) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].hidden = hidden ? 1 : 0;
}

bool linkEntity3D(int16_t childHandle, int16_t parentHandle) {
  if (childHandle < 0 || childHandle >= ENTITY3D_LIMIT || !entities3D[childHandle].inUse) return false;
  if (parentHandle >= ENTITY3D_LIMIT || !entities3D[parentHandle].inUse) return false;
  if (childHandle == parentHandle) return false;
  entities3D[childHandle].parentHandle = parentHandle;
  return true;
}

void entityOrder3D(int16_t handle, int8_t order) {
  if (handle < 0 || handle >= ENTITY3D_LIMIT || !entities3D[handle].inUse) return;
  entities3D[handle].order = order;
}

// ---------------------------------------------------------------------------
// Cameras
// ---------------------------------------------------------------------------

int16_t createCamera3D() {
  if (!ensureWorld3D()) return -1;
  if (implicitCamera3D) {
    implicitCamera3D = false;
    cameras3D[0].inUse = false;
  }
  for (int16_t i = 0; i < CAMERA3D_LIMIT; i++) {
    if (cameras3D[i].inUse) continue;
    memset(&cameras3D[i], 0, sizeof(Camera3D));
    cameras3D[i].inUse = true;
    cameras3D[i].z = -100;
    cameras3D[i].zoom = 1.0f;
    cameras3D[i].nearPlane = 1.0f;
    cameras3D[i].farPlane = 5000.0f;
    return i;
  }
  Serial.println("engine3d: no free camera slot!");
  return -1;
}

bool freeCamera3D(int16_t handle) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return false;
  cameras3D[handle].inUse = false;
  return true;
}

void positionCamera3D(int16_t handle, float x, float y, float z) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return;
  cameras3D[handle].x = x;
  cameras3D[handle].y = y;
  cameras3D[handle].z = z;
}

void rotateCamera3D(int16_t handle, float pitch, float yaw, float roll) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return;
  cameras3D[handle].pitch = pitch;
  cameras3D[handle].yaw = yaw;
  cameras3D[handle].roll = roll;
}

void turnCamera3D(int16_t handle, float pitch, float yaw, float roll) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return;
  cameras3D[handle].pitch += pitch;
  cameras3D[handle].yaw += yaw;
  cameras3D[handle].roll += roll;
}

void cameraZoom3D(int16_t handle, float zoom) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return;
  if (zoom < 0.01f) zoom = 0.01f;
  cameras3D[handle].zoom = zoom;
}

void cameraRange3D(int16_t handle, float nearPlane, float farPlane) {
  if (handle < 0 || handle >= CAMERA3D_LIMIT || !cameras3D[handle].inUse) return;
  if (nearPlane < 0.01f) nearPlane = 0.01f;
  if (farPlane <= nearPlane + 0.01f) farPlane = nearPlane + 100.0f;
  cameras3D[handle].nearPlane = nearPlane;
  cameras3D[handle].farPlane = farPlane;
}

// ---------------------------------------------------------------------------
// Lights
// ---------------------------------------------------------------------------

void ambientLight3D(uint8_t r, uint8_t g, uint8_t b) {
  ambient3D[0] = (float)r / 255.0f;
  ambient3D[1] = (float)g / 255.0f;
  ambient3D[2] = (float)b / 255.0f;
}

static void lightSetCone3D(Light3D* l, float innerDeg, float outerDeg) {
  if (innerDeg < 0.1f) innerDeg = 0.1f;
  if (outerDeg <= innerDeg + 0.1f) outerDeg = innerDeg + 0.1f;
  l->cosInner = cosf(innerDeg * DEG3D_TO_RAD);
  l->cosOuter = cosf(outerDeg * DEG3D_TO_RAD);
  float delta = l->cosInner - l->cosOuter;
  l->cosInvDelta = (delta > 1e-6f) ? (1.0f / delta) : 1000000.0f;
}

int16_t createLight3D(uint8_t r, uint8_t g, uint8_t b, float x, float y, float z, float range, uint8_t type) {
  if (!ensureWorld3D()) return -1;
  for (int16_t i = 0; i < LIGHT3D_LIMIT; i++) {
    if (lights3D[i].inUse) continue;
    Light3D* l = &lights3D[i];
    memset(l, 0, sizeof(Light3D));
    l->inUse = true;
    l->type = type;
    l->r = (float)r / 255.0f;
    l->g = (float)g / 255.0f;
    l->b = (float)b / 255.0f;
    l->x = x; l->y = y; l->z = z;
    if (range > 0.0f) {
      l->range = range;
      l->invRange = 1.0f / range;
      l->rangeSq = range * range;
    } else {
      l->range = 0;
      l->invRange = 0;
      l->rangeSq = LIGHT3D_MAX_RANGE;
    }
    l->dx = 0; l->dy = -1; l->dz = 0;
    lightSetCone3D(l, SPOT3D_DEFAULT_INNER_DEG, SPOT3D_DEFAULT_OUTER_DEG);
    return i;
  }
  Serial.println("engine3d: no free light slot!");
  return -1;
}

bool freeLight3D(int16_t handle) {
  if (handle < 0 || handle >= LIGHT3D_LIMIT || !lights3D[handle].inUse) return false;
  memset(&lights3D[handle], 0, sizeof(Light3D));
  return true;
}

void setLightDirection3D(int16_t handle, float dx, float dy, float dz) {
  if (handle < 0 || handle >= LIGHT3D_LIMIT || !lights3D[handle].inUse) return;
  float len = sqrtf(dx*dx + dy*dy + dz*dz);
  if (len < 1e-6f) return;
  float inv = 1.0f / len;
  lights3D[handle].dx = dx * inv;
  lights3D[handle].dy = dy * inv;
  lights3D[handle].dz = dz * inv;
}

void setLightCone3D(int16_t handle, float innerAngleDeg, float outerAngleDeg) {
  if (handle < 0 || handle >= LIGHT3D_LIMIT || !lights3D[handle].inUse) return;
  lightSetCone3D(&lights3D[handle], innerAngleDeg, outerAngleDeg);
}

void resetWorld3D() {
  for (int16_t i = 0; i < MESH3D_LIMIT; i++) {
    if (world3DReady) freeMeshArrays3D(&meshes3D[i]);
    else memset(&meshes3D[i], 0, sizeof(Mesh3D));
  }
  if (world3DReady) {
    for (int16_t i = 0; i < TEXTURE3D_LIMIT; i++) {
      if (textures3D[i].inUse && textureSprites3D[i].created()) textureSprites3D[i].deleteSprite();
    }
  }
  memset(textures3D, 0, sizeof(textures3D));
  memset(entities3D, 0, sizeof(entities3D));
  memset(lights3D, 0, sizeof(lights3D));
  ambient3D[0] = 0.5f; ambient3D[1] = 0.5f; ambient3D[2] = 0.5f;
  drawCount3D = 0;
  w3d3DFrameActive3D = false;
  w3dMaskActive3D = false;
  w3dDrawMask3D = nullptr;
  setWorld3DViewport3D(0, 0, 0, 0); // viewport is a per-game setting
  setupDefaultCamera3D();
}
// ---------------------------------------------------------------------------
// World update: transforms, culling, Gouraud lighting, depth sorting
// ---------------------------------------------------------------------------

static void entityWorldTransform3D(int16_t handle, float* rot9, float* pos3, uint8_t guard) {
  Entity3D* e = &entities3D[handle];
  Mat3 own;
  mat3FromEuler3D(e->pitch, e->yaw, e->roll, &own);
  memcpy(rot9, own.m, sizeof(own.m));
  pos3[0] = e->x;
  pos3[1] = e->y;
  pos3[2] = e->z;
  if (e->parentHandle >= 0 && e->parentHandle < ENTITY3D_LIMIT &&
      entities3D[e->parentHandle].inUse && guard < 8) {
    float prot[9], ppos[3];
    entityWorldTransform3D(e->parentHandle, prot, ppos, guard + 1);
    Mat3 parent, child, combined;
    memcpy(parent.m, prot, sizeof(parent.m));
    memcpy(child.m, own.m, sizeof(child.m));
    mat3Mul3D(&parent, &child, &combined);
    memcpy(rot9, combined.m, sizeof(combined.m));
    float nx = prot[0]*pos3[0] + prot[1]*pos3[1] + prot[2]*pos3[2] + ppos[0];
    float ny = prot[3]*pos3[0] + prot[4]*pos3[1] + prot[5]*pos3[2] + ppos[1];
    float nz = prot[6]*pos3[0] + prot[7]*pos3[1] + prot[8]*pos3[2] + ppos[2];
    pos3[0] = nx; pos3[1] = ny; pos3[2] = nz;
  }
}

static uint32_t lerpColor3D(uint32_t ca, uint32_t cb, float t) {
  int32_t ra = (ca >> 16) & 0xFF, ga = (ca >> 8) & 0xFF, ba = ca & 0xFF;
  int32_t rb = (cb >> 16) & 0xFF, gb = (cb >> 8) & 0xFF, bb = cb & 0xFF;
  int32_t r = clamp3DI(ra + (int32_t)((float)(rb - ra) * t), 0, 255);
  int32_t g = clamp3DI(ga + (int32_t)((float)(gb - ga) * t), 0, 255);
  int32_t b = clamp3DI(ba + (int32_t)((float)(bb - ba) * t), 0, 255);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

// Sutherland-Hodgman clip of a triangle against the near plane, interpolating
// position, color and texture coordinates. Returns 3 or 4 clipped vertices.
static uint8_t clipTriangleNear3D(float nearPlane,
    const float* pa, const float* pb, const float* pc,
    uint32_t colA, uint32_t colB, uint32_t colC,
    int32_t uvA0, int32_t uvA1, int32_t uvB0, int32_t uvB1, int32_t uvC0, int32_t uvC1,
    float outPos[4][3], uint32_t outCol[4], int32_t outU[4], int32_t outV[4]) {
  float pos[3][3] = {
    {pa[0], pa[1], pa[2]},
    {pb[0], pb[1], pb[2]},
    {pc[0], pc[1], pc[2]}
  };
  uint32_t col[3] = {colA, colB, colC};
  int32_t us[3] = {uvA0, uvB0, uvC0};
  int32_t vs[3] = {uvA1, uvB1, uvC1};
  uint8_t outCount = 0;
  for (int edge = 0; edge < 3 && outCount < 4; edge++) {
    int a = edge;
    int b = (edge == 2) ? 0 : edge + 1;
    bool inA = pos[a][2] >= nearPlane;
    bool inB = pos[b][2] >= nearPlane;
    if (inA && outCount < 4) {
      outPos[outCount][0] = pos[a][0];
      outPos[outCount][1] = pos[a][1];
      outPos[outCount][2] = pos[a][2];
      outCol[outCount] = col[a];
      outU[outCount] = us[a];
      outV[outCount] = vs[a];
      outCount++;
    }
    if (inA != inB && outCount < 4) {
      float denom = pos[b][2] - pos[a][2];
      float t = (denom == 0.0f) ? 0.0f : (nearPlane - pos[a][2]) * fastRecip3D(denom);
      outPos[outCount][0] = pos[a][0] + (pos[b][0] - pos[a][0]) * t;
      outPos[outCount][1] = pos[a][1] + (pos[b][1] - pos[a][1]) * t;
      outPos[outCount][2] = nearPlane;
      outCol[outCount] = lerpColor3D(col[a], col[b], t);
      outU[outCount] = us[a] + (int32_t)((float)(us[b] - us[a]) * t);
      outV[outCount] = vs[a] + (int32_t)((float)(vs[b] - vs[a]) * t);
      outCount++;
    }
  }
  return outCount;
}

// scale carries projScale * (1/viewZ): the divide is hoisted to the callers, which
// need 1/z for their texture coordinates anyway. Callers only project vertices at
// or beyond the near plane, so scale is always finite.
static void projectClipPoint3D(const float* viewPos, float scale, float cx, float cy,
    int32_t* outX, int32_t* outY) {
  float sxf = viewPos[0] * scale;
  float syf = viewPos[1] * scale;
  if (sxf > 1024.0f) sxf = 1024.0f;
  else if (sxf < -1024.0f) sxf = -1024.0f;
  if (syf > 1024.0f) syf = 1024.0f;
  else if (syf < -1024.0f) syf = -1024.0f;
  *outX = (int32_t)((cx + sxf) * 256.0f);
  *outY = (int32_t)((cy - syf) * 256.0f);
}

void setWorld3DDebug3D(bool enabled) {
  perf3DEnabled = enabled;
  if (!enabled) return;
  resetWorld3DPerf3D();
  perf3DReportBase3D = perf3D;
  perf3DReportMs3D = millis() + 1000;
}

void getWorld3DPerf3D(World3DPerf3D* out) {
  if (out) *out = perf3D;
}

void resetWorld3DPerf3D() {
  memset(&perf3D, 0, sizeof(perf3D));
  perf3DReportBase3D = perf3D;
  perf3DReportMs3D = 0;
  w3dPerfFrameT3D = 0;
}

// Accumulates externally measured time (pressure sensor reads) into the perf
// window so the frame budget gap can be attributed.
void addWorld3DSensorUs3D(int32_t us) {
  if (!perf3DEnabled) return;
  perf3D.sensorUs += us;
}

// Prints the averages of the window since the previous report, once per second.
void reportWorld3DFrame3D(int32_t pushUs) {
  if (!perf3DEnabled) return;
  int64_t frameT = esp_timer_get_time();
  if (w3dPerfFrameT3D) perf3D.frameUs += (int32_t)(frameT - w3dPerfFrameT3D);
  w3dPerfFrameT3D = frameT;
  perf3D.frames++;
  perf3D.pushUs += pushUs;
  uint32_t now = millis();
  if (perf3DReportMs3D == 0) { perf3DReportMs3D = now + 1000; return; }
  if (now < perf3DReportMs3D) return;
  World3DPerf3D d = perf3D;
  d.updateUs -= perf3DReportBase3D.updateUs;
  d.drawUs -= perf3DReportBase3D.drawUs;
  d.pushUs -= perf3DReportBase3D.pushUs;
  d.xfUs -= perf3DReportBase3D.xfUs;
  d.faceUs -= perf3DReportBase3D.faceUs;
  d.bucketUs -= perf3DReportBase3D.bucketUs;
  d.vertsTransformed -= perf3DReportBase3D.vertsTransformed;
  d.trisSubmitted -= perf3DReportBase3D.trisSubmitted;
  d.trisClipped -= perf3DReportBase3D.trisClipped;
  d.spans -= perf3DReportBase3D.spans;
  d.pixels -= perf3DReportBase3D.pixels;
  d.frames -= perf3DReportBase3D.frames;
  d.sensorUs -= perf3DReportBase3D.sensorUs;
  d.frameUs -= perf3DReportBase3D.frameUs;
  d.gapUs -= perf3DReportBase3D.gapUs;
  d.gapPx -= perf3DReportBase3D.gapPx;
  perf3DReportBase3D = perf3D;
  double f = d.frames ? (double)d.frames : 1.0;
  double windowMs = (double)(now - perf3DReportMs3D + 1000) / 1000.0;
  double frameMs = d.frameUs / 1000.0 / f;
  // "other" is the part of the frame loop the engine timers do not cover:
  // Lua VM, HUD drawing, sprite fill, sensor reads, doSystemTasks delay.
  double otherMs = frameMs - d.updateUs / 1000.0 / f - d.drawUs / 1000.0 / f;
  if (strcmp(w3dBlitMode3D, "sync") == 0) otherMs -= d.pushUs / 1000.0 / f;
  Serial.printf("engine3d perf: %.1f fps  update=%.2fms  draw=%.2fms  blit=%.2fms(%s)"
                "  other=%.2fms  gap=%.2fms  update parts: xf=%.2f face=%.2f bucket=%.2f\n",
                f / windowMs, d.updateUs / 1000.0 / f, d.drawUs / 1000.0 / f, d.pushUs / 1000.0 / f,
                w3dBlitMode3D, otherMs, d.gapUs / 1000.0 / f,
                d.xfUs / 1000.0 / f, d.faceUs / 1000.0 / f, d.bucketUs / 1000.0 / f);
  Serial.printf("engine3d perf: verts=%.0f  tris=%.0f  clipped=%.0f  spans=%.0f  px=%.0f  gappx=%.0f  sensor=%.2fms (per frame)\n",
                d.vertsTransformed / f, d.trisSubmitted / f, d.trisClipped / f, d.spans / f, d.pixels / f,
                d.gapPx / f, d.sensorUs / 1000.0 / f);
  perf3DReportMs3D = now + 1000;
}

/*
 * Pipelined display blit. Pushing a full screen costs ~15ms of 8-bit
 * parallel bus time no matter which core issues it, so a task pinned to
 * core 0 pushes the frame that was just rendered while the game core
 * rasterizes the next frame into the sprite's other frame buffer. The two
 * cores never touch the same buffer in the same frame, and the ESP32-S3
 * MMU/cache is shared between both cores, so handing over PSRAM buffers
 * needs no cache maintenance. The pipelined push is a plain full push: a
 * diff push would re-read the 153KB back buffer from PSRAM every frame,
 * competing with the rendering core on the shared bus, and dynamic frames
 * rarely skip enough runs for the diff to recover that cost. Screens that
 * are not full-frame game loops are never pipelined and keep the
 * synchronous diff push.
 */
static SemaphoreHandle_t w3dPushReady3D = nullptr;
static SemaphoreHandle_t w3dPushDone3D = nullptr;
static SemaphoreHandle_t w3dFillDone3D = nullptr;
// Work drain: taken by a blit that must know the task consumed the previous
// hand-off before overwriting the hand-off variables, given back by the task
// once push and fill bookkeeping are finished.
static SemaphoreHandle_t w3dWorkDone3D = nullptr;
static TaskHandle_t w3dPushTask3DHandle = nullptr;
static bool w3dPushPending3D = false;
static bool w3dPipelineEnabled3D = true;
static bool w3dI80Push3D = true; // full-frame pushes go through the LCD_CAM peripheral when available
static uint16_t* w3dPushNew3D = nullptr; // buffer holding the frame to display
static int16_t w3dPushW3D = 0, w3dPushH3D = 0;
static volatile int32_t w3dPushUs3D = 0;
// Background blanking via coverage masks. Each double-buffer frame has a
// 1-bit-per-pixel mask: the rasterizer sets bits as it writes spans, 2D
// writers claim rects before drawing, and the core 0 task memsets every
// unmasked run of the buffer right before pushing it. A frame buffer that
// is pushed is therefore fully valid (drawn pixels + black gaps) without
// core 1 ever memsetting the 153KB buffer, and core 0 only touches the
// uncovered remainder (~35KB for a typical 3D scene instead of 153KB).
static uint32_t* w3dMask3D[2] = {nullptr, nullptr};
static int16_t w3dMaskWords3D = 0;
static int16_t w3dMaskRows3D = 0;
static int16_t w3dMaskWidth3D = 0;
static uint32_t w3dMaskTail3D = 0; // valid pixel bits in the last word of a row
static bool w3dMaskValid3D = false;
static uint16_t* w3dGapBuf3D = nullptr;  // buffer whose gaps this push fills first
static const uint32_t* w3dGapMask3D = nullptr;
static DISPLAY_T* w3dMaskDisplay3D = nullptr;
static int8_t w3dMaskFrameId3D = 0;
// Legacy pipelined blanking (core 0 memsets the whole next buffer) stays as
// the fallback whenever the mask mode is not in effect.
static uint16_t* w3dFillBuf3D = nullptr;
static uint32_t w3dFillBytes3D = 0;
static bool w3dFillWait3D = false;

static inline uint32_t maskRange3D(int32_t lo, int32_t hi) {
  if (lo <= 0 && hi >= 31) return 0xFFFFFFFFu;
  return (((1u << (hi - lo + 1)) - 1u) << lo);
}

// Memset every run of zero bits in one mask word (pixels start at fbRow);
// returns the number of pixels filled.
static uint32_t gapFillWord3D(uint16_t* fbRow, uint32_t uncovered) {
  uint32_t filled = 0;
  while (uncovered) {
    int32_t b = __builtin_ctz(uncovered);
    uint32_t v = uncovered >> b;
    int32_t len = (v == 0xFFFFFFFFu) ? 32 : __builtin_ctz(~v);
    memset(fbRow + b, 0, (size_t)len * 2);
    uncovered &= ~(len >= 32 ? 0xFFFFFFFFu : (((1u << len) - 1u) << b));
    filled += (uint32_t)len;
  }
  return filled;
}

// Mark the pixels a rasterizer span wrote; the gap fill will skip them.
static inline void markSpanMask3D(int32_t row, int32_t x0, int32_t x1) {
  uint32_t* m = w3dDrawMask3D + row * w3dMaskWords3D;
  int32_t w0 = x0 >> 5, w1 = x1 >> 5;
  if (w0 == w1) {
    m[w0] |= maskRange3D(x0 & 31, x1 & 31);
  } else {
    m[w0] |= maskRange3D(x0 & 31, 31);
    for (int32_t w = w0 + 1; w < w1; w++) m[w] = 0xFFFFFFFFu;
    m[w1] |= maskRange3D(0, x1 & 31);
  }
}

static uint32_t gapFill3D(uint16_t* buf, const uint32_t* mask) {
  uint32_t filled = 0;
  for (int32_t row = 0; row < w3dMaskRows3D; row++) {
    const uint32_t* mrow = mask + row * w3dMaskWords3D;
    uint16_t* fbRow = buf + row * (int32_t)w3dMaskWidth3D;
    for (int32_t wi = 0; wi < w3dMaskWords3D; wi++) {
      uint32_t valid = (wi + 1 == w3dMaskWords3D) ? w3dMaskTail3D : 0xFFFFFFFFu;
      uint32_t uncovered = valid & ~mrow[wi];
      if (uncovered) filled += gapFillWord3D(fbRow + (wi << 5), uncovered);
    }
  }
  return filled;
}

static bool ensureMask3D(int32_t w, int32_t h) {
  int16_t words = (int16_t)((w + 31) >> 5);
  if (w3dMaskValid3D && w3dMaskWords3D == words && w3dMaskRows3D == h) return true;
  for (int i = 0; i < 2; i++) {
    heap_caps_free(w3dMask3D[i]);
    w3dMask3D[i] = nullptr;
  }
  w3dMaskValid3D = false;
  size_t bytes = (size_t)words * h * 4;
  w3dMask3D[0] = (uint32_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  w3dMask3D[1] = (uint32_t*)heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!w3dMask3D[0] || !w3dMask3D[1]) {
    heap_caps_free(w3dMask3D[0]);
    heap_caps_free(w3dMask3D[1]);
    w3dMask3D[0] = w3dMask3D[1] = nullptr;
    return false;
  }
  memset(w3dMask3D[0], 0, bytes);
  memset(w3dMask3D[1], 0, bytes);
  w3dMaskWords3D = words;
  w3dMaskRows3D = (int16_t)h;
  w3dMaskWidth3D = (int16_t)w;
  w3dMaskTail3D = (w & 31) ? ((1u << (w & 31)) - 1u) : 0xFFFFFFFFu;
  w3dMaskValid3D = true;
  return true;
}

static void w3dPushTaskLoop3D(void*) {
  for (;;) {
    xSemaphoreTake(w3dPushReady3D, portMAX_DELAY);
    uint16_t* gapBuf = w3dGapBuf3D; // latch the hand-off so later blits
    const uint32_t* gapMask = w3dGapMask3D; // cannot mutate this iteration's work
    uint16_t* fillBuf = w3dFillBuf3D;
    uint32_t fillBytes = w3dFillBytes3D;
    if (gapBuf) {
      int64_t gapT0 = perf3DEnabled ? esp_timer_get_time() : 0;
      uint32_t filled = gapFill3D(gapBuf, gapMask);
      if (perf3DEnabled) {
        perf3D.gapUs += (int32_t)(esp_timer_get_time() - gapT0);
        perf3D.gapPx += filled;
      }
    }
    if (fillBuf) {
      memset(fillBuf, 0, fillBytes); // RGB565 black
      xSemaphoreGive(w3dFillDone3D);
    }
    int64_t t0 = esp_timer_get_time();
    if (!w3dI80Push3D || !tft.pushRectI80(0, 0, w3dPushW3D, w3dPushH3D, w3dPushNew3D))
      tft.pushRect(0, 0, w3dPushW3D, w3dPushH3D, w3dPushNew3D);
    w3dPushUs3D = (int32_t)(esp_timer_get_time() - t0);
    xSemaphoreGive(w3dPushDone3D);
    xSemaphoreGive(w3dWorkDone3D); // hand-off variables are safe to rewrite
  }
}

// Created on the first pipelined frame so games that never use 3D pay nothing.
static bool w3dPipelineReady3D() {  if (w3dPushTask3DHandle) return true;
  w3dPushReady3D = xSemaphoreCreateBinary();
  w3dPushDone3D = xSemaphoreCreateBinary();
  w3dFillDone3D = xSemaphoreCreateBinary();
  w3dWorkDone3D = xSemaphoreCreateBinary();
  if (!w3dPushReady3D || !w3dPushDone3D || !w3dFillDone3D || !w3dWorkDone3D) return false;
  if (xTaskCreatePinnedToCore(w3dPushTaskLoop3D, "w3dblit", 8192, nullptr, 2,
                              &w3dPushTask3DHandle, 0) != pdPASS) {
    w3dPushTask3DHandle = nullptr;
    return false;
  }
  return true;
}

// Wait out a pipelined push so direct TFT writes cannot race with it.
void quiesceWorld3DBlit3D() {
  barrierWorld3DFill3D();
  if (w3dPushPending3D) {
    xSemaphoreTake(w3dPushDone3D, portMAX_DELAY);
    xSemaphoreTake(w3dWorkDone3D, portMAX_DELAY); // consume the drain too, so
    w3dPushPending3D = false;                     // no stale token outlives us
  }
  w3dMaskActive3D = false;
  w3dDrawMask3D = nullptr;
}

// Wait until the core 0 task has finished its legacy full-buffer fill of the
// frame buffer this frame will draw into. Mask mode never fills on core 0
// ahead of time, so there the check is a flag test.
void barrierWorld3DFill3D() {
  if (w3dFillWait3D) {
    xSemaphoreTake(w3dFillDone3D, portMAX_DELAY);
    w3dFillWait3D = false;
  }
}

static inline uint32_t* currentDrawMask3D(DISPLAY_T* display) {
  if (!w3dMaskActive3D || !display || display != w3dMaskDisplay3D) return nullptr;
  int8_t fid = display->frameBufferId();
  if (fid != w3dMaskFrameId3D || fid < 1 || fid > 2) return nullptr;
  return w3dMask3D[fid - 1];
}

bool world3DMaskMode3D(DISPLAY_T* display) {
  return currentDrawMask3D(display) != nullptr;
}

// Frame start for full-screen game loops, in place of fillSprite(TFT_BLACK).
// In mask mode the frame buffers are never wiped whole: clearing this draw
// target's coverage mask is the whole "clear", and the core 0 task memsets
// the uncovered pixels of each buffer right before pushing it. Without mask
// mode this keeps the previous behavior (pipelined background fill of the
// next buffer, or a synchronous fill here).
void beginWorld3DFill3D(DISPLAY_T* display) {
  if (!display || !display->created()) return;
  barrierWorld3DFill3D();
  if (w3dPipelineEnabled3D && display->frameBufferId() && w3dPipelineReady3D()
      && ensureMask3D(display->width(), display->height())) {
    memset(w3dMask3D[display->frameBufferId() - 1], 0,
           (size_t)w3dMaskWords3D * w3dMaskRows3D * 4);
    w3dMaskDisplay3D = display;
    w3dMaskFrameId3D = display->frameBufferId();
    w3dMaskActive3D = true;
    w3dDrawMask3D = w3dMask3D[display->frameBufferId() - 1];
    return;
  }
  w3dMaskActive3D = false;
  w3dDrawMask3D = nullptr;
  if (w3dFillWait3D && w3dFillBuf3D == (uint16_t*)display->getPointer()) return;
  display->fillSprite(TFT_BLACK);
}

// 2D writers on the framebuffer call this BEFORE drawing: the region's still
// uncovered pixels are blacked and the region is marked covered, so the
// pending gap fill can neither leave stale pixels in the region nor erase
// what the 2D write is about to paint there. Outside mask mode it is a no-op.
void claimFramebufferRect3D(DISPLAY_T* display, int16_t x, int16_t y, int16_t w, int16_t h) {
  uint32_t* mask = currentDrawMask3D(display);
  if (!mask || w <= 0 || h <= 0) return;
  int32_t x0 = x, y0 = y, x1 = (int32_t)x + w, y1 = (int32_t)y + h;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > (int32_t)w3dMaskWidth3D) x1 = w3dMaskWidth3D;
  if (y1 > (int32_t)w3dMaskRows3D) y1 = w3dMaskRows3D;
  if (x0 >= x1 || y0 >= y1) return;
  uint16_t* fb = (uint16_t*)display->getPointer();
  int32_t w0 = x0 >> 5, w1 = (x1 - 1) >> 5;
  for (int32_t row = y0; row < y1; row++) {
    uint32_t* mrow = mask + row * w3dMaskWords3D;
    uint16_t* fbRow = fb + row * (int32_t)w3dMaskWidth3D;
    for (int32_t wi = w0; wi <= w1; wi++) {
      uint32_t valid = (wi + 1 == w3dMaskWords3D) ? w3dMaskTail3D : 0xFFFFFFFFu;
      int32_t lo = x0 - (wi << 5); if (lo < 0) lo = 0;
      int32_t hi = x1 - 1 - (wi << 5); if (hi > 31) hi = 31;
      uint32_t bits = valid & maskRange3D(lo, hi);
      uint32_t uncovered = bits & ~mrow[wi];
      if (uncovered) gapFillWord3D(fbRow + (wi << 5), uncovered);
      mrow[wi] |= bits;
    }
  }
}

// FillSprite semantics for a mask-mode framebuffer: black only needs the
// mask cleared (the gap fill will black every untouched pixel at push time);
// any other colour must really paint the buffer and then mark it fully
// covered so the gap fill leaves it alone. Returns true when mask mode
// handled the call, false when the caller has to fill itself.
bool fillFramebufferMasked3D(DISPLAY_T* display, uint16_t color) {
  uint32_t* mask = currentDrawMask3D(display);
  if (!mask) return false;
  if (color == TFT_BLACK) {
    memset(mask, 0, (size_t)w3dMaskWords3D * w3dMaskRows3D * 4);
    return true;
  }
  display->fillScreen(color);
  memset(mask, 0xFF, (size_t)w3dMaskWords3D * w3dMaskRows3D * 4);
  return true;
}

// Claim the rectangle a drawString/print call at (x, y) can paint, honoring
// the current text datum; over-claiming is safe, it only costs memset.
void claimTextRect3D(DISPLAY_T* display, const char* text, int16_t x, int16_t y) {
  if (!world3DMaskMode3D(display)) return;
  int32_t tw = display->textWidth(text);
  if (tw <= 0) tw = (int32_t)strlen(text) * (display->textsize * 6 + 1);
  int32_t th = display->fontHeight();
  if (th <= 0) th = 8 * display->textsize;
  uint8_t datum = display->getTextDatum();
  int32_t hx = datum % 3, vy = datum / 3;
  int32_t x0 = x - hx * (tw + 1) / 2;
  int32_t x1 = x + (2 - hx) * (tw + 1) / 2;
  int32_t y0 = y - vy * (th + 1) / 2;
  int32_t y1 = y + (2 - vy) * (th + 1) / 2;
  claimFramebufferRect3D(display, (int16_t)x0, (int16_t)y0,
                         (int16_t)(x1 - x0 + 2), (int16_t)(y1 - y0 + 2));
}

// Switch between pipelined and synchronous blit for on-device perf A/B
// (systemconfig.ini renderPipeline3D).
void setWorld3DPipeline3D(bool enabled) {
  w3dPipelineEnabled3D = enabled;
  if (!enabled) quiesceWorld3DBlit3D();
}

// Switch the pipelined full-frame push between the LCD_CAM/GDMA bus driver and
// bit-banged pushRect for on-device perf A/B (systemconfig.ini renderI80Push3D).
void setWorld3DI80Push3D(bool enabled) {
  w3dI80Push3D = enabled;
}

// End-of-frame display push for full-screen game loops: pipelined whenever
// the display sprite is double-buffered, the plain synchronous sprite push
// otherwise. Reports the frame to the perf counters when debug logging is
// on, including the blit time the push task measured (or the synchronous
// push time). TFT_eSPI::pushImage and pushImageFast hold a bus mutex, so a
// synchronous push from another context waits for an in-flight pipelined
// push instead of interleaving with it.
void blitWorld3DFrame3D(DISPLAY_T* display) {
  if (!display || !display->created()) return;
  int64_t perfT0 = perf3DEnabled ? esp_timer_get_time() : 0;
  bool pipelined = false;
  int8_t drawn = display->frameBufferId();
  if (w3dPipelineEnabled3D && drawn && w3dPipelineReady3D()) {
    if (w3dPushPending3D) {
      // Commit wait: the displayed buffer must be fully pushed before it is
      // redrawn, and the task must have consumed the previous hand-off
      // variables before this blit rewrites them.
      xSemaphoreTake(w3dPushDone3D, portMAX_DELAY);
      xSemaphoreTake(w3dWorkDone3D, portMAX_DELAY);
    }
    w3dPushNew3D = (uint16_t*)display->frameBuffer(drawn);
    display->frameBuffer(drawn == 2 ? 1 : 2); // next frame draws into the other buffer
    if (w3dMaskActive3D && display == w3dMaskDisplay3D
        && display->width() == w3dMaskWidth3D && display->height() == w3dMaskRows3D
        && w3dMaskFrameId3D == drawn) {
      w3dGapBuf3D = w3dPushNew3D; // blank the untouched pixels of the buffer
      w3dGapMask3D = w3dMask3D[drawn - 1]; // we are about to display, on core 0
      w3dFillBuf3D = nullptr;
      w3dFillWait3D = false;
    } else if (w3d3DFrameActive3D) {
      w3dFillBuf3D = (uint16_t*)display->getPointer();
      w3dFillBytes3D = (uint32_t)display->width() * (uint32_t)display->height() * 2;
      w3dFillWait3D = true;
    } else {
      w3dFillBuf3D = nullptr;
      w3dFillWait3D = false;
    }
    w3dPushW3D = display->width();
    w3dPushH3D = display->height();
    w3dPushPending3D = true;
    xSemaphoreGive(w3dPushReady3D);
    pipelined = true;
    w3dBlitMode3D = "full";
  } else {
    quiesceWorld3DBlit3D();
    display->pushSpriteFast(0, 0);
    w3dBlitMode3D = "sync";
  }
  if (perf3DEnabled) {
    reportWorld3DFrame3D(pipelined ? w3dPushUs3D : (int32_t)(esp_timer_get_time() - perfT0));
  }
}

/*
 * Splitting the xf pass over both cores. A core 0 helper task transforms and
 * lights entities it claims from a shared queue (longest mesh first so the
 * big jobs start first on both cores, which auto-balances the tail), core 1
 * claims the rest; the fused pass has no inter-entity dependencies.
 *
 * The split xf phase transforms every entity before any face pass reads
 * scratch, but mesh->vs is a single per-mesh buffer: several entities can
 * share one mesh (the 3ddemo car's four wheels reuse two wheel meshes), so
 * one instance's transform would clobber another's before the face loop ever
 * sees it. Every entity therefore gets its own scratch slice each frame: the
 * first instance of a mesh uses mesh->vs, extra instances get a slice from a
 * PSRAM pool. Slices and their msync legs are aligned to whole 64-byte cache
 * lines, so no cache line is ever written by both cores in one frame. Before
 * releasing the helper, core 1 flushes the shared block and invalidates its
 * own copies of every candidate scratch slice; the helper invalidates the
 * shared block and the vtx streams it reads, and writes back each scratch
 * slice it owns before signaling done. Core 1 then re-reads the helper's
 * result. The hand-off legs are exact-line ranges: esp_cache_msync rejects
 * the UNALIGNED flag for M2C, and an invalidate leg must never reach into
 * data it does not own.
 */
#define w3dXfCtx3D (w3dSplitShared3D.ctx)
#define w3dXfHelperRes3D (w3dSplitShared3D.res)
#define w3dXfScratch3D (w3dSplitShared3D.scratch)

static bool w3dSplitXf3D = true;
static bool w3dSplitTried3D = false;
static SemaphoreHandle_t w3dXfStart3D = nullptr;
static SemaphoreHandle_t w3dXfDone3D = nullptr;
static QueueHandle_t w3dXfQueue3D = nullptr;
static TaskHandle_t w3dXfTask3DHandle = nullptr;
static ViewVertex3D* w3dXfPool3D = nullptr;
static size_t w3dXfPoolVerts3D = 0;
static const size_t W3D_XF_POOL_MAX_VERTS = 8192; // 256 KB PSRAM ceiling for shared-instance scratch

static bool xfCacheWarn3D(const char* what) {
  static bool warned3D = false;
  if (!warned3D) {
    warned3D = true;
    Serial.printf("engine3d split: cache sync failed at %s\n", what);
  }
  return false;
}
static inline bool xfCacheFlush3D(void* addr, size_t len, const char* what) {
  if (esp_cache_msync(addr, len, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED) == ESP_OK) return true;
  return xfCacheWarn3D(what);
}
static inline bool xfCacheInvalidate3D(void* addr, size_t len, const char* what) {
  if (esp_cache_msync(addr, len, ESP_CACHE_MSYNC_FLAG_DIR_M2C) == ESP_OK) return true;
  return xfCacheWarn3D(what);
}

// Fused transform/project/light pass for one entity, writing into that
// entity's scratch slice. Returns the number of vertices transformed and
// marks the culled bit when the bounding-sphere frustum test skips the
// entity, so the face loop skips it exactly like the original fused loop did.
static uint32_t xfEntityPass3D(int16_t ei, const XfFrameCtx3D* ctx, uint64_t* culledBits, ViewVertex3D* vs) {
  Entity3D* e = &entities3D[ei];
  if (!e->inUse || e->hidden) return 0;
  if (e->meshHandle < 0 || e->meshHandle >= MESH3D_LIMIT || !meshes3D[e->meshHandle].inUse) return 0;
  Mesh3D* mesh = &meshes3D[e->meshHandle];
  uint16_t vertexCount = mesh->vertexCount;

  float rot[9], epos[3];
  entityWorldTransform3D(ei, rot, epos, 0);
  Mat3 rotMat, basisMat, cMat;
  memcpy(rotMat.m, rot, sizeof(rotMat.m));
  memcpy(basisMat.m, ctx->basis.m, sizeof(basisMat.m));
  mat3Mul3D(&basisMat, &rotMat, &cMat);
  const float* c = cMat.m;
  const float* basis = ctx->basis.m;
  float ex = epos[0] - ctx->camX, ey = epos[1] - ctx->camY, ez = epos[2] - ctx->camZ;
  float tx = basis[0]*ex + basis[1]*ey + basis[2]*ez;
  float ty = basis[3]*ex + basis[4]*ey + basis[5]*ez;
  float tz = basis[6]*ex + basis[7]*ey + basis[8]*ez;

  // Frustum cull using the transformed bounding sphere
  float maxScale = fabsf(e->sx);
  if (fabsf(e->sy) > maxScale) maxScale = fabsf(e->sy);
  if (fabsf(e->sz) > maxScale) maxScale = fabsf(e->sz);
  const Vector3D* bc = &mesh->boundsCenter;
  float ccx = c[0]*bc->x*e->sx + c[1]*bc->y*e->sy + c[2]*bc->z*e->sz + tx;
  float ccy = c[3]*bc->x*e->sx + c[4]*bc->y*e->sy + c[5]*bc->z*e->sz + ty;
  float ccz = c[6]*bc->x*e->sx + c[7]*bc->y*e->sy + c[8]*bc->z*e->sz + tz;
  float boundsRadius = mesh->boundsRadius * maxScale;
  if (ccz + boundsRadius < ctx->nearPlane) { culledBits[ei >> 6] |= 1ULL << (ei & 63); return 0; }
  if (ccz - boundsRadius > ctx->farPlane) { culledBits[ei >> 6] |= 1ULL << (ei & 63); return 0; }
  if (ccz - boundsRadius > ctx->nearPlane) {
    if (fabsf(ccx) > (ccz - boundsRadius) * ctx->tanHalfX + boundsRadius) { culledBits[ei >> 6] |= 1ULL << (ei & 63); return 0; }
    if (fabsf(ccy) > (ccz - boundsRadius) * ctx->tanHalfY + boundsRadius) { culledBits[ei >> 6] |= 1ULL << (ei & 63); return 0; }
  }

  uint32_t baseR = ((uint32_t)e->color[0] * mesh->color[0] + 128) / 255;
  uint32_t baseG = ((uint32_t)e->color[1] * mesh->color[1] + 128) / 255;
  uint32_t baseB = ((uint32_t)e->color[2] * mesh->color[2] + 128) / 255;

  /*
   * One fused pass transforms, projects and lights each vertex. These
   * scratch arrays are PSRAM streams far larger than the ESP32-S3 data
   * cache, so running the passes as separate loops reloads nearly every
   * vertex from PSRAM a second time.
   */
  const bool dynamicLighting = ctx->dynamicLighting;
  const float nearPlane = ctx->nearPlane;
  for (uint16_t vi = 0; vi < vertexCount; vi++) {
    const MeshVertex3D* mv = &mesh->vtx[vi];
    const Vector3D* p = &mv->pos;
    float px = p->x * e->sx, py = p->y * e->sy, pz = p->z * e->sz;
    float vpX = c[0]*px + c[1]*py + c[2]*pz + tx;
    float vpY = c[3]*px + c[4]*py + c[5]*pz + ty;
    float vpZ = c[6]*px + c[7]*py + c[8]*pz + tz;
    ViewVertex3D* v = &vs[vi];
    v->x = vpX; v->y = vpY; v->z = vpZ;
    if (vpZ >= nearPlane) {
      v->iz = fastRecip3D(vpZ);
      projectClipPoint3D(&v->x, ctx->projScale * v->iz, ctx->vpCx, ctx->vpCy, &v->sx, &v->sy);
    }
    if (dynamicLighting) {
      const Vector3D* n = &mv->nrm;
      float nx = c[0]*n->x + c[1]*n->y + c[2]*n->z;
      float ny = c[3]*n->x + c[4]*n->y + c[5]*n->z;
      float nz = c[6]*n->x + c[7]*n->y + c[8]*n->z;
      float lr = ctx->ambient[0], lg = ctx->ambient[1], lb = ctx->ambient[2];
      for (int8_t la = 0; la < ctx->activeLightCount; la++) {
        const Light3D* l = &lights3D[ctx->activeLights[la]];
        if (l->type == LIGHT3D_TYPE_DIRECTIONAL) {
          float ndl = nx*l->tx + ny*l->ty + nz*l->tz;
          if (ndl > 0.0f) {
            lr += l->r * ndl;
            lg += l->g * ndl;
            lb += l->b * ndl;
          }
        } else {
          float ldx = l->vx - vpX, ldy = l->vy - vpY, ldz = l->vz - vpZ;
          float d2 = ldx*ldx + ldy*ldy + ldz*ldz;
          if (d2 >= l->rangeSq) continue;
          // dist only feeds an 8-bit light term and a cone test, so the approximate
          // reciprocal square root replaces both a sqrtf() call and a float divide.
          float inv = (d2 < 1e-6f) ? 1000.0f : fastInvSqrt3D(d2);
          float dist = d2 * inv;
          float ndl = (nx*ldx + ny*ldy + nz*ldz) * inv;
          if (ndl <= 0.0f) continue;
          float att = 1.0f - dist * l->invRange;
          if (att <= 0.0f) continue;
          if (l->type == LIGHT3D_TYPE_SPOT) {
            float ca = -(ldx*l->bx + ldy*l->by + ldz*l->bz) * inv;
            if (ca <= l->cosOuter) continue;
            if (ca < l->cosInner) {
              float ct = (ca - l->cosOuter) * l->cosInvDelta;
              att *= ct * ct * (3.0f - 2.0f * ct);
              if (att <= 0.0f) continue;
            }
          }
          lr += l->r * ndl * att;
          lg += l->g * ndl * att;
          lb += l->b * ndl * att;
        }
      }
      if (lr > 1.0f) lr = 1.0f; else if (lr < 0.0f) lr = 0.0f;
      if (lg > 1.0f) lg = 1.0f; else if (lg < 0.0f) lg = 0.0f;
      if (lb > 1.0f) lb = 1.0f; else if (lb < 0.0f) lb = 0.0f;
      uint32_t cr = clamp3DI((int32_t)(lr * baseR), 0, 255);
      uint32_t cg = clamp3DI((int32_t)(lg * baseG), 0, 255);
      uint32_t cb = clamp3DI((int32_t)(lb * baseB), 0, 255);
      vs[vi].color = (cr << 16) | (cg << 8) | cb;
    }
  }
  return vertexCount;
}

// Face-building pass for one entity: reads the scratch slice the xf pass
// produced, runs backface/viewport rejection and near-plane clipping and
// appends draw records. Core 1 only.
static void faceEntityPass3D(int16_t ei, const XfFrameCtx3D* ctx, ViewVertex3D* vs, float* zLo, float* zHi) {
  Entity3D* e = &entities3D[ei];
  if (!e->inUse || e->hidden) return;
  if (e->meshHandle < 0 || e->meshHandle >= MESH3D_LIMIT || !meshes3D[e->meshHandle].inUse) return;
  Mesh3D* mesh = &meshes3D[e->meshHandle];
  uint16_t vertexCount = mesh->vertexCount;
  const float nearPlane = ctx->nearPlane;
  const bool dynamicLighting = ctx->dynamicLighting;

  uint32_t baseR = ((uint32_t)e->color[0] * mesh->color[0] + 128) / 255;
  uint32_t baseG = ((uint32_t)e->color[1] * mesh->color[1] + 128) / 255;
  uint32_t baseB = ((uint32_t)e->color[2] * mesh->color[2] + 128) / 255;
  uint32_t uniformR = clamp3DI((int32_t)(ctx->ambient[0] * baseR + 0.5f), 0, 255);
  uint32_t uniformG = clamp3DI((int32_t)(ctx->ambient[1] * baseG + 0.5f), 0, 255);
  uint32_t uniformB = clamp3DI((int32_t)(ctx->ambient[2] * baseB + 0.5f), 0, 255);
  uint32_t uniformColor = (uniformR << 16) | (uniformG << 8) | uniformB;

  const int32_t* uvQ = mesh->uvQ;
  const Texture3D* tex = nullptr;
  if (mesh->textureHandle >= 0 && mesh->textureHandle < TEXTURE3D_LIMIT &&
      textures3D[mesh->textureHandle].inUse && uvQ) {
    tex = &textures3D[mesh->textureHandle];
  }
  uint8_t mode = tex ? (dynamicLighting ? 2 : 3) : (dynamicLighting ? 1 : 0);

  for (uint16_t fi = 0; fi < mesh->faceCount; fi++) {
    if (drawCount3D >= WORLD3D_MAX_TRIANGLES) break;
    const Face3D* f = &mesh->faces[fi];
    uint16_t ia = f->a;
    uint16_t ib = mesh->flipped ? f->c : f->b;
    uint16_t ic = mesh->flipped ? f->b : f->c;
    if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) continue;
    const ViewVertex3D* pa = &vs[ia];
    const ViewVertex3D* pb = &vs[ib];
    const ViewVertex3D* pc = &vs[ic];
    float za = pa->z, zb = pb->z, zc = pc->z;
    if (za < nearPlane && zb < nearPlane && zc < nearPlane) continue;
    float e1x = pb->x-pa->x, e1y = pb->y-pa->y, e1z = pb->z-pa->z;
    float e2x = pc->x-pa->x, e2y = pc->y-pa->y, e2z = pc->z-pa->z;
    float nx = e1y*e2z - e1z*e2y;
    float ny = e1z*e2x - e1x*e2z;
    float nz = e1x*e2y - e1y*e2x;
    // Front facing when the winding normal points towards the camera (origin)
    if (nx*(pa->x+pb->x+pc->x) + ny*(pa->y+pb->y+pc->y) + nz*(pa->z+pb->z+pc->z) >= 0.0f) continue;
    /* Screen bbox reject, only on the unclipped fast path: perspective maps
     * a triangle that stays behind the near plane exactly onto the triangle
     * of its projected vertices, so an entirely outside bbox cannot re-enter
     * the viewport through near-plane clipping later. */
    if (za >= nearPlane && zb >= nearPlane && zc >= nearPlane) {
      int32_t sminx = pa->sx, smaxx = pa->sx, sminy = pa->sy, smaxy = pa->sy;
      if (pb->sx < sminx) sminx = pb->sx; else if (pb->sx > smaxx) smaxx = pb->sx;
      if (pc->sx < sminx) sminx = pc->sx; else if (pc->sx > smaxx) smaxx = pc->sx;
      if (pb->sy < sminy) sminy = pb->sy; else if (pb->sy > smaxy) smaxy = pb->sy;
      if (pc->sy < sminy) sminy = pc->sy; else if (pc->sy > smaxy) smaxy = pc->sy;
      if (smaxx <= (vpL3D << 8) || sminx >= (vpR3D << 8)
          || smaxy <= (vpT3D << 8) || sminy >= (vpB3D << 8)) continue;
    }

    TriDraw3D* d = &drawList3D[drawCount3D];
    uint32_t colA = dynamicLighting ? pa->color : uniformColor;
    uint32_t colB = dynamicLighting ? pb->color : uniformColor;
    uint32_t colC = dynamicLighting ? pc->color : uniformColor;
    int32_t uvA0 = uvQ ? uvQ[ia*2] : 0;
    int32_t uvA1 = uvQ ? uvQ[ia*2+1] : 0;
    int32_t uvB0 = uvQ ? uvQ[ib*2] : 0;
    int32_t uvB1 = uvQ ? uvQ[ib*2+1] : 0;
    int32_t uvC0 = uvQ ? uvQ[ic*2] : 0;
    int32_t uvC1 = uvQ ? uvQ[ic*2+1] : 0;
    float zAvg;
    if (za >= nearPlane && zb >= nearPlane && zc >= nearPlane) {
      d->vertexCount = 3;
      d->sx[0] = pa->sx; d->sy[0] = pa->sy;
      d->sx[1] = pb->sx; d->sy[1] = pb->sy;
      d->sx[2] = pc->sx; d->sy[2] = pc->sy;
      float izA = pa->iz, izB = pb->iz, izC = pc->iz;
      d->ru[0] = uvA0 * izA; d->rv[0] = uvA1 * izA; d->rz[0] = izA;
      d->ru[1] = uvB0 * izB; d->rv[1] = uvB1 * izB; d->rz[1] = izB;
      d->ru[2] = uvC0 * izC; d->rv[2] = uvC1 * izC; d->rz[2] = izC;
      d->color[0] = colA;
      d->color[1] = colB;
      d->color[2] = colC;
      zAvg = (za + zb + zc) * 0.3333333f;
    } else {
      float clipPos[4][3];
      uint32_t clipCol[4];
      int32_t clipU[4], clipV[4];
      uint8_t clipCount = clipTriangleNear3D(nearPlane, &pa->x, &pb->x, &pc->x,
          colA, colB, colC, uvA0, uvA1, uvB0, uvB1, uvC0, uvC1,
          clipPos, clipCol, clipU, clipV);
      if (clipCount < 3) continue;
      d->vertexCount = clipCount;
      zAvg = 0.0f;
      for (uint8_t ci = 0; ci < clipCount; ci++) {
        float izc = fastRecip3D(clipPos[ci][2]);
        projectClipPoint3D(clipPos[ci], ctx->projScale * izc, ctx->vpCx, ctx->vpCy, &d->sx[ci], &d->sy[ci]);
        d->ru[ci] = clipU[ci] * izc;
        d->rv[ci] = clipV[ci] * izc;
        d->rz[ci] = izc;
        d->color[ci] = clipCol[ci];
        zAvg += clipPos[ci][2];
      }
      zAvg *= (clipCount == 3) ? 0.3333333f : 0.25f;
    }
    d->tex = tex ? tex->pixels : nullptr;
    d->texW = tex ? tex->w : 0;
    d->texH = tex ? tex->h : 0;
    d->texMask = tex ? tex->mask : -1;
    d->mode = mode;
    d->flatColor = uniformColor;
    d->zKey = zAvg;
    d->orderOff = e->order;
    if (zAvg < *zLo) *zLo = zAvg;
    if (zAvg > *zHi) *zHi = zAvg;
    if (perf3DEnabled) {
      perf3D.trisSubmitted++;
      if (d->vertexCount >= 4) perf3D.trisClipped++;
    }
    drawCount3D++;
  }
}

static void w3dXfTaskLoop3D(void*) {
  for (;;) {
    xSemaphoreTake(w3dXfStart3D, portMAX_DELAY);
    uint64_t culled[2] = {0, 0};
    int32_t verts = 0;
    bool usable = xfCacheInvalidate3D(&w3dSplitShared3D, sizeof(w3dSplitShared3D), "shared M2C");
    int16_t ei;
    if (usable) {
      while (xQueueReceive(w3dXfQueue3D, &ei, 0) == pdTRUE) {
        Mesh3D* m = &meshes3D[entities3D[ei].meshHandle];
        ViewVertex3D* sc = w3dXfScratch3D[ei];
        // Core 1 may have re-uploaded vtx data whose write-back still sits in
        // its private cache; drop any stale local copy before streaming it.
        if (m->vtx && !xfCacheInvalidate3D(m->vtx, meshVtxSpan3D(m->vertexCount), "helper vtx M2C")) break;
        uint32_t n = xfEntityPass3D(ei, &w3dXfCtx3D, culled, sc);
        verts += (int32_t)n;
        if (n && !xfCacheFlush3D(sc, viewVertsSpan3D(m->vertexCount), "helper scratch C2M")) break; // leave the rest to core 1
      }
    }
    w3dXfHelperRes3D.verts = verts;
    w3dXfHelperRes3D.culledLo = culled[0];
    w3dXfHelperRes3D.culledHi = culled[1];
    xfCacheFlush3D(&w3dSplitShared3D.res, sizeof(w3dSplitShared3D.res), "helper result C2M");
    xSemaphoreGive(w3dXfDone3D);
  }
}

// Created on the first split frame so single-core builds pay nothing.
static bool w3dSplitReady3D() {
  if (w3dXfTask3DHandle) return true;
  if (w3dSplitTried3D) return false;
  w3dSplitTried3D = true;
  w3dXfStart3D = xSemaphoreCreateBinary();
  w3dXfDone3D = xSemaphoreCreateBinary();
  w3dXfQueue3D = xQueueCreate(ENTITY3D_LIMIT, sizeof(int16_t));
  if (!w3dXfStart3D || !w3dXfDone3D || !w3dXfQueue3D) {
    Serial.println("engine3d split: semaphore/queue creation failed");
    return false;
  }
  if (xTaskCreatePinnedToCore(w3dXfTaskLoop3D, "w3dxf", 6144, nullptr, 1,
                              &w3dXfTask3DHandle, 0) != pdPASS) {
    w3dXfTask3DHandle = nullptr;
    Serial.println("engine3d split: helper task creation failed");
    return false;
  }
  return true;
}

void setWorld3DSplitXf3D(bool enabled) {
  w3dSplitXf3D = enabled;
}

void updateWorld3D(DISPLAY_T* display) {
  if (!ensureWorld3D()) return;
  int64_t perfT0 = perf3DEnabled ? esp_timer_get_time() : 0;
  int64_t perfSub3D = perfT0;
  drawCount3D = 0;
  if (!display || !display->created()) {
    Serial.println("engine3d: updateWorld called with an invalid display!");
    return;
  }
  int8_t camIdx = -1;
  for (int8_t i = 0; i < CAMERA3D_LIMIT; i++) {
    if (cameras3D[i].inUse) { camIdx = i; break; }
  }
  if (camIdx < 0) return;
  Camera3D* cam = &cameras3D[camIdx];
  if (cam->nearPlane < 0.01f) cam->nearPlane = 0.01f;
  if (cam->farPlane <= cam->nearPlane) cam->farPlane = cam->nearPlane + 100.0f;
  if (cam->zoom < 0.01f) cam->zoom = 0.01f;
  resolveViewport3D(display->width(), display->height());
  float screenH = (float)display->height();
  // Fixed projection scale relative to the viewport center: the same world
  // point projects to the same pixel offset no matter where the viewport sits,
  // so moving a viewport of unchanged size shifts the image by exactly that
  // offset; resizing it crops or reveals, never zooms. The frustum window is
  // then the viewport's angular extent at this scale.
  float projScale = cam->zoom * screenH * PROJ3D_FACTOR;
  float tanHalfX = (vpR3D - vpL3D) * 0.5f / projScale;
  float tanHalfY = (vpB3D - vpT3D) * 0.5f / projScale;

  Mat3 camRot, basis;
  mat3FromEuler3D(cam->pitch, cam->yaw, cam->roll, &camRot);
  mat3Transpose3D(&camRot, &basis); // rows are the camera axes in world space

  int8_t activeLights[LIGHT3D_LIMIT];
  int activeLightCount = 0;
  for (int8_t li = 0; li < LIGHT3D_LIMIT; li++) {
    Light3D* l = &lights3D[li];
    if (!l->inUse) continue;
    float lx = l->x - cam->x, ly = l->y - cam->y, lz = l->z - cam->z;
    l->vx = basis.m[0]*lx + basis.m[1]*ly + basis.m[2]*lz;
    l->vy = basis.m[3]*lx + basis.m[4]*ly + basis.m[5]*lz;
    l->vz = basis.m[6]*lx + basis.m[7]*ly + basis.m[8]*lz;
    l->bx = basis.m[0]*l->dx + basis.m[1]*l->dy + basis.m[2]*l->dz;
    l->by = basis.m[3]*l->dx + basis.m[4]*l->dy + basis.m[5]*l->dz;
    l->bz = basis.m[6]*l->dx + basis.m[7]*l->dy + basis.m[8]*l->dz;
    l->tx = -l->bx;
    l->ty = -l->by;
    l->tz = -l->bz;
    if (l->r + l->g + l->b > 0.001f) activeLights[activeLightCount++] = li;
  }
  bool dynamicLighting = activeLightCount > 0;

  memset(bucketHeads3D, 0xFF, sizeof(int16_t) * BUCKET3D_COUNT);
  // Bucket the sorted span of actually-drawn triangles rather than the
  // camera's near..far range: with a large farPlane the bucket width exceeds
  // the depth gap between adjacent surfaces, so small view rotations flip
  // draw order and surfaces flicker against each other.
  float zLo = 1e30f, zHi = -1e30f;

  /* Per-frame setup shared with the core 0 helper (core 1 uses it too). */
  XfFrameCtx3D* ctx = &w3dXfCtx3D;
  memcpy(ctx->basis.m, basis.m, sizeof(ctx->basis.m));
  ctx->camX = cam->x; ctx->camY = cam->y; ctx->camZ = cam->z;
  ctx->nearPlane = cam->nearPlane; ctx->farPlane = cam->farPlane;
  ctx->projScale = projScale; ctx->vpCx = vpCx3D; ctx->vpCy = vpCy3D;
  ctx->tanHalfX = tanHalfX; ctx->tanHalfY = tanHalfY;
  ctx->ambient[0] = ambient3D[0]; ctx->ambient[1] = ambient3D[1]; ctx->ambient[2] = ambient3D[2];
  memcpy(ctx->activeLights, activeLights, sizeof(activeLights));
  ctx->activeLightCount = (int8_t)activeLightCount;
  ctx->dynamicLighting = dynamicLighting;

  uint64_t xfCulled3D[2] = {0, 0};
  uint64_t xfClaimed3D[2] = {0, 0};
  static int16_t xfQueued3D[ENTITY3D_LIMIT];
  int16_t xfQueuedCount3D = 0;
  int32_t xfVerts3D = 0;
  bool splitXf3D = false;

  if (perf3DEnabled) perfSub3D = esp_timer_get_time();

  if (w3dSplitXf3D && w3dSplitReady3D()) {
    // Queue every valid entity, longest mesh first: the big jobs are claimed
    // before either core runs dry and the tail of small meshes absorbs the
    // imbalance. Culling stays inside the passes so both cores decide alike.
    for (int16_t ei = 0; ei < ENTITY3D_LIMIT; ei++) {
      Entity3D* e = &entities3D[ei];
      if (!e->inUse || e->hidden) continue;
      if (e->meshHandle < 0 || e->meshHandle >= MESH3D_LIMIT || !meshes3D[e->meshHandle].inUse) continue;
      uint16_t vc = meshes3D[e->meshHandle].vertexCount;
      int16_t pos = xfQueuedCount3D++;
      while (pos > 0 &&
             meshes3D[entities3D[xfQueued3D[pos - 1]].meshHandle].vertexCount < vc) {
        xfQueued3D[pos] = xfQueued3D[pos - 1];
        pos--;
      }
      xfQueued3D[pos] = ei;
    }
    /* mesh->vs scratch is per-mesh, but several live entities can share one
     * mesh (the 3ddemo car's four wheels reuse two wheel meshes). The split
     * xf phase transforms every entity before any face pass reads scratch,
     * so each instance past the first gets its own 64-byte aligned slice
     * from a PSRAM pool; when the pool cannot cover the scene, this frame
     * runs sequentially instead (sequential interleaves xf and face per
     * entity, where shared scratch is safe). */
    static int8_t instSeen3D[MESH3D_LIMIT];
    size_t poolVerts = 0;
    memset(instSeen3D, 0, sizeof(instSeen3D));
    for (int16_t qi = 0; qi < xfQueuedCount3D; qi++) {
      Mesh3D* m = &meshes3D[entities3D[xfQueued3D[qi]].meshHandle];
      if (instSeen3D[m - meshes3D]++) poolVerts += (m->vertexCount + 1) & ~1;
    }
    bool poolOk = poolVerts <= w3dXfPoolVerts3D;
    if (!poolOk && poolVerts <= W3D_XF_POOL_MAX_VERTS) {
      heap_caps_free(w3dXfPool3D);
      w3dXfPool3D = (ViewVertex3D*)heap_caps_aligned_alloc(64, poolVerts * sizeof(ViewVertex3D), MALLOC_CAP_SPIRAM);
      w3dXfPoolVerts3D = w3dXfPool3D ? poolVerts : 0;
      poolOk = w3dXfPool3D != nullptr;
    }
    if (poolOk) {
      size_t poolOff = 0;
      memset(instSeen3D, 0, sizeof(instSeen3D));
      for (int16_t qi = 0; qi < xfQueuedCount3D; qi++) {
        int16_t ei = xfQueued3D[qi];
        Mesh3D* m = &meshes3D[entities3D[ei].meshHandle];
        if (instSeen3D[m - meshes3D]++) {
          w3dXfScratch3D[ei] = w3dXfPool3D + poolOff;
          poolOff += (m->vertexCount + 1) & ~1;
        } else {
          w3dXfScratch3D[ei] = m->vs;
        }
      }
      bool synced = xfCacheFlush3D(&w3dSplitShared3D, sizeof(w3dSplitShared3D), "shared C2M");
      for (int16_t qi = 0; synced && qi < xfQueuedCount3D; qi++) {
        Mesh3D* m = &meshes3D[entities3D[xfQueued3D[qi]].meshHandle];
        synced = m->vtx && m->vertexCount
          ? xfCacheFlush3D(m->vtx, meshVtxSpan3D(m->vertexCount), "vtx C2M")
          : true;
      }
      // Whichever core ends up claiming a slice, core 1's previous-frame
      // copies of every candidate slice must be gone from its cache before
      // the helper writes them. Invalidate only: no PSRAM traffic.
      for (int16_t qi = 0; synced && qi < xfQueuedCount3D; qi++) {
        int16_t ei = xfQueued3D[qi];
        Mesh3D* m = &meshes3D[entities3D[ei].meshHandle];
        synced = w3dXfScratch3D[ei] && m->vertexCount
          ? xfCacheInvalidate3D(w3dXfScratch3D[ei], viewVertsSpan3D(m->vertexCount), "scratch M2C")
          : true;
      }
      if (synced && xQueueReset(w3dXfQueue3D) == pdTRUE) {
        for (int16_t qi = 0; qi < xfQueuedCount3D; qi++)
          if (xQueueSend(w3dXfQueue3D, &xfQueued3D[qi], 0) != pdTRUE) { synced = false; break; }
        if (synced) synced = (xSemaphoreGive(w3dXfStart3D) == pdTRUE);
      }
      if (!synced) w3dSplitXf3D = false; // drop permanently to the single-core path
      splitXf3D = synced;
    }
  }

  bool splitResOk3D = false;
  if (splitXf3D) {
    int16_t ei;
    while (xQueueReceive(w3dXfQueue3D, &ei, 0) == pdTRUE) {
      xfClaimed3D[ei >> 6] |= 1ULL << (ei & 63);
      xfVerts3D += (int32_t)xfEntityPass3D(ei, ctx, xfCulled3D, w3dXfScratch3D[ei]);
    }
    xSemaphoreTake(w3dXfDone3D, portMAX_DELAY);
    int32_t helperVerts3D = 0;
    // The helper's result lives in the shared block; this core's copies of
    // it (and of everything the helper may have touched) are stale now.
    splitResOk3D = xfCacheInvalidate3D(&w3dSplitShared3D, sizeof(w3dSplitShared3D), "shared result M2C");
    if (splitResOk3D) {
      helperVerts3D = w3dXfHelperRes3D.verts;
      xfCulled3D[0] |= w3dXfHelperRes3D.culledLo;
      xfCulled3D[1] |= w3dXfHelperRes3D.culledHi;
      xfVerts3D += helperVerts3D;
    } else {
      w3dSplitXf3D = false;
    }
    static bool reported3D = false;
    static uint8_t reportFrames3D = 0;
    if (!reported3D && (helperVerts3D > 0 || reportFrames3D < 5)) {
      reportFrames3D++;
      if (helperVerts3D > 0) reported3D = true;
      Serial.printf("engine3d split: frame core1=%d helper=%d queued=%d\n",
                    (int)(xfVerts3D - helperVerts3D), (int)helperVerts3D, (int)xfQueuedCount3D);
    }
    if (perf3DEnabled) {
      perf3D.vertsTransformed += xfVerts3D;
      perf3D.xfUs += (int32_t)(esp_timer_get_time() - perfSub3D);
      perfSub3D = esp_timer_get_time();
    }
  } else {
    // Sequential fused order, matching the original loop: xf and face
    // interleave per entity, so shared mesh scratch cannot clobber instances.
    for (int16_t ei = 0; ei < ENTITY3D_LIMIT; ei++) {
      Entity3D* e = &entities3D[ei];
      if (!e->inUse || e->hidden) continue;
      if (e->meshHandle < 0 || e->meshHandle >= MESH3D_LIMIT || !meshes3D[e->meshHandle].inUse) continue;
      ViewVertex3D* vs = meshes3D[e->meshHandle].vs;
      if (perf3DEnabled) perfSub3D = esp_timer_get_time();
      uint32_t n = xfEntityPass3D(ei, ctx, xfCulled3D, vs);
      if (perf3DEnabled) {
        perf3D.vertsTransformed += (int32_t)n;
        perf3D.xfUs += (int32_t)(esp_timer_get_time() - perfSub3D);
        perfSub3D = esp_timer_get_time();
      }
      if (xfCulled3D[ei >> 6] & (1ULL << (ei & 63))) continue;
      faceEntityPass3D(ei, ctx, vs, &zLo, &zHi);
      if (perf3DEnabled) {
        perf3D.faceUs += (int32_t)(esp_timer_get_time() - perfSub3D);
        perfSub3D = esp_timer_get_time();
      }
    }
  }

  if (splitXf3D) {
    for (int16_t ei = 0; ei < ENTITY3D_LIMIT; ei++) {
      if (xfCulled3D[ei >> 6] & (1ULL << (ei & 63))) continue;
      // With the helper's done-result lost, helper-owned entities have scratch
      // state that cannot be trusted; skip them this frame.
      if (!splitResOk3D && !(xfClaimed3D[ei >> 6] & (1ULL << (ei & 63)))) continue;
      faceEntityPass3D(ei, ctx, w3dXfScratch3D[ei], &zLo, &zHi);
    }
    if (perf3DEnabled) {
      perf3D.faceUs += (int32_t)(esp_timer_get_time() - perfSub3D);
      perfSub3D = esp_timer_get_time();
    }
  }

  float span = zHi - zLo;
  if (span < 1e-4f) span = 1e-4f;
  float bucketScale = 4095.0f * fastRecip3D(span);
  for (int32_t i = 0; i < drawCount3D; i++) {
    TriDraw3D* d = &drawList3D[i];
    int32_t bucket = (int32_t)((d->zKey - zLo) * bucketScale) + (int32_t)d->orderOff * 32;
    if (bucket < 0) bucket = 0;
    else if (bucket >= BUCKET3D_COUNT) bucket = BUCKET3D_COUNT - 1;
    d->next = bucketHeads3D[bucket];
    bucketHeads3D[bucket] = (int16_t)i;
  }
  if (perf3DEnabled) {
    int64_t perfT1 = esp_timer_get_time();
    perf3D.bucketUs += (int32_t)(perfT1 - perfSub3D);
    perf3D.updateUs += (int32_t)(perfT1 - perfT0);
  }
}
// ---------------------------------------------------------------------------
// Rasterizer: integer span filling with Gouraud shading (screen-space) and
// perspective-correct textures (u/z and 1/z interpolated, divided per pixel)
// ---------------------------------------------------------------------------

static inline uint16_t sampleTexel3D(const uint16_t* tex, int32_t tw, int32_t th,
    int32_t mask, int32_t uQ16, int32_t vQ16) {
  // uvQ is normalized (1.0 == one full texture repeat, clamped to +-16 at
  // load), so scale by the texture size before wrapping. Without the scale,
  // u=0..1 would only ever hit texels 0..1 and faces get tinted by a single
  // texture column.
  int32_t tx = (uQ16 * tw) >> 16;
  int32_t ty = (vQ16 * th) >> 16;
  if (mask >= 0) {
    tx &= mask;
    ty &= mask;
  } else {
    tx %= tw;
    if (tx < 0) tx += tw;
    ty %= th;
    if (ty < 0) ty += th;
  }
  // TFT_eSPI 16bpp sprite buffers hold RGB565 byteswapped (as stored by loadBmp
  // and the sprite draw calls in gfxHandler) — unswap before channel math
  return __builtin_bswap16(tex[ty * tw + tx]);
}

static void rasterTriPart3D(TriDraw3D* t, uint8_t i0, uint8_t i1, uint8_t i2) {
  int32_t x0 = t->sx[i0], y0 = t->sy[i0];
  int32_t x1 = t->sx[i1], y1 = t->sy[i1];
  int32_t x2 = t->sx[i2], y2 = t->sy[i2];
  uint32_t c0 = t->color[i0], c1 = t->color[i1], c2 = t->color[i2];
  float u0 = t->ru[i0], v0 = t->rv[i0], w0 = t->rz[i0];
  float u1 = t->ru[i1], v1 = t->rv[i1], w1 = t->rz[i1];
  float u2 = t->ru[i2], v2 = t->rv[i2], w2 = t->rz[i2];
  uint8_t mode = t->mode;
  const uint16_t* texPix = t->tex;
  if ((mode == 2 || mode == 3) && !texPix) mode = (mode == 2) ? 1 : 0;

  if (y0 > y1) {
    int32_t tv; uint32_t tc; float tf;
    tv = x0; x0 = x1; x1 = tv;
    tv = y0; y0 = y1; y1 = tv;
    tf = u0; u0 = u1; u1 = tf;
    tf = v0; v0 = v1; v1 = tf;
    tf = w0; w0 = w1; w1 = tf;
    tc = c0; c0 = c1; c1 = tc;
  }
  if (y1 > y2) {
    int32_t tv; uint32_t tc; float tf;
    tv = x1; x1 = x2; x2 = tv;
    tv = y1; y1 = y2; y2 = tv;
    tf = u1; u1 = u2; u2 = tf;
    tf = v1; v1 = v2; v2 = tf;
    tf = w1; w1 = w2; w2 = tf;
    tc = c1; c1 = c2; c2 = tc;
  }
  if (y0 > y1) {
    int32_t tv; uint32_t tc; float tf;
    tv = x0; x0 = x1; x1 = tv;
    tv = y0; y0 = y1; y1 = tv;
    tf = u0; u0 = u1; u1 = tf;
    tf = v0; v0 = v1; v1 = tf;
    tf = w0; w0 = w1; w1 = tf;
    tc = c0; c0 = c1; c1 = tc;
  }
  if (y2 - y0 <= 0) return;
  int32_t syStart = (y0 + 255) >> 8;
  if (syStart < vpT3D) syStart = vpT3D;
  int32_t syEnd = y2 >> 8;
  if (syEnd >= vpB3D) syEnd = vpB3D - 1;
  if (syStart > syEnd) return;

  bool wantColor = (mode == 1 || mode == 2);
  bool wantUV = (mode == 2 || mode == 3);

  int32_t syV0 = (y0 + 255) >> 8;
  if (syV0 < 0) syV0 = 0;
  int32_t syV1 = (y1 + 255) >> 8;
  if (syV1 < 0) syV1 = 0;
  int32_t syMid = (syV1 > syStart) ? syV1 : syStart;

  int32_t dy01 = y1 - y0;
  int32_t dy02 = y2 - y0;
  int32_t dy12 = y2 - y1;
  int32_t sAx = (dy02 > 0) ? ((x2 - x0) * 256) / dy02 : 0;
  int32_t sBx = (dy01 > 0) ? ((x1 - x0) * 256) / dy01 : 0;
  int32_t sCx = (dy12 > 0) ? ((x2 - x1) * 256) / dy12 : 0;

  int32_t r0 = 0, g0 = 0, b0 = 0, r1 = 0, g1 = 0, b1 = 0, r2 = 0, g2 = 0, b2 = 0;
  int32_t sAr = 0, sAg = 0, sAb = 0, sBr = 0, sBg = 0, sBb = 0, sCr = 0, sCg = 0, sCb = 0;
  if (wantColor) {
    unpackColor3D(c0, &r0, &g0, &b0);
    unpackColor3D(c1, &r1, &g1, &b1);
    unpackColor3D(c2, &r2, &g2, &b2);
    if (dy02 > 0) {
      sAr = ((r2 - r0) * 256) / dy02;
      sAg = ((g2 - g0) * 256) / dy02;
      sAb = ((b2 - b0) * 256) / dy02;
    }
    if (dy01 > 0) {
      sBr = ((r1 - r0) * 256) / dy01;
      sBg = ((g1 - g0) * 256) / dy01;
      sBb = ((b1 - b0) * 256) / dy01;
    }
    if (dy12 > 0) {
      sCr = ((r2 - r1) * 256) / dy12;
      sCg = ((g2 - g1) * 256) / dy12;
      sCb = ((b2 - b1) * 256) / dy12;
    }
  }
  float sAu = 0, sAv = 0, sAw = 0, sBu = 0, sBv = 0, sBw = 0, sCu = 0, sCv = 0, sCw = 0;
  if (wantUV) {
    if (dy02 > 0) {
      float inv = 256.0f * fastRecip3D((float)dy02);
      sAu = (u2 - u0) * inv; sAv = (v2 - v0) * inv; sAw = (w2 - w0) * inv;
    }
    if (dy01 > 0) {
      float inv = 256.0f * fastRecip3D((float)dy01);
      sBu = (u1 - u0) * inv; sBv = (v1 - v0) * inv; sBw = (w1 - w0) * inv;
    }
    if (dy12 > 0) {
      float inv = 256.0f * fastRecip3D((float)dy12);
      sCu = (u2 - u1) * inv; sCv = (v2 - v1) * inv; sCw = (w2 - w1) * inv;
    }
  }

  /* Screen-space interpolation is affine, so the step from the left edge of a
   * span to its right edge is identical on every scanline of one triangle half:
   * both edges start at the same vertex, so their attribute difference and their
   * x difference grow with the same factor. Evaluating the ratio once per half
   * (at a representative scanline, where both are well conditioned) removes the
   * three colour and three texture divides from every scanline of the frame. */
  int32_t midUp = (syStart + syMid - 1) >> 1;
  if (midUp < syStart) midUp = syStart;
  int32_t midDn = (syMid + syEnd) >> 1;
  int32_t kUp = midUp - syV0;
  int32_t kDnA = midDn - syV0;
  int32_t kDnC = midDn - syV1;
  int32_t stepUpR = 0, stepUpG = 0, stepUpB = 0, stepDnR = 0, stepDnG = 0, stepDnB = 0;
  float stepUpU = 0, stepUpV = 0, stepUpW = 0, stepDnU = 0, stepDnV = 0, stepDnW = 0;
  if (wantColor || wantUV) {
    int32_t spanUp = (x0 + sAx * kUp) - (x0 + sBx * kUp);
    if (spanUp != 0) {
      float invSpan = 256.0f * fastRecip3D((float)spanUp);
      if (wantColor) {
        stepUpR = (int32_t)((float)((r0 + sAr * kUp) - (r0 + sBr * kUp)) * invSpan);
        stepUpG = (int32_t)((float)((g0 + sAg * kUp) - (g0 + sBg * kUp)) * invSpan);
        stepUpB = (int32_t)((float)((b0 + sAb * kUp) - (b0 + sBb * kUp)) * invSpan);
      }
      if (wantUV) {
        float kUpF = (float)kUp;
        stepUpU = ((u0 + sAu * kUpF) - (u0 + sBu * kUpF)) * invSpan;
        stepUpV = ((v0 + sAv * kUpF) - (v0 + sBv * kUpF)) * invSpan;
        stepUpW = ((w0 + sAw * kUpF) - (w0 + sBw * kUpF)) * invSpan;
      }
    }
    int32_t spanDn = (x0 + sAx * kDnA) - (x1 + sCx * kDnC);
    if (spanDn != 0) {
      float invSpan = 256.0f * fastRecip3D((float)spanDn);
      if (wantColor) {
        stepDnR = (int32_t)((float)((r0 + sAr * kDnA) - (r1 + sCr * kDnC)) * invSpan);
        stepDnG = (int32_t)((float)((g0 + sAg * kDnA) - (g1 + sCg * kDnC)) * invSpan);
        stepDnB = (int32_t)((float)((b0 + sAb * kDnA) - (b1 + sCb * kDnC)) * invSpan);
      }
      if (wantUV) {
        float kDnAF = (float)kDnA, kDnCF = (float)kDnC;
        stepDnU = ((u0 + sAu * kDnAF) - (u1 + sCu * kDnCF)) * invSpan;
        stepDnV = ((v0 + sAv * kDnAF) - (v1 + sCv * kDnCF)) * invSpan;
        stepDnW = ((w0 + sAw * kDnAF) - (w1 + sCw * kDnCF)) * invSpan;
      }
    }
  }

  uint16_t flat565 = 0;
  int32_t lightR5 = 31, lightG6 = 63, lightB5 = 31;
  if (mode == 0) {
    flat565 = __builtin_bswap16(rgbTo5653D(t->flatColor));
  } else if (mode == 3) {
    lightR5 = ((t->flatColor >> 16) & 0xFF) >> 3;
    lightG6 = ((t->flatColor >> 8) & 0xFF) >> 2;
    lightB5 = (t->flatColor & 0xFF) >> 3;
  }
  int32_t texW = t->texW, texH = t->texH, texMask = t->texMask;

  for (int32_t sy = syStart; sy <= syEnd; sy++) {
    bool lower = (sy >= syMid);
    int32_t kA = sy - syV0;
    int32_t kB = lower ? (sy - syV1) : (sy - syV0);
    int32_t xA = x0 + sAx * kA;
    int32_t xB = lower ? (x1 + sCx * kB) : (x0 + sBx * kB);
    bool bIsLeft = (xB < xA);
    int32_t xL = bIsLeft ? xB : xA;
    int32_t xR = bIsLeft ? xA : xB;

    int32_t px0 = (xL + 255) >> 8;
    if (px0 < vpL3D) px0 = vpL3D;
    int32_t px1 = xR >> 8;
    if (px1 >= vpR3D) px1 = vpR3D - 1;
    if (px0 > px1) continue;
    uint16_t* row = fb3D + sy * fbW3D;
    int32_t span = xR - xL;
    int32_t dist = px0 * 256 - xL;
    if (dist < 0) dist = 0;
    if (span > 0 && dist > span) dist = span;
    if (perf3DEnabled) {
      perf3D.spans++;
      perf3D.pixels += (uint32_t)(px1 - px0 + 1);
    }
    if (w3dDrawMask3D) markSpanMask3D(sy, px0, px1);

    switch (mode) {
      case 0: {
        for (int32_t x = px0; x <= px1; x++) row[x] = flat565;
        break;
      }
      case 1: {
        int32_t arA = r0 + sAr * kA, agA = g0 + sAg * kA, abA = b0 + sAb * kA;
        int32_t arB, agB, abB;
        if (lower) { arB = r1 + sCr * kB; agB = g1 + sCg * kB; abB = b1 + sCb * kB; }
        else { arB = r0 + sBr * kB; agB = g0 + sBg * kB; abB = b0 + sBb * kB; }
        int32_t dR = lower ? stepDnR : stepUpR;
        int32_t dG = lower ? stepDnG : stepUpG;
        int32_t dB = lower ? stepDnB : stepUpB;
        int32_t curR = (bIsLeft ? arB : arA) + ((dR * dist) >> 8);
        int32_t curG = (bIsLeft ? agB : agA) + ((dG * dist) >> 8);
        int32_t curB = (bIsLeft ? abB : abA) + ((dB * dist) >> 8);
        for (int32_t x = px0; x <= px1; x++) {
          int32_t r8 = curR >> 8;
          int32_t g8 = curG >> 8;
          int32_t b8 = curB >> 8;
          if (r8 < 0) r8 = 0; else if (r8 > 255) r8 = 255;
          if (g8 < 0) g8 = 0; else if (g8 > 255) g8 = 255;
          if (b8 < 0) b8 = 0; else if (b8 > 255) b8 = 255;
          // framebuffer holds byte-swapped RGB565 words (TFT_eSPI convention)
          row[x] = __builtin_bswap16((uint16_t)(((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3)));
          curR += dR;
          curG += dG;
          curB += dB;
        }
        break;
      }
      case 3: {
        float kaF = (float)kA, kbF = (float)kB;
        float auA = u0 + sAu * kaF, avA = v0 + sAv * kaF, awA = w0 + sAw * kaF;
        float auB, avB, awB;
        if (lower) { auB = u1 + sCu * kbF; avB = v1 + sCv * kbF; awB = w1 + sCw * kbF; }
        else { auB = u0 + sBu * kbF; avB = v0 + sBv * kbF; awB = w0 + sBw * kbF; }
        float curUw = bIsLeft ? auB : auA, curVw = bIsLeft ? avB : avA, curW = bIsLeft ? awB : awA;
        float dUw = lower ? stepDnU : stepUpU;
        float dVw = lower ? stepDnV : stepUpV;
        float dW = lower ? stepDnW : stepUpW;
        float distF = (float)dist * 0.00390625f;
        curUw += dUw * distF; curVw += dVw * distF; curW += dW * distF;
        for (int32_t x = px0; x <= px1; x++) {
          float f = (curW > 1e-12f) ? fastRecip3D(curW) : 0.0f;
          uint32_t smp = sampleTexel3D(texPix, texW, texH, texMask, (int32_t)(curUw * f), (int32_t)(curVw * f));
          uint32_t r5 = (((smp >> 11) & 31) * (uint32_t)lightR5) >> 5;
          uint32_t g6 = (((smp >> 5) & 63) * (uint32_t)lightG6) >> 6;
          uint32_t b5 = ((smp & 31) * (uint32_t)lightB5) >> 5;
          // framebuffer holds byte-swapped RGB565 words (TFT_eSPI convention)
          row[x] = __builtin_bswap16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
          curUw += dUw; curVw += dVw; curW += dW;
        }
        break;
      }
      default: { // mode 2: texture modulated by interpolated Gouraud light
        int32_t arA = r0 + sAr * kA, agA = g0 + sAg * kA, abA = b0 + sAb * kA;
        int32_t arB, agB, abB;
        if (lower) { arB = r1 + sCr * kB; agB = g1 + sCg * kB; abB = b1 + sCb * kB; }
        else { arB = r0 + sBr * kB; agB = g0 + sBg * kB; abB = b0 + sBb * kB; }
        float kaF = (float)kA, kbF = (float)kB;
        float auA = u0 + sAu * kaF, avA = v0 + sAv * kaF, awA = w0 + sAw * kaF;
        float auB, avB, awB;
        if (lower) { auB = u1 + sCu * kbF; avB = v1 + sCv * kbF; awB = w1 + sCw * kbF; }
        else { auB = u0 + sBu * kbF; avB = v0 + sBv * kbF; awB = w0 + sBw * kbF; }
        float curUw = bIsLeft ? auB : auA, curVw = bIsLeft ? avB : avA, curW = bIsLeft ? awB : awA;
        int32_t dR = lower ? stepDnR : stepUpR;
        int32_t dG = lower ? stepDnG : stepUpG;
        int32_t dB = lower ? stepDnB : stepUpB;
        float dUw = lower ? stepDnU : stepUpU;
        float dVw = lower ? stepDnV : stepUpV;
        float dW = lower ? stepDnW : stepUpW;
        float distF = (float)dist * 0.00390625f;
        int32_t curR = (bIsLeft ? arB : arA) + ((dR * dist) >> 8);
        int32_t curG = (bIsLeft ? agB : agA) + ((dG * dist) >> 8);
        int32_t curB = (bIsLeft ? abB : abA) + ((dB * dist) >> 8);
        curUw += dUw * distF; curVw += dVw * distF; curW += dW * distF;
        for (int32_t x = px0; x <= px1; x++) {
          int32_t r8 = curR >> 8;
          int32_t g8 = curG >> 8;
          int32_t b8 = curB >> 8;
          if (r8 < 0) r8 = 0; else if (r8 > 255) r8 = 255;
          if (g8 < 0) g8 = 0; else if (g8 > 255) g8 = 255;
          if (b8 < 0) b8 = 0; else if (b8 > 255) b8 = 255;
          float f = (curW > 1e-12f) ? fastRecip3D(curW) : 0.0f;
          uint32_t smp = sampleTexel3D(texPix, texW, texH, texMask, (int32_t)(curUw * f), (int32_t)(curVw * f));
          uint32_t r5 = (((smp >> 11) & 31) * (uint32_t)(r8 >> 3)) >> 5;
          uint32_t g6 = (((smp >> 5) & 63) * (uint32_t)(g8 >> 2)) >> 6;
          uint32_t b5 = ((smp & 31) * (uint32_t)(b8 >> 3)) >> 5;
          // framebuffer holds byte-swapped RGB565 words (TFT_eSPI convention)
          row[x] = __builtin_bswap16((uint16_t)((r5 << 11) | (g6 << 5) | b5));
          curR += dR;
          curG += dG;
          curB += dB;
          curUw += dUw; curVw += dVw; curW += dW;
        }
        break;
      }
    }
  }
}

static void rasterTri3D(TriDraw3D* t) {
  rasterTriPart3D(t, 0, 1, 2);
  if (t->vertexCount >= 4) rasterTriPart3D(t, 0, 2, 3);
}

void drawWorld3D(DISPLAY_T* display) {
  if (!ensureWorld3D()) return;
  if (!display || !display->created()) {
    Serial.println("engine3d: drawWorld called with an invalid display!");
    return;
  }
  int64_t perfT0 = perf3DEnabled ? esp_timer_get_time() : 0;
  fb3D = (uint16_t*)display->getPointer();
  fbW3D = display->width();
  fbH3D = display->height();
  if (!fb3D || fbW3D < 1 || fbH3D < 1) {
    Serial.println("engine3d: display sprite has no pixel buffer!");
    return;
  }
  resolveViewport3D(fbW3D, fbH3D); // raster clip must match the update's projection
  w3dDrawMask3D = currentDrawMask3D(display);
  barrierWorld3DFill3D(); // rasterizer is the first writer of this frame's buffer
  w3d3DFrameActive3D = true;
  for (int32_t bucket = BUCKET3D_COUNT - 1; bucket >= 0; bucket--) {
    int16_t idx = bucketHeads3D[bucket];
    while (idx >= 0) {
      TriDraw3D* d = &drawList3D[idx];
      int16_t nextIdx = d->next;
      rasterTri3D(d);
      idx = nextIdx;
    }
  }
  if (perf3DEnabled) perf3D.drawUs += (int32_t)(esp_timer_get_time() - perfT0);
}

void initWorld3D() {
  ensureWorld3D();
}
