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
#include <cstdlib>  // setenv
#include <filesystem>
#include <string>
#include <vector>

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
    std::vector<uint16_t> snapshot() {
        std::vector<uint16_t> fb;
        fb.reserve(320 * 240);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++) fb.push_back(pixel(x, y));
        return fb;
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

void markerColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (x >= 48 && y < 16) { *r = 240; *g = 20; *b = 20; }
    else { *r = 20; *g = 20; *b = 240; }
}

std::string makeMarkerBmp(int w, int h) {
    int rowSize = (w * 3 + 3) & ~3;
    std::string out;
    out.append("BM");
    auto put32 = [&](uint32_t v) { out.append((const char*)&v, 4); };
    auto put16 = [&](uint16_t v) { out.append((const char*)&v, 2); };
    put32(54 + rowSize * h);
    put32(0);
    put32(54);
    put32(40);
    put32(w);
    put32(h);
    put16(1);
    put16(24);
    put32(0);
    put32(rowSize * h);
    put32(2835);
    put32(2835);
    put32(0);
    put32(0);
    uint8_t r, g, b;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            markerColor(x, h - 1 - y, &r, &g, &b);
            out += char(b); out += char(g); out += char(r);
        }
        out.append(rowSize - w * 3, 0);
    }
    return out;
}

const char* UVFIXED_OBJ =
    "mtllib cube.mtl\n"
    "v 25 25 -25\nv -25 25 -25\nv -25 -25 -25\nv 25 -25 -25\n"
    "v 25 25 25\nv -25 25 25\nv -25 -25 25\nv 25 -25 25\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "vn 0 0 -1\nvn -1 0 0\nvn 0 0 1\nvn 1 0 0\nvn 0 1 0\nvn 0 -1 0\n"
    "usemtl marker\ns off\n"
    "f 1/3/1 4/2/1 3/1/1\nf 1/3/1 3/1/1 2/4/1\n"
    "f 7/2/3 8/1/3 5/4/3\nf 7/2/3 5/4/3 6/3/3\n"
    "f 4/1/4 1/4/4 5/3/4\nf 4/1/4 5/3/4 8/2/4\n"
    "f 2/3/2 3/2/2 7/1/2\nf 2/3/2 7/1/2 6/4/2\n"
    "f 6/4/5 5/3/5 1/2/5\nf 6/4/5 1/2/5 2/1/5\n"
    "f 8/1/6 7/2/6 3/3/6\nf 8/1/6 3/3/6 4/4/6\n";

void runView(const char* label, float yaw) {
    resetWorld3D();
    int16_t mesh = loadMesh3D(String("/models/cube.obj"));
    REQUIRE(mesh >= 0);
    int16_t cube = createEntity3D(mesh);
    rotateEntity3D(cube, 0, yaw, 0);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    const uint16_t* fb = (const uint16_t*)disp.getPointer();

    long sx = 0, sy = 0; int n = 0, nblue = 0;
    long lx0 = 9999, lx1 = -1, ly0 = 9999, ly1 = -1;
    for (int y = 0; y < 240; y++) {
        for (int x = 0; x < 320; x++) {
            uint16_t p = __builtin_bswap16(fb[y * 320 + x]);
            if (!p) continue;
            int r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
            if (x < lx0) lx0 = x; if (x > lx1) lx1 = x;
            if (y < ly0) ly0 = y; if (y > ly1) ly1 = y;
            if (r >= 10 && g <= 7 && b <= 7) { sx += x; sy += y; n++; }
            else if (b >= 15 && b > r + 4) nblue++;
        }
    }
    if (n == 0) { printf("%s: no marker pixels (blue=%d)\n", label, nblue); }
    else {
        double cx = (double)sx / n, cy = (double)sy / n;
        double mx = (lx0 + lx1) / 2.0, my = (ly0 + ly1) / 2.0;
        printf("%s: facebox x[%ld..%ld] y[%ld..%ld] marker n=%d pos=(%.0f,%.0f) -> %s-%s\n",
               label, lx0, lx1, ly0, ly1, n, cx, cy,
               cx < mx ? "LEFT" : "RIGHT", cy < my ? "TOP" : "BOTTOM");
    }
    fflush(stdout);
    disp.deleteSprite();
}

void stripeColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if ((x / 8) & 1) { *r = 255; *g = 255; *b = 255; }
    else { *r = 30; *g = 30; *b = 180; }
}

void redFill(int, int, uint8_t* r, uint8_t* g, uint8_t* b) { *r = 235; *g = 40; *b = 40; }
void flameFill(int, int, uint8_t* r, uint8_t* g, uint8_t* b) { *r = 220; *g = 120; *b = 40; }
void greenFill(int, int, uint8_t* r, uint8_t* g, uint8_t* b) { *r = 40; *g = 220; *b = 60; }

/*
 * Single planar quad tilted so depth varies from 55 (left) to 95 (right):
 * perspective-correct mapping compresses stripe density toward the far side,
 * screen-affine mapping keeps every stripe the same width.
 */
const char* TILTQUAD_OBJ =
    "mtllib quad.mtl\n"
    "v -25 25 -45\nv 25 25 -5\nv 25 -25 -5\nv -25 -25 -45\n"
    "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
    "usemtl stripes\n"
    "f 1/4 2/3 3/2\nf 1/4 3/2 4/1\n";

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

