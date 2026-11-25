/*
 * PEPitCompat — 3D engine micro-benchmark (hidden: run it with ./pepit_tests "[perf]")
 *
 * Renders the hardware 3ddemo scene (race car + wheels + ground plane + sphere,
 * three lights, CameraRange 1..4000) at the panel resolution and reports the
 * engine's own phase timings and workload counters.
 *
 * Native timings are NOT ESP32 timings: this host has hardware float divide and
 * a fast memory system, so only the counters and the *relative* change between
 * two builds of the engine are meaningful. On the device the same numbers come
 * from the serial log written by reportWorld3DFrame3D (systemConfig.ini key
 * debugLogRender3D).
 */

#include "catch_amalgamated.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "engine3d.hpp"
#include "test_helpers.h"

using pepit_test::makeSdRoot;
using pepit_test::writeSdFile;

namespace {

std::string readRepoFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    REQUIRE(f);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

}  // namespace

TEST_CASE("engine3d perf: 3ddemo scene phase timings", "[perf][.disabled]") {
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path models = repoRoot / "SDCardContent" / "games" / "3ddemo" / "models";

    std::filesystem::path root = makeSdRoot("engine3d_perf");
    for (const char* name : {"racecar.obj", "racecar.mtl", "racecar.bmp",
                             "wheel_front.obj", "wheel_front.mtl",
                             "wheel_rear.obj", "wheel_rear.mtl"})
        writeSdFile(root, std::string("models/") + name, readRepoFile(models / name));

    resetWorld3D();
    int16_t body = loadMesh3D(String("/models/racecar.obj"));
    int16_t wf = loadMesh3D(String("/models/wheel_front.obj"));
    int16_t wr = loadMesh3D(String("/models/wheel_rear.obj"));
    REQUIRE(body >= 0);
    REQUIRE(wf >= 0);
    REQUIRE(wr >= 0);

    const float carY = -32.0f;
    int16_t car = createEntity3D(body);
    REQUIRE(car >= 0);
    for (auto wp : {std::make_pair(wf, 15.12f), std::make_pair(wr, -14.16f)}) {
        for (float sx : {1.0f, -1.0f}) {
            int16_t w = createEntity3D(wp.first);
            REQUIRE(w >= 0);
            REQUIRE(linkEntity3D(w, car));
            positionEntity3D(w, sx * (wp.second > 0 ? 9.0f : 9.4f), wp.second > 0 ? 4.6f : 5.1f, wp.second);
        }
    }
    positionEntity3D(car, 0, carY, 0);

    int16_t ground = createEntity3D(createMesh3DPlane(600, 600));
    positionEntity3D(ground, 0, carY, 0);
    entityColor3D(ground, 140, 140, 160);
    int16_t sphere = createEntity3D(createMesh3DSphere(22, 14));
    positionEntity3D(sphere, 80, -8, 60);
    entityColor3D(sphere, 255, 220, 80);

    positionCamera3D(0, 0, 2, -76);
    rotateCamera3D(0, -14, 0, 0);
    cameraRange3D(0, 1.0f, 4000.0f);
    ambientLight3D(35, 35, 40);
    createLight3D(255, 240, 215, 90, 120, -80, 500, LIGHT3D_TYPE_POINT);
    createLight3D(70, 100, 255, -150, 20, 80, 700, LIGHT3D_TYPE_POINT);
    int16_t spot = createLight3D(255, 100, 70, -30, 170, -60, 600, LIGHT3D_TYPE_SPOT);
    setLightDirection3D(spot, 0.15, -1, 0.3);

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);

    const int FRAMES = 40;
    setWorld3DDebug3D(true);
    resetWorld3DPerf3D();
    double updateMs = 0, drawMs = 0;
    int lit = 0;
    for (int f = 0; f < FRAMES; f++) {
        turnEntity3D(car, 0, 3.0f, 0);
        disp.fillSprite(0);
        auto t0 = std::chrono::steady_clock::now();
        updateWorld3D(&disp);
        auto t1 = std::chrono::steady_clock::now();
        drawWorld3D(&disp);
        auto t2 = std::chrono::steady_clock::now();
        updateMs += std::chrono::duration<double, std::milli>(t1 - t0).count();
        drawMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
        // blit exercises the production frame-end path: on device it pipelines
        // the push to the core 0 task; on host the shim runs it on a thread.
        const uint16_t* fb = (const uint16_t*)disp.getPointer();
        blitWorld3DFrame3D(&disp);
        lit = 0;
        for (int i = 0; i < 320 * 240; i++) if (fb[i]) lit++;
    }

    World3DPerf3D perf;
    getWorld3DPerf3D(&perf);
    quiesceWorld3DBlit3D();
    disp.deleteSprite();
    setWorld3DDebug3D(false);
    resetWorld3DPerf3D();

    double frames = (double)FRAMES;
    printf("3ddemo perf (%d frames, 320x240):\n", FRAMES);
    printf("  host wall time  update=%.3f ms  draw=%.3f ms  total=%.3f ms  -> %.1f fps\n",
           updateMs / frames, drawMs / frames, (updateMs + drawMs) / frames,
           1000.0 * frames / (updateMs + drawMs));
    printf("  engine timing   update=%.3f ms (xf=%.3f face=%.3f bucket=%.3f)  draw=%.3f ms\n",
           perf.updateUs / 1000.0 / frames, perf.xfUs / 1000.0 / frames,
           perf.faceUs / 1000.0 / frames, perf.bucketUs / 1000.0 / frames, perf.drawUs / 1000.0 / frames);
    printf("  workload        verts=%.0f  tris=%.0f  clipped=%.0f  spans=%.0f  px=%.0f (per frame)\n",
           perf.vertsTransformed / frames, perf.trisSubmitted / frames, perf.trisClipped / frames,
           perf.spans / frames, perf.pixels / frames);
    printf("  coverage        lit pixels in last frame = %d\n", lit);
    fflush(stdout);

    CHECK(lit > 3000);
    CHECK(perf.frames == (uint32_t)FRAMES);
    CHECK(perf.pushUs > 0);                    // the pipelined push task measured its runs
    int64_t partsUs = (int64_t)perf.xfUs + perf.faceUs + perf.bucketUs;
    // sub-phase timers must partition updateUs (plus timer-call slack)
    CHECK(partsUs <= (int64_t)perf.updateUs + 5 * FRAMES * 1000);
    CHECK(partsUs >= 0);
    CHECK(perf.trisSubmitted > 1000);
    CHECK(perf.spans > perf.trisSubmitted);      // every triangle covers several scanlines
    CHECK(perf.pixels > perf.spans);             // every span covers several pixels
    CHECK(updateMs > 0.0);
    CHECK(drawMs > 0.0);

    resetWorld3D();
}
