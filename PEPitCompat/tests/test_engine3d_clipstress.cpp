/* engine3d robustness: geometry engulfing the camera must render interior walls
 * via near-plane clipping instead of crashing (happens when a camera moves
 * through level geometry). */
#include "catch_amalgamated.hpp"

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "engine3d.hpp"
#include "test_helpers.h"

TEST_CASE("engine3d: camera inside a giant mesh clips interior walls instead of crashing", "[engine3d]") {
    resetWorld3D();
    int16_t mesh = createMesh3DCube();
    int16_t big = createEntity3D(mesh);
    scaleEntity3D(big, 25, 25, 25);  // giant cube engulfing the camera
    setMeshFlipped3D(mesh, true);    // inward walls face the camera: clip fans straddle near

    TFT_eSprite disp = TFT_eSprite(&tft);
    disp.setColorDepth(16);
    disp.createSprite(320, 240);
    disp.fillSprite(0);
    updateWorld3D(&disp);
    drawWorld3D(&disp);
    const uint16_t* fb = (const uint16_t*)disp.getPointer();
    int n = 0;
    for (int i = 0; i < 320 * 240; i++) if (fb[i]) n++;
    CHECK(n > 0);  // should render the surrounding interior walls, not crash

    disp.deleteSprite();
    resetWorld3D();
}