TEST_CASE("engine3d: per-face UV winding keeps texture orientation consistent", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_chirality");
    writeSdFile(root, "models/cube.obj", UVFIXED_OBJ);
    writeSdFile(root, "models/cube.mtl", "newmtl marker\nmap_Kd marker.bmp\n");
    writeSdFile(root, "models/marker.bmp", makeMarkerBmp(64, 64));

    struct View { const char* name; float yaw; };
    for (View v : {View{"front", 0.0f}, View{"yaw+90", 90.0f}, View{"yaw-90", -90.0f}}) {
        resetWorld3D();
        int16_t mesh = loadMesh3D(String("/models/cube.obj"));
        REQUIRE(mesh >= 0);
        int16_t cube = createEntity3D(mesh);
        rotateEntity3D(cube, 0, v.yaw, 0);

        Display3D d;
        d.render();
        long sx = 0, sy = 0;
        int n = 0;
        for (int y = 51; y <= 189; y++) {          // cube silhouette rows
            for (int x = 91; x <= 229; x++) {
                int r, g, b;
                Display3D::rgb(d.pixel(x, y), r, g, b);
                if (r > 120 && g < 60 && b < 60) { sx += x; sy += y; n++; }
            }
        }
        INFO(v.name);
        CHECK(n > 300);                            // marker present on the visible face
        CHECK(sx > n * 160);                       // right half of the face
        CHECK(sy < n * 120);                       // upper half of the face
    }
    resetWorld3D();
}

TEST_CASE("engine3d: created camera replaces the implicit default", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    REQUIRE(mesh >= 0);
    int16_t ent = createEntity3D(mesh);
    REQUIRE(ent >= 0);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    const uint16_t* fb = (const uint16_t*)disp.getPointer();
    int defaultLit = 0;
    for (int i = 0; i < 320 * 240; i++) if (fb[i]) defaultLit++;
    CHECK(defaultLit > 1000);  // implicit camera at (0,0,-100) frames the cube

    int16_t cam = createCamera3D();
    REQUIRE(cam >= 0);
    positionCamera3D(cam, 0, 0, -25);  // inside the cube's near field
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    int createdLit = 0;
    for (int i = 0; i < 320 * 240; i++) if (fb[i]) createdLit++;
    INFO("default " << defaultLit << " vs created-camera " << createdLit);
    CHECK(createdLit != defaultLit);  // the new camera must be the active one

    disp.deleteSprite();
    resetWorld3D();
}

TEST_CASE("engine3d: procedural racecar model loads fully textured", "[engine3d]") {
    // The 3ddemo racecar model (WIPSDCardContent/tools/gen_racecar.py): body plus
    // two wheel meshes, every triangle of each OBJ must load, the mtl atlas must
    // auto-attach, and the assembled car must render from opposite sides.
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path models = repoRoot / "WIPSDCardContent" / "games" / "3ddemo" / "models";
    if (!std::filesystem::exists(models / "racecar.obj"))
        SKIP("3ddemo game fixtures live in WIPSDCardContent, which is not committed");
    auto read = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        REQUIRE(in);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    auto countTris = [](const std::string& obj) {
        uint32_t n = 0;
        for (size_t pos = 0; (pos = obj.find("\nf ", pos)) != std::string::npos; pos++)
            n++;
        return n;
    };
    std::string body = read(models / "racecar.obj");
    std::string wf = read(models / "wheel_front.obj");
    std::string wr = read(models / "wheel_rear.obj");
    uint32_t bodyTris = countTris(body), wfTris = countTris(wf), wrTris = countTris(wr);
    REQUIRE(bodyTris >= 700);
    REQUIRE(wfTris >= 200);
    REQUIRE(wfTris + wrTris + bodyTris >= 1300);

    std::filesystem::path root = makeSdRoot("engine3d_racecar");
    writeSdFile(root, "models/racecar.obj", body);
    writeSdFile(root, "models/racecar.mtl", read(models / "racecar.mtl"));
    writeSdFile(root, "models/racecar.bmp", read(models / "racecar.bmp"));
    writeSdFile(root, "models/wheel_front.obj", wf);
    writeSdFile(root, "models/wheel_front.mtl", read(models / "wheel_front.mtl"));
    writeSdFile(root, "models/wheel_rear.obj", wr);
    writeSdFile(root, "models/wheel_rear.mtl", read(models / "wheel_rear.mtl"));
    resetWorld3D();

    int16_t mesh = loadMesh3D(String("/models/racecar.obj"));
    int16_t wfMesh = loadMesh3D(String("/models/wheel_front.obj"));
    int16_t wrMesh = loadMesh3D(String("/models/wheel_rear.obj"));
    REQUIRE(mesh >= 0);
    REQUIRE(wfMesh >= 0);
    REQUIRE(wrMesh >= 0);
    uint16_t verts = 0, faces = 0;
    REQUIRE(getMeshStats3D(mesh, &verts, &faces));
    CHECK(faces == bodyTris);
    REQUIRE(getMeshStats3D(wfMesh, &verts, &faces));
    CHECK(faces == wfTris);
    REQUIRE(getMeshStats3D(wrMesh, &verts, &faces));
    CHECK(faces == wrTris);

    int16_t ent = createEntity3D(mesh);
    REQUIRE(ent >= 0);
    positionEntity3D(ent, 0, -4, 0);
    for (auto wp : {std::make_pair(wfMesh, 15.12f), std::make_pair(wrMesh, -14.16f)}) {
        for (float sx : {1.0f, -1.0f}) {
            int16_t w = createEntity3D(wp.first);
            REQUIRE(w >= 0);
            REQUIRE(linkEntity3D(w, ent));
            positionEntity3D(w, sx * 9.0f, 4.8f, wp.second);
        }
    }
    positionCamera3D(0, 0, 12, -46);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);
    for (int yaw : {90, -90}) {
        rotateEntity3D(ent, 0, (float)yaw, 0);
        disp.fillSprite(0);
        updateWorld3D(&disp);
        drawWorld3D(&disp);
        const uint16_t* fb = (const uint16_t*)disp.getPointer();
        int lit = 0;
        for (int i = 0; i < 320 * 240; i++) if (fb[i]) lit++;
        INFO("yaw " << yaw << ": lit pixels " << lit);
        CHECK(lit > 4000);
    }
    disp.deleteSprite();
    resetWorld3D();
}

