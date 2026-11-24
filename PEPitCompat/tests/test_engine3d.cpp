/*
 * PEPitCompat — Unit tests: 3D engine (src/hardware/engine3d.cpp)
 *
 * Conventions: view space +x right, +y up, +z forward; camera looks along its
 * local +z; rotations in degrees (pitch about x, positive looks up; yaw about
 * y, positive turns right). Faces use standard counter-clockwise OBJ winding.
 * No z-buffer: painter's algorithm, so pixel assertions only rely on the
 * frontmost face at a sample point.
 *
 * Static engine state persists across test cases — every case starts with
 * resetWorld3D() which also re-creates the default camera at (0,0,-100) with
 * near plane 1 / far 5000 and default 50% grey ambient.
 */

#include "catch_amalgamated.hpp"

#include <cstdint>
#include <filesystem>
#include <string>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "engine3d.hpp"
#include "test_helpers.h"

using pepit_test::makeSdRoot;
using pepit_test::writeSdFile;

namespace {

/*
 * The engine lazily allocates all pools in PSRAM (heap_caps_malloc — malloc
 * under the shim). initWorld3D() is idempotent; resetWorld3D() is safe either
 * way. Test display: a sprite of the panel's exact size, same as the game
 * display the Lua wrappers pass to update/drawWorld3D.
 */
class Display3D {
public:
    Display3D() {
        disp.setColorDepth(16);
        disp.createSprite(320, 240);
    }
    ~Display3D() { disp.deleteSprite(); }
    void render() {
        disp.fillSprite(0);
        updateWorld3D(&disp);
        drawWorld3D(&disp);
    }
    uint16_t pixel(int x, int y) {
        // Sprite buffers hold byteswapped RGB565 — return the native color value
        return __builtin_bswap16(reinterpret_cast<uint16_t*>(disp.getPointer())[y * 320 + x]);
    }
    int litPixels() {
        const uint16_t* fb = reinterpret_cast<const uint16_t*>(disp.getPointer());
        int n = 0;
        for (int i = 0; i < 320 * 240; i++) if (fb[i]) n++;
        return n;
    }
    static void rgb(uint16_t c, int& r, int& g, int& b) {
        r = (c >> 11) * 255 / 31;
        g = ((c >> 5) & 63) * 255 / 63;
        b = (c & 31) * 255 / 31;
    }
private:
    TFT_eSprite disp = TFT_eSprite(&tft);
};

// 24-bit uncompressed BMP, bottom-up rows — same format the device loads.
// loadBmp consumes bytes as B,G,R (see gfxHandler.cpp COMPOSE_RGB565).
std::string makeBmp24(int w, int h, void (*color)(int, int, uint8_t*, uint8_t*, uint8_t*)) {
    uint32_t rowSize = (w * 3 + 3) & ~3u;
    uint32_t imageSize = rowSize * h;
    uint32_t fileSize = 54 + imageSize;
    std::string out(54, 0);
    auto put32 = [&out](size_t off, uint32_t v) {
        for (int i = 0; i < 4; i++) out[off + i] = char((v >> (8 * i)) & 0xFF);
    };
    auto put16 = [&out](size_t off, uint16_t v) {
        out[off] = char(v & 0xFF);
        out[off + 1] = char((v >> 8) & 0xFF);
    };
    out[0] = 'B'; out[1] = 'M';
    put32(2, fileSize);
    put32(10, 54);   // data offset
    put32(14, 40);   // DIB header size
    put32(18, w);
    put32(22, (uint32_t)h);
    put16(26, 1);    // planes
    put16(28, 24);   // bpp
    put32(30, 0);    // BI_RGB
    put32(34, imageSize);
    for (int y = 0; y < h; y++) {  // bottom-up = source row h-1-y first
        for (int x = 0; x < w; x++) {
            uint8_t r, g, b;
            color(x, h - 1 - y, &r, &g, &b);
            out += char(b);
            out += char(g);
            out += char(r);
        }
        out.append(rowSize - w * 3, 0);
    }
    return out;
}

// Bricks are red-dominant, mortar is near-white but every brick keeps a clear
// r > g margin so lighting attenuation cannot reorder the channels.
void brickColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if ((x / 8 + y / 4) % 2) { *r = 240; *g = 100; *b = 60; }
    else { *r = 200; *g = 100; *b = 80; }
}

/*
 * Half-25 cube with UVs and per-face normals. Winding chosen so that
 * cross(p1-p0, p2-p0) points outward (standard CCW OBJ convention).
 */