TEST_CASE("engine3d: wheel entities roll and steer when rotated", "[engine3d]") {
    // A wheel mesh is symmetric except for its spoke/tread texture, so a
    // rolling or steered wheel must visibly change the rendered frame.
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path models = repoRoot / "WIPSDCardContent" / "games" / "3ddemo" / "models";
    if (!std::filesystem::exists(models / "racecar.obj"))
        SKIP("3ddemo game fixtures live in WIPSDCardContent, which is not committed");
    auto read = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        REQUIRE(in);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    std::filesystem::path root = makeSdRoot("engine3d_wheels");
    writeSdFile(root, "models/wheel_front.obj", read(models / "wheel_front.obj"));
    writeSdFile(root, "models/wheel_front.mtl", read(models / "wheel_front.mtl"));
    writeSdFile(root, "models/racecar.bmp", read(models / "racecar.bmp"));
    resetWorld3D();

    int16_t wf = loadMesh3D(String("/models/wheel_front.obj"));
    REQUIRE(wf >= 0);
    positionCamera3D(0, 0, 0, -35);
    cameraRange3D(0, 1.0f, 4000.0f);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);

    int16_t w0 = createEntity3D(wf);
    REQUIRE(w0 >= 0);
    rotateEntity3D(w0, 0, 90, 0);  // yaw so the hub disc faces the camera
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    std::vector<uint16_t> base((const uint16_t*)disp.getPointer(),
                               (const uint16_t*)disp.getPointer() + 320 * 240);
    int lit = 0;
    for (auto p : base) if (p) lit++;
    CHECK(lit > 1500);

    rotateEntity3D(w0, 40, 90, 0);  // roll 40 degrees about the axle
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    const uint16_t* fb = (const uint16_t*)disp.getPointer();
    int diff = 0;
    for (int i = 0; i < 320 * 240; i++) if (fb[i] != base[i]) diff++;
    INFO("roll diff " << diff);
    CHECK(diff > 500);
    std::vector<uint16_t> rolled(fb, fb + 320 * 240);

    rotateEntity3D(w0, 40, 115, 0);  // same roll, add 25 degrees of steering
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    fb = (const uint16_t*)disp.getPointer();
    diff = 0;
    for (int i = 0; i < 320 * 240; i++) if (fb[i] != rolled[i]) diff++;
    INFO("steer diff " << diff);
    CHECK(diff > 500);

    disp.deleteSprite();
    resetWorld3D();
}

TEST_CASE("engine3d: entities sharing a mesh render as independent instances", "[engine3d]") {
    // Regression: the 3ddemo's four wheels reuse two wheel meshes. If the
    // transform phase writes shared scratch per mesh instead of per instance,
    // the last-transformed instance clobbers the others and the earlier
    // instances render on top of it (the missing-left-wheels bug). Two
    // entities sharing one mesh must produce the exact frame that two
    // entities on two identical meshes produce.
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path models = repoRoot / "WIPSDCardContent" / "games" / "3ddemo" / "models";
    if (!std::filesystem::exists(models / "racecar.obj"))
        SKIP("3ddemo game fixtures live in WIPSDCardContent, which is not committed");
    auto read = [](const std::filesystem::path& f) {
        std::ifstream in(f, std::ios::binary);
        REQUIRE(in);
        return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    };
    std::filesystem::path root = makeSdRoot("engine3d_shared_mesh");
    writeSdFile(root, "models/wheel_front.obj", read(models / "wheel_front.obj"));
    writeSdFile(root, "models/wheel_front.mtl", read(models / "wheel_front.mtl"));
    writeSdFile(root, "models/racecar.bmp", read(models / "racecar.bmp"));

    auto frameWithWheels = [&](bool shared) {
        resetWorld3D();
        int16_t wl = -1, wr = -1;
        int16_t m1 = loadMesh3D(String("/models/wheel_front.obj"));
        REQUIRE(m1 >= 0);
        positionCamera3D(0, 0, 0, -60);
        cameraRange3D(0, 1.0f, 4000.0f);
        if (shared) {
            wl = createEntity3D(m1);
            wr = createEntity3D(m1);
        } else {
            int16_t m2 = loadMesh3D(String("/models/wheel_front.obj"));
            REQUIRE(m2 >= 0);
            wl = createEntity3D(m1);
            wr = createEntity3D(m2);
        }
        REQUIRE(wl >= 0); REQUIRE(wr >= 0);
        rotateEntity3D(wl, 0, 90, 0);
        rotateEntity3D(wr, 0, 90, 0);
        positionEntity3D(wl, -14, 0, 0);
        positionEntity3D(wr, 14, 0, 0);
        Display3D d;
        d.render();
        std::vector<uint16_t> fb;
        fb.reserve(320 * 240);
        for (int y = 0; y < 240; y++)
            for (int x = 0; x < 320; x++) fb.push_back(d.pixel(x, y));
        return fb;
    };

    std::vector<uint16_t> sharedA = frameWithWheels(true);
    std::vector<uint16_t> sharedB = frameWithWheels(true);   // threading must not vary the frame
    std::vector<uint16_t> exclusive = frameWithWheels(false);

    int lit = 0, leftHalf = 0, rightHalf = 0;
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            if (sharedA[y * 320 + x]) {
                lit++;
                if (x < 160) leftHalf++; else rightHalf++;
            }
        }
    CHECK(lit > 1400);
    CHECK(leftHalf > 300);          // the left instance really is on the left
    CHECK(rightHalf > 300);         // and the right one on the right
    CHECK(sharedA == sharedB);
    CHECK(sharedA == exclusive);

    resetWorld3D();
}

TEST_CASE("engine3d: ngons and meshes over the historic caps load completely", "[engine3d]") {
    // Regression for two silent-truncation bugs: face records with more than
    // 16 corners were fan-triangulated from a fixed array, and meshes above
    // the old 8192-vertex / 4096-face caps were cut mid-mesh. A synthetic
    // model covers both without shipping a third-party asset.
    std::string obj = "o bigmodel\n";
    for (int i = 0; i < 2100; i++) {
        float x = i;
        obj += "v " + std::to_string(x) + " 0 0\n";
        obj += "v " + std::to_string(x) + " 1 0\n";
        obj += "v " + std::to_string(x + 1) + " 1 0.5\n";
        obj += "v " + std::to_string(x + 1) + " 0 0.5\n";
    }
    int base = 1;
    for (int i = 0; i < 2100; i++, base += 4) {
        obj += "f " + std::to_string(base) + " " + std::to_string(base + 1) + " "
             + std::to_string(base + 2) + " " + std::to_string(base + 3) + "\n";
    }
    // one 40-corner ngon, fan-triangulates to 38 faces
    int ngonBase = base;
    for (int k = 0; k < 40; k++) {
        double a = 2.0 * M_PI * k / 40.0;
        obj += "v " + std::to_string(3000 + 10 * cos(a)) + " 5 "
             + std::to_string(5 * sin(a)) + "\n";
    }
    obj += "f";
    for (int k = 0; k < 40; k++) obj += " " + std::to_string(ngonBase + k);
    obj += "\n";

    uint32_t expectedFaces = 2100u * 2 + 38u;
    uint32_t expectedVerts = 2100u * 4 + 40u;
    REQUIRE(expectedFaces > 4096);      // above the old face cap
    REQUIRE(expectedVerts > 8192);      // above the old vertex cap

    std::filesystem::path root = makeSdRoot("engine3d_bigmesh");
    writeSdFile(root, "models/big.obj", obj);
    resetWorld3D();

    int16_t mesh = loadMesh3D(String("/models/big.obj"));
    REQUIRE(mesh >= 0);
    uint16_t verts = 0, faces = 0;
    REQUIRE(getMeshStats3D(mesh, &verts, &faces));
    INFO("loaded " << verts << " verts, " << faces << " faces; expected "
         << expectedVerts << "/" << expectedFaces);
    CHECK(faces == expectedFaces);
    CHECK(verts == expectedVerts);

    resetWorld3D();
}

TEST_CASE("engine3d: texture interpolation is perspective-correct", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_persp");
    writeSdFile(root, "models/quad.obj", TILTQUAD_OBJ);
    writeSdFile(root, "models/quad.mtl", "newmtl stripes\nmap_Kd stripes.bmp\n");
    writeSdFile(root, "models/stripes.bmp", makeBmp24(64, 64, stripeColor));
    resetWorld3D();

    int16_t mesh = loadMesh3D(String("/models/quad.obj"));
    REQUIRE(mesh >= 0);
    REQUIRE(createEntity3D(mesh) >= 0);

    Display3D d;
    d.render();  // default camera at (0,0,-100): near edge at x=-25, far at x=25

    const int ROW = 120;
    int faceL = -1, faceR = -1;
    std::vector<int> boundaries;
    int prevClass = -1;
    for (int x = 0; x < 320; x++) {
        int r, g, b;
        Display3D::rgb(d.pixel(x, ROW), r, g, b);
        int cls = -1;
        if (r > 60 && g > 60 && b > 60) cls = 1;              // white stripe
        else if (b > 60 && b > r + 20 && b > g + 20) cls = 0; // dark stripe
        if (cls < 0) continue;
        if (faceL < 0) faceL = x;
        faceR = x;
        if (prevClass >= 0 && cls != prevClass) boundaries.push_back(x);
        prevClass = cls;
    }
    REQUIRE(faceR - faceL > 80);
    REQUIRE(boundaries.size() >= 6);

    int mid = (faceL + faceR) / 2;
    long nearSpan = 0, farSpan = 0;
    int nearCount = 0, farCount = 0;
    std::vector<int> cuts = boundaries;
    cuts.insert(cuts.begin(), faceL);
    cuts.push_back(faceR);
    for (size_t i = 1; i < cuts.size(); i++) {
        int span = cuts[i] - cuts[i - 1];
        int spanMid = (cuts[i] + cuts[i - 1]) / 2;
        if (spanMid < mid) { nearSpan += span; nearCount++; }
        else if (spanMid > mid) { farSpan += span; farCount++; }
    }
    REQUIRE(nearCount >= 2);
    REQUIRE(farCount >= 2);
    double nearAvg = (double)nearSpan / nearCount;
    double farAvg = (double)farSpan / farCount;
    INFO("near stripe width " << nearAvg << ", far " << farAvg);
    CHECK(nearAvg > farAvg * 1.25);   // affine mapping would give a ratio of 1

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

TEST_CASE("engine3d: progress bar overdraw beyond 100% survives the gap fill", "[engine3d]") {
    // The compat shim refuses the engine3d helper tasks so drawing stays
    // single-threaded; this case asserts the pipelined gap fill, so it opts in.
    setenv("PEPIT_COMPAT_ALLOW_3D_TASKS", "1", 1);
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    createEntity3D(mesh);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240, 2);
    disp.frameBuffer(1);

    // mask mode only arms after the engine rasterized a 3D frame on the display
    beginWorld3DFill3D(&disp);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    blitWorld3DFrame3D(&disp);

    beginWorld3DFill3D(&disp);
    REQUIRE(world3DMaskMode3D(&disp));

    // percent > 100 (overshoot): the fill must paint past the bar's frame box
    drawProgressBar(&disp, 150, 10, 1, 190, 180, 10);

    blitWorld3DFrame3D(&disp);  // core 0 gap-fills the drawn buffer, then pushes it
    quiesceWorld3DBlit3D();

    int8_t drawnId = disp.frameBufferId() == 1 ? 2 : 1;
    const uint16_t* pushed = (const uint16_t*)disp.frameBuffer(drawnId);
    int overshoot = 0;
    for (int x = 190; x < 260; x++)
        if (pushed[195 * 320 + x]) overshoot++;
    CHECK(overshoot > 60);  // unclaimed pixels would have been blacked by the gap fill
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

TEST_CASE("engine3d: setLightColor3D/setLightRange3D mutate a live light", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);

    int16_t light = createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_POINT);
    REQUIRE(light >= 0);
    Display3D d;
    int r, g, b;
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 180);  // baseline: full-brightness white

    setLightColor3D(light, 60, 60, 60);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 20); CHECK(r < 120);  // same surface, clearly dimmer

    setLightRange3D(light, 30);  // nearest cube vertex is 75 units away
    d.render();
    CHECK(d.pixel(160, 120) == 0);  // outside the new range

    setLightRange3D(light, 0);  // 0 restores infinite range
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 20); CHECK(r < 120);  // lit again at the dimmed color

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