const char* CUBE_OBJ =
    "mtllib cube.mtl\n"
    "v 25 25 -25\nv -25 25 -25\nv -25 -25 -25\nv 25 -25 -25\n"
    "v 25 25 25\nv -25 25 25\nv -25 -25 25\nv 25 -25 25\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "vn 0 0 -1\nvn -1 0 0\nvn 0 0 1\nvn 1 0 0\nvn 0 1 0\nvn 0 -1 0\n"
    "usemtl bricks\ns off\n"
    "f 1/1/1 4/2/1 3/3/1\nf 1/1/1 3/3/1 2/4/1\n"
    "f 7/1/3 8/2/3 5/3/3\nf 7/1/3 5/3/3 6/4/3\n"
    "f 4/1/4 1/2/4 5/3/4\nf 4/1/4 5/3/4 8/4/4\n"
    "f 2/1/2 3/2/2 7/3/2\nf 2/1/2 7/3/2 6/4/2\n"
    "f 6/1/5 5/2/5 1/3/5\nf 6/1/5 1/3/5 2/4/5\n"
    "f 8/1/6 7/2/6 3/3/6\nf 8/1/6 3/3/6 4/4/6\n";

const std::string CUBE_MTL = "newmtl bricks\nmap_Kd bricks.bmp\n";

// Left half pure red, right half pure green: a correctly mapped face must
// show BOTH clusters; a broken UV path tints the face from one texture column.
void splitColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (x < 32) { *r = 240; *g = 20; *b = 20; }
    else { *r = 20; *g = 240; *b = 20; }
}



std::filesystem::path writeModelFixtures(const std::string& name) {
    std::filesystem::path root = makeSdRoot("engine3d_" + name);
    writeSdFile(root, "models/cube.obj", CUBE_OBJ);
    writeSdFile(root, "models/cube.mtl", CUBE_MTL);
    writeSdFile(root, "models/bricks.bmp", makeBmp24(64, 64, brickColor));
    return root;
}

}  // namespace

TEST_CASE("engine3d: OBJ load auto-textures via mtllib map_Kd and renders textured", "[engine3d]") {
    writeModelFixtures("textured");
    resetWorld3D();

    int16_t mesh = loadMesh3D(String("/models/cube.obj"));
    REQUIRE(mesh >= 0);
    int16_t cube = createEntity3D(mesh);
    REQUIRE(cube >= 0);
    rotateEntity3D(cube, 0, 45, 0);  // front and right face visible

    Display3D d;
    d.render();
    int r, g, b;
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(d.litPixels() > 8000);
    // Bricks pattern is red-dominant (r >= g >= b); flat shading would be grey
    CHECK(r > 60);
    CHECK(r >= g);
    CHECK(g >= b);

    resetWorld3D();
}

TEST_CASE("engine3d: texture UVs span the full texture, not one column", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_uvsweep");
    writeSdFile(root, "models/cube.obj", CUBE_OBJ);
    writeSdFile(root, "models/cube.mtl", "newmtl split\nmap_Kd split.bmp\n");
    writeSdFile(root, "models/split.bmp", makeBmp24(64, 64, splitColor));
    resetWorld3D();

    int16_t mesh = loadMesh3D(String("/models/cube.obj"));
    REQUIRE(mesh >= 0);
    REQUIRE(createEntity3D(mesh) >= 0);

    Display3D d;
    d.render();  // default camera looks at the front face head-on
    int red = 0, green = 0;
    for (int y = 55; y < 185; y++) {          // the 139px cube silhouette
        for (int x = 90; x < 230; x++) {
            int r, g, b;
            Display3D::rgb(d.pixel(x, y), r, g, b);
            if (r > g + 80 && r > b + 80) red++;
            else if (g > r + 80 && g > b + 80) green++;
        }
    }
    CHECK(red > 3000);
    CHECK(green > 3000);

    resetWorld3D();
}

TEST_CASE("engine3d: default 50% ambient shades an untextured cube to mid-grey", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();  // 50-unit cube, -25..25 on all axes
    REQUIRE(mesh >= 0);
    int16_t cube = createEntity3D(mesh);

    Display3D d;
    d.render();
    int r, g, b;
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(d.litPixels() > 10000);
    CHECK(r > 100); CHECK(r < 160);
    CHECK(g > 100); CHECK(b > 100);
    resetWorld3D();
}

TEST_CASE("engine3d: draws into the active buffer of a double-buffered display", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    createEntity3D(mesh);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240, 2);  // frames=2, like the firmware display sprite

    const uint16_t* prevFb = nullptr;
    for (int frame = 0; frame < 4; frame++) {
        disp.fillSprite(0);
        updateWorld3D(&disp);
        drawWorld3D(&disp);
        const uint16_t* fb = (const uint16_t*)disp.getPointer();
        if (frame > 0)
            CHECK(fb != prevFb);  // pushSpriteFast flipped the active buffer
        int lit = 0;
        for (int i = 0; i < 320 * 240; i++)
            if (fb[i]) lit++;
        CHECK(lit > 5000);  // the buffer about to be pushed holds this frame's scene
        prevFb = fb;
        disp.pushSpriteFast(0, 0);
    }
    disp.deleteSprite();
    resetWorld3D();
}

TEST_CASE("engine3d: point light at camera fully lights the front face, range cuts it off", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);

    // range 0 == infinite (no attenuation); front face sits ~head-on to the
    // light at the camera, Gouraud corners pull the flat centre a bit below 255
    int16_t light = createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_POINT);
    REQUIRE(light >= 0);
    Display3D d;
    d.render();
    int r, g, b;
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 180); CHECK(g > 180); CHECK(b > 180);
    freeLight3D(light);

    // Same light pushed 800 units away with range 300: surface outside range
    light = createLight3D(255, 255, 255, 0, 0, -900, 300, LIGHT3D_TYPE_POINT);
    d.render();
    CHECK(d.pixel(160, 120) == 0);
    freeLight3D(light);
    resetWorld3D();
}