TEST_CASE("engine3d: emissive entity keeps its own brightness in an unlit world", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    int16_t cube = createEntity3D(mesh);
    Display3D d;
    int r, g, b;

    d.render();
    CHECK(d.pixel(160, 120) == 0);  // no ambient, no light, no emission

    setMeshEmissive3D(mesh, 255, 0, 0);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 240); CHECK(g < 10); CHECK(b < 10);

    // Half emission halves the surface brightness; the entity override wins
    // over the mesh default and -1 hands control back to the mesh.
    entityEmissive3D(cube, 100, 0, 0);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 90); CHECK(r < 115);
    entityEmissive3D(cube, -1, -1, -1);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 240);

    // Emission is a floor, not a ceiling: light landing on it still adds
    int16_t light = createLight3D(255, 255, 255, 0, 0, -100, 200, LIGHT3D_TYPE_POINT);
    REQUIRE(light >= 0);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 160);
    freeLight3D(light);
    resetWorld3D();
}

TEST_CASE("engine3d: emissive textures are read at their own brightness", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_emissive_tex");
    writeSdFile(root, "models/cube.obj", CUBE_OBJ);
    writeSdFile(root, "models/cube.mtl", "newmtl bricks\nmap_Kd flame.bmp\n");
    writeSdFile(root, "models/flame.bmp", makeBmp24(64, 64, flameFill));

    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = loadMesh3D(String("/models/cube.obj"));
    REQUIRE(mesh >= 0);
    int16_t flame = createEntity3D(mesh);
    Display3D d;
    int r, g, b;

    d.render();
    CHECK(d.litPixels() == 0);

    setMeshEmissive3D(mesh, 255, 255, 255);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 200); CHECK(g > 100); CHECK(b > 25);

    setMeshEmissive3D(mesh, 128, 128, 128);
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 95); CHECK(r < 125);
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
    int16_t sphere = createEntity3D(sphereMesh);
    entityColor3D(sphere, 200, 60, 60);
    int16_t plane = createEntity3D(planeMesh);
    positionEntity3D(plane, 0, -60, 0);
    int16_t cam = createCamera3D();
    REQUIRE(cam >= 0);
    positionCamera3D(cam, 0, 40, -150);
    rotateCamera3D(cam, -15, 0, 0);

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 20000);
    int spherePixels = 0;
    for (int y = 0; y < 240; y++) {
        int r, g, b;
        Display3D::rgb(d.pixel(160, y), r, g, b);
        if (r > 60 && r > g + 30 && r > b + 30) spherePixels++;
    }
    CHECK(spherePixels > 60);  // the created camera must frame the colored sphere
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

TEST_CASE("engine3d: handles are globally unique and checked against their kind", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_handle_kinds");
    writeSdFile(root, "models/tex.bmp", makeBmp24(8, 8, brickColor));
    resetWorld3D();

    int16_t mesh = createMesh3DCube();
    int16_t tex = loadTexture3D(String("/models/tex.bmp"));
    int16_t ent = createEntity3D(mesh);
    int16_t cam = createCamera3D();
    int16_t light = createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_POINT);
    REQUIRE(mesh >= 0); REQUIRE(tex >= 0); REQUIRE(ent >= 0);
    REQUIRE(cam >= 0); REQUIRE(light >= 0);

    // Cameras and lights are entities: they share the entity handle space and
    // every handle stays distinct. Mesh and texture handles live in disjoint
    // ranges above it.
    CHECK(ent < ENTITY3D_LIMIT);
    CHECK(cam < ENTITY3D_LIMIT);
    CHECK(light < ENTITY3D_LIMIT);
    CHECK(ent != cam); CHECK(ent != light); CHECK(cam != light);
    CHECK(mesh >= MESH3D_HANDLE_BASE);
    CHECK(mesh < MESH3D_HANDLE_BASE + MESH3D_LIMIT);
    CHECK(tex >= TEXTURE3D_HANDLE_BASE);
    CHECK(tex < TEXTURE3D_HANDLE_BASE + TEXTURE3D_LIMIT);

    // Wrong-kind handles are rejected rather than aliasing another pool
    CHECK(createEntity3D(ent) == -1);   // an entity handle is not a mesh handle
    CHECK(!freeEntity3D(mesh));         // a mesh handle is not an entity handle
    CHECK(!freeCamera3D(ent));          // a mesh entity is not a camera
    CHECK(!freeLight3D(cam));           // a camera entity is not a light
    CHECK(!freeTexture3D(ent));
    CHECK(!linkEntity3D(ent, mesh));

    hideEntity3D(mesh, true);           // rejected: the cube entity stays visible
    Display3D d;
    d.render();
    CHECK(d.litPixels() > 5000);

    // Cameras and lights draw from the entity pool but are capped by their
    // component limits. One camera and one light are already live.
    for (int i = 1; i < CAMERA3D_LIMIT; i++) REQUIRE(createCamera3D() >= 0);
    CHECK(createCamera3D() == -1);
    for (int i = 1; i < LIGHT3D_LIMIT; i++)
        REQUIRE(createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_POINT) >= 0);
    CHECK(createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_POINT) == -1);
    resetWorld3D();
}

TEST_CASE("engine3d: MoveEntity translates along the entity's own axes", "[engine3d]") {
    resetWorld3D();  // ambient-only lighting: cube shading is orientation-free
    int16_t mesh = createMesh3DCube();
    Display3D d;

    int16_t e = createEntity3D(mesh);
    positionEntity3D(e, 30, 0, 0);
    d.render();
    std::vector<uint16_t> ref = d.snapshot();
    CHECK(d.litPixels() > 5000);
    freeEntity3D(e);

    e = createEntity3D(mesh);            // identity rotation: local axes are world axes
    moveEntity3D(e, 30, 0, 0);
    d.render();
    CHECK(d.snapshot() == ref);
    freeEntity3D(e);

    e = createEntity3D(mesh);
    rotateEntity3D(e, 0, 90, 0);         // yaw 90 puts local +z at world +x
    moveEntity3D(e, 0, 0, 30);
    d.render();
    CHECK(d.snapshot() == ref);
    freeEntity3D(e);

    e = createEntity3D(mesh);
    rotateEntity3D(e, 90, 0, 0);         // pitch 90 puts local +z at world +y
    moveEntity3D(e, 0, 0, 30);
    d.render();
    std::vector<uint16_t> up = d.snapshot();
    freeEntity3D(e);

    e = createEntity3D(mesh);
    positionEntity3D(e, 0, 30, 0);
    d.render();
    CHECK(d.snapshot() == up);

    e = createEntity3D(mesh);            // identity rotation: +z is depth, not screen shift
    moveEntity3D(e, 0, 0, 30);
    d.render();
    CHECK(d.snapshot() != ref);          // the cube moved towards the camera instead
    freeEntity3D(e);
    resetWorld3D();
}

TEST_CASE("engine3d: CreatePivot rigs — children follow parent rotation and moves", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    Display3D d;

    int16_t pivot = createPivot3D();
    REQUIRE(pivot >= 0);
    rotateEntity3D(pivot, 0, 90, 0);     // pivot's local +z now points at world +x
    int16_t child = createEntity3D(mesh);
    REQUIRE(child >= 0);
    REQUIRE(linkEntity3D(child, pivot));
    positionEntity3D(child, 0, 0, 20);   // orbits to world (20,0,0)
    d.render();
    std::vector<uint16_t> rig = d.snapshot();
    CHECK(d.litPixels() > 5000);
    freeEntity3D(child);
    freeEntity3D(pivot);

    int16_t base = createEntity3D(mesh); // baseline: one cube at world (20,0,0)
    rotateEntity3D(base, 0, 90, 0);
    positionEntity3D(base, 20, 0, 0);
    d.render();
    CHECK(d.snapshot() == rig);
    freeEntity3D(base);

    pivot = createPivot3D();
    rotateEntity3D(pivot, 0, 90, 0);
    moveEntity3D(pivot, 0, 0, 15);       // the whole rig slides along the pivot's +z
    child = createEntity3D(mesh);
    REQUIRE(linkEntity3D(child, pivot));
    positionEntity3D(child, 0, 0, 20);   // now orbits to world (35,0,0)
    d.render();
    rig = d.snapshot();
    freeEntity3D(child);
    freeEntity3D(pivot);

    base = createEntity3D(mesh);
    rotateEntity3D(base, 0, 90, 0);
    positionEntity3D(base, 35, 0, 0);
    d.render();
    CHECK(d.snapshot() == rig);
    freeEntity3D(base);
    resetWorld3D();
}

TEST_CASE("engine3d: camera is an entity — MoveEntity dollies, a pivot orbits it, hiding disables it", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    createEntity3D(mesh);
    Display3D d;

    int16_t cam = createCamera3D();
    REQUIRE(cam >= 0);
    positionCamera3D(cam, 0, 0, -100);
    d.render();
    int far = d.litPixels();
    CHECK(far > 10000);

    moveEntity3D(cam, 0, 0, 40);         // dollies along its own look axis
    d.render();
    CHECK(d.litPixels() > far * 2);

    resetWorld3D();
    mesh = createMesh3DCube();
    createEntity3D(mesh);
    cam = createCamera3D();
    positionCamera3D(cam, 0, 0, -100);
    d.render();
    std::vector<uint16_t> straight = d.snapshot();
    int16_t pivot = createPivot3D();
    REQUIRE(linkEntity3D(cam, pivot));
    rotateEntity3D(pivot, 0, 90, 0);     // swings the camera to (-100,0,0), looking at the cube
    d.render();
    CHECK(d.snapshot() == straight);     // yaw-symmetric cube renders the same frame

    hideEntity3D(cam, true);             // a hidden camera is not the active camera
    int16_t cam2 = createCamera3D();     // tiny far view only wins if cam stays out of the pick
    REQUIRE(cam2 >= 0);
    positionCamera3D(cam2, 0, 0, -600);  // same look axis, six times further: small silhouette
    d.render();
    CHECK(d.litPixels() < 3000);
    resetWorld3D();
}

TEST_CASE("engine3d: light is an entity — RotateEntity/TurnEntity aim the beam", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    createEntity3D(mesh);
    int16_t spot = createLight3D(255, 255, 255, 0, 0, -100, 0, LIGHT3D_TYPE_SPOT);
    REQUIRE(spot >= 0);
    setLightCone3D(spot, 30, 45);

    Display3D d;
    int r, g, b;
    d.render();                          // unrotated: beam runs along local +z, at the face
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 180); CHECK(g > 180); CHECK(b > 180);

    rotateEntity3D(spot, 90, 0, 0);     // pitch 90 aims the beam straight up
    d.render();
    CHECK(d.pixel(160, 120) == 0);

    turnEntity3D(spot, -90, 0, 0);      // back onto the face
    d.render();
    Display3D::rgb(d.pixel(160, 120), r, g, b);
    CHECK(r > 180); CHECK(g > 180); CHECK(b > 180);
    resetWorld3D();
}