TEST_CASE("engine3d: spot light respects cone — dark outside, lit when aimed at face", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);

    int16_t spot = createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_SPOT);
    REQUIRE(spot >= 0);
    setLightCone3D(spot, 30, 45);
    Display3D d;

    setLightDirection3D(spot, 0, 1, 0);  // beam straight up, front face outside cone
    d.render();
    CHECK(d.pixel(160, 120) == 0);

    setLightDirection3D(spot, 0, 0, 1);  // beam at the cube, all vertices inside cone
    d.render();
    int r, g, b;
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 180); CHECK(g > 180); CHECK(b > 180);

    freeLight3D(spot);
    resetWorld3D();
}

TEST_CASE("engine3d: directional beam lights only faces it hits head-on", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t light = createLight3D(255, 255, 255, 0, 0, 0, 0, LIGHT3D_TYPE_DIRECTIONAL);
    REQUIRE(light >= 0);
    setLightDirection3D(light, 0, -1, 0);  // beam downwards

    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);
    positionCamera3D(0, 0, 120, -120);
    rotateCamera3D(0, -45, 0, 0);  // look straight down at the cube

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 2000);
    int r, g, b;
    Display3D::rgb(d.pixel(160, 100), r, g, b);  // top face, seen head-on
    CHECK(r > 200); CHECK(g > 200); CHECK(b > 200);

    freeLight3D(light);
    resetWorld3D();
}

TEST_CASE("engine3d: near-plane clipping keeps geometry in front and drops the rest", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);
    // Camera looks down at the origin cube; its top-face triangles straddle a
    // near plane at 115 (camera-to-center distance is ~116.6).
    positionCamera3D(0, 0, 60, -100);
    rotateCamera3D(0, -30, 0, 0);

    cameraRange3D(0, 115, 5000);
    Display3D d;
    d.render();
    int clipped = d.litPixels();

    cameraRange3D(0, 10, 5000);
    d.render();
    int full = d.litPixels();

    CHECK(clipped > 100);           // clipped fan still rasterizes
    CHECK(clipped < full);          // behind-near geometry disappears
    resetWorld3D();
}

TEST_CASE("engine3d: entity entirely behind the camera is culled", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);
    positionEntity3D(cube, 0, 0, -200);  // default camera sits at z=-100

    Display3D d;
    d.render();
    CHECK(d.litPixels() == 0);
    resetWorld3D();
}

TEST_CASE("engine3d: procedural sphere and plane render with Gouraud shading", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(120, 120, 120);
    int16_t sphereMesh = createMesh3DSphere(40, 16);
    int16_t planeMesh = createMesh3DPlane(600, 600);
    REQUIRE(sphereMesh >= 0);
    REQUIRE(planeMesh >= 0);
    createEntity3D(sphereMesh);
    int16_t plane = createEntity3D(planeMesh);
    positionEntity3D(plane, 0, -60, 0);
    int16_t cam = createCamera3D();
    REQUIRE(cam >= 0);
    positionCamera3D(cam, 0, 40, -150);
    rotateCamera3D(cam, -15, 0, 0);

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 20000);
    CHECK(d.pixel(160, 60) != 0);   // sphere top
    CHECK(d.pixel(100, 120) != 0);  // sphere side
    resetWorld3D();
}

TEST_CASE("engine3d: LinkEntity makes the child follow the parent transform", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    int16_t parent = createEntity3D(mesh);
    int16_t child = createEntity3D(mesh);
    REQUIRE(parent >= 0);
    REQUIRE(child >= 0);
    positionEntity3D(parent, 0, 0, 0);
    positionEntity3D(child, -120, 0, 0);  // unlinked: off screen left
    REQUIRE(linkEntity3D(child, parent));
    rotateCamera3D(0, 0, -45, 0);  // look towards -x so both cubes are in view

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 5000);
    CHECK(d.pixel(160, 120) != 0);  // screen center covered around the parent
    resetWorld3D();
}

TEST_CASE("engine3d: missing model file and full pool handled without crash", "[engine3d]") {
    makeSdRoot("engine3d_empty");
    resetWorld3D();
    CHECK(loadMesh3D(String("/does/not/exist.obj")) == -1);
    CHECK(freeMesh3D(-1) == false);
    CHECK(freeEntity3D(9999) == false);
    CHECK(freeLight3D(-5) == false);

    // Exhaust the mesh pool: further creates must return -1, not overflow
    int16_t handles[MESH3D_LIMIT + 1];
    for (int i = 0; i < MESH3D_LIMIT + 1; i++) handles[i] = createMesh3DCube();
    CHECK(handles[MESH3D_LIMIT] == -1);
    CHECK(handles[MESH3D_LIMIT - 1] >= 0);
    resetWorld3D();
}