TEST_CASE("engine3d: a light parented to a pivot travels with it", "[engine3d]") {
    resetWorld3D();
    ambientLight3D(0, 0, 0);
    int16_t mesh = createMesh3DCube();
    createEntity3D(mesh);
    Display3D d;

    int16_t direct = createLight3D(255, 255, 255, 0, 0, -100, 500, LIGHT3D_TYPE_POINT);
    REQUIRE(direct >= 0);
    d.render();
    CHECK(d.pixel(160, 120) != 0);      // face sits well inside the 500-unit range
    std::vector<uint16_t> ref = d.snapshot();
    freeLight3D(direct);

    int16_t pivot = createPivot3D();
    positionEntity3D(pivot, 0, 0, -100);
    int16_t light = createLight3D(255, 255, 255, 0, 0, 0, 500, LIGHT3D_TYPE_POINT);
    REQUIRE(light >= 0);
    REQUIRE(linkEntity3D(light, pivot));  // same world position through the rig
    d.render();
    CHECK(d.snapshot() == ref);

    moveEntity3D(pivot, 0, 0, 900);      // the light flies to z=+800, out of range
    d.render();
    CHECK(d.pixel(160, 120) == 0);
    resetWorld3D();
}

TEST_CASE("engine3d: entities sharing a mesh can carry different textures", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_entity_textures");
    writeSdFile(root, "models/red.bmp", makeBmp24(16, 16, redFill));
    writeSdFile(root, "models/green.bmp", makeBmp24(16, 16, greenFill));
    resetWorld3D();

    int16_t mesh = createMesh3DCube();   // one shared mesh object, no texture of its own
    int16_t red = loadTexture3D(String("/models/red.bmp"));
    int16_t green = loadTexture3D(String("/models/green.bmp"));
    REQUIRE(mesh >= 0); REQUIRE(red >= 0); REQUIRE(green >= 0);

    int16_t left = createEntity3D(mesh);
    int16_t right = createEntity3D(mesh);
    REQUIRE(left >= 0); REQUIRE(right >= 0);
    positionEntity3D(left, -40, 0, 0);
    positionEntity3D(right, 40, 0, 0);
    setEntityTexture3D(left, red);
    setEntityTexture3D(right, green);

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 10000);
    int r, g, b;
    Display3D::rgb(d.pixel(49, 120), r, g, b);   // left cube's front face centre
    CHECK(r > 90); CHECK(r > g + 60); CHECK(r > b + 60);
    Display3D::rgb(d.pixel(271, 120), r, g, b);  // right cube's front face centre
    CHECK(g > 90); CHECK(g > r + 60); CHECK(g > b + 40);

    // Freeing a texture clears the entity overrides that referenced it
    REQUIRE(freeTexture3D(green));
    d.render();
    Display3D::rgb(d.pixel(271, 120), r, g, b);
    CHECK(r > 100); CHECK(r < 160); CHECK(g > 100); CHECK(b > 100);  // back to untextured ambient grey
    Display3D::rgb(d.pixel(49, 120), r, g, b);
    CHECK(r > 90); CHECK(r > g + 60);                  // left one still red

    // An explicit -1 override also falls back to the (untextured) mesh
    setEntityTexture3D(left, -1);
    d.render();
    Display3D::rgb(d.pixel(49, 120), r, g, b);
    CHECK(r > 100); CHECK(r < 160); CHECK(g > 100); CHECK(b > 100);
    resetWorld3D();
}

static void billHalfKeyColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    if (x < 8) { *r = 248; *g = 0; *b = 248; }   // RGB565 0xF81F, the key color
    else { *r = 255; *g = 255; *b = 255; }
}

static void billWhiteColor(int x, int y, uint8_t* r, uint8_t* g, uint8_t* b) {
    *r = 255; *g = 255; *b = 255;
}

TEST_CASE("engine3d: billboards key out their masking color and spin in the screen plane", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_billboard_key");
    writeSdFile(root, "models/halfkey.bmp", makeBmp24(16, 16, billHalfKeyColor));
    resetWorld3D();

    int16_t tex = loadTexture3D(String("/models/halfkey.bmp"));
    REQUIRE(tex >= 0);
    int16_t bill = createBillboard3D(tex, 0xF81F, false);
    REQUIRE(bill >= 0);
    CHECK(bill >= BILLBOARD3D_HANDLE_BASE);
    scaleBillboard3D(bill, 100, 100);

    Display3D d;
    int r, g, b;
    // Billboard at the origin, default camera at (0,0,-100): the quad is centred on
    // screen with a ~104 px half-size, so 110 and 210 sit in opposite halves.
    d.render();
    Display3D::rgb(d.pixel(210, 120), r, g, b);
    CHECK(r > 100); CHECK(g > 100); CHECK(b > 100);
    Display3D::rgb(d.pixel(110, 120), r, g, b);
    CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);            // keyed texels keep the background
    CHECK(d.litPixels() > 8000);                  // the unkeyed half of the quad

    // The quad always faces the camera, so a spin turns the image in the screen plane:
    // the keyed half moves from the left to the bottom.
    rotateBillboard3D(bill, 90);
    d.render();
    Display3D::rgb(d.pixel(160, 70), r, g, b);
    CHECK(r > 100); CHECK(g > 100); CHECK(b > 100);
    Display3D::rgb(d.pixel(160, 170), r, g, b);
    CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);

    turnBillboard3D(bill, 90);                    // 180 total: keyed half on the right
    d.render();
    Display3D::rgb(d.pixel(210, 120), r, g, b);
    CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);
    Display3D::rgb(d.pixel(110, 120), r, g, b);
    CHECK(r > 100); CHECK(g > 100); CHECK(b > 100);

    REQUIRE(freeBillboard3D(bill));
    CHECK(!freeBillboard3D(bill));
    resetWorld3D();
}

TEST_CASE("engine3d: an unkeyed billboard covers its whole quad and hides on request", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_billboard_hide");
    writeSdFile(root, "models/plain.bmp", makeBmp24(16, 16, billWhiteColor));
    resetWorld3D();

    int16_t tex = loadTexture3D(String("/models/plain.bmp"));
    REQUIRE(tex >= 0);
    int16_t bill = createBillboard3D(tex, -1, false);
    REQUIRE(bill >= 0);
    scaleBillboard3D(bill, 100, 100);

    Display3D d;
    d.render();
    CHECK(d.litPixels() > 40000);                 // ~208x208 quad at 50% ambient
    hideBillboard3D(bill, true);
    d.render();
    CHECK(d.litPixels() == 0);
    hideBillboard3D(bill, false);
    d.render();
    CHECK(d.litPixels() > 40000);
    resetWorld3D();
}

TEST_CASE("engine3d: billboards shade from ambient and emission, lights only when created lit", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_billboard_light");
    writeSdFile(root, "models/plain.bmp", makeBmp24(16, 16, billWhiteColor));
    resetWorld3D();
    ambientLight3D(0, 0, 0);

    int16_t tex = loadTexture3D(String("/models/plain.bmp"));
    REQUIRE(tex >= 0);
    int16_t unlit = createBillboard3D(tex, -1, false);
    int16_t lit = createBillboard3D(tex, -1, true);
    REQUIRE(unlit >= 0);
    REQUIRE(lit >= 0);
    positionBillboard3D(unlit, -60, 0, 0);
    positionBillboard3D(lit, 60, 0, 0);
    scaleBillboard3D(unlit, 100, 100);
    scaleBillboard3D(lit, 100, 100);

    Display3D d;
    int r, g, b;
    d.render();
    CHECK(d.litPixels() == 0);                    // no ambient, no lights, no emission

    int16_t light = createLight3D(255, 255, 255, 0, 0, 0, 0, LIGHT3D_TYPE_DIRECTIONAL);
    REQUIRE(light >= 0);
    setLightDirection3D(light, 0, 0, 1);          // beam from behind the camera towards the scene
    d.render();
    Display3D::rgb(d.pixel(220, 120), r, g, b);   // lit billboard, camera-facing normal
    CHECK(r > 200); CHECK(g > 200); CHECK(b > 200);
    Display3D::rgb(d.pixel(100, 120), r, g, b);   // the unlit one ignores it
    CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);
    freeLight3D(light);

    setBillboardEmissive3D(unlit, 255, 0, 0);
    d.render();
    Display3D::rgb(d.pixel(100, 120), r, g, b);
    CHECK(r > 240); CHECK(g < 10); CHECK(b < 10);
    resetWorld3D();
}

TEST_CASE("engine3d: a keyed billboard draws after opaque geometry so its key lets them through", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_billboard_order");
    writeSdFile(root, "models/keyhalf.bmp", makeBmp24(16, 16, billHalfKeyColor));
    resetWorld3D();
    ambientLight3D(255, 255, 255);

    int16_t tex = loadTexture3D(String("/models/keyhalf.bmp"));
    REQUIRE(tex >= 0);
    int16_t cube = createEntity3D(createMesh3DCube());
    REQUIRE(cube >= 0);
    entityColor3D(cube, 200, 60, 60);

    int16_t bill = createBillboard3D(tex, 0xF81F, false);
    REQUIRE(bill >= 0);
    scaleBillboard3D(bill, 100, 100);
    positionBillboard3D(bill, 0, 0, -40);  // nearer than the cube's visible face: view z 60 vs 75

    Display3D d;
    int r, g, b;
    d.render();
    Display3D::rgb(d.pixel(120, 120), r, g, b);   // key half: the cube behind still shows
    CHECK(r > 120); CHECK(g < 80); CHECK(b < 80);
    Display3D::rgb(d.pixel(210, 120), r, g, b);   // unkeyed half covers the cube
    CHECK(r > 200); CHECK(g > 200); CHECK(b > 200);
    resetWorld3D();
}

TEST_CASE("engine3d: keyed texels stay unclaimed so the gap fill owns them", "[engine3d]") {
    std::filesystem::path root = makeSdRoot("engine3d_billboard_mask");
    writeSdFile(root, "models/keyhalf.bmp", makeBmp24(16, 16, billHalfKeyColor));
    // The compat shim refuses the engine3d helper tasks so drawing stays
    // single-threaded; this case asserts the pipelined gap fill, so it opts in.
    setenv("PEPIT_COMPAT_ALLOW_3D_TASKS", "1", 1);
    resetWorld3D();
    ambientLight3D(255, 255, 255);

    int16_t tex = loadTexture3D(String("/models/keyhalf.bmp"));
    REQUIRE(tex >= 0);
    int16_t bill = createBillboard3D(tex, 0xF81F, false);
    REQUIRE(bill >= 0);
    scaleBillboard3D(bill, 100, 100);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240, 2);
    disp.frameBuffer(1);

    beginWorld3DFill3D(&disp);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    blitWorld3DFrame3D(&disp);
    quiesceWorld3DBlit3D();

    beginWorld3DFill3D(&disp);
    REQUIRE(world3DMaskMode3D(&disp));

    // A color the billboard cannot produce, planted inside the quad's key half:
    // if the keyed span claimed the whole row the gap fill would leave it there.
    int8_t target = disp.frameBufferId();
    uint16_t* buf = (uint16_t*)disp.frameBuffer(target);
    for (int y = 110; y < 130; y++)
        for (int x = 110; x < 130; x++) buf[y * 320 + x] = __builtin_bswap16(0x001F);

    updateWorld3D(&disp);
    drawWorld3D(&disp);
    blitWorld3DFrame3D(&disp);
    quiesceWorld3DBlit3D();

    int8_t drawnId = disp.frameBufferId() == target ? target : (target == 1 ? 2 : 1);
    const uint16_t* pushed = (const uint16_t*)disp.frameBuffer(drawnId);
    auto pushedPixel = [pushed](int x, int y) {
        return __builtin_bswap16(pushed[y * 320 + x]);
    };
    int r, g, b;
    Display3D::rgb(pushedPixel(120, 120), r, g, b);
    CHECK(r == 0); CHECK(g == 0); CHECK(b == 0);          // gap-filled, not the sentinel
    Display3D::rgb(pushedPixel(210, 120), r, g, b);
    CHECK(r > 200); CHECK(g > 200); CHECK(b > 200);       // the drawn half claims its pixels
    disp.deleteSprite();
    resetWorld3D();
}
