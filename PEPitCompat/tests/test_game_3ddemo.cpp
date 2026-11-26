/*
 * PEPitCompat — Integration test: run the real 3ddemo Lua game
 *
 * Boots SDCardContent/games/3ddemo through the exact firmware entry points
 * (initGames_lua + drawShortBlowGame_lua/drawLongBlowGame_lua/
 * drawEqualBlowGame_lua + endGame_lua), feeding synthetic BlowData like the
 * physio loop does. The game itself drives the 3D engine through Lua
 * (LoadMesh, CreateEntity, CreateLight, UpdateWorld, DrawWorld), so this
 * proves the whole chain: SD loading -> Lua bindings -> engine -> display.
 *
 * Rendered frames are saved as BMPs under $PEPIT_DATA_DIR/3ddemo_shots/.
 */

#include "catch_amalgamated.hpp"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <string>
#include <vector>

#include "Arduino.h"
#include "TFT_eSPI.h"
#include "constants.h"
#include "gameLua.h"
#include "test_helpers.h"

using pepit_test::makeSdRoot;
using pepit_test::writeSdFile;

namespace {

// One shared sprite across the game tests: gameLua keeps process-wide
// luaDisplay/luaFramebuffer pointers, so per-test sprites would dangle.
TFT_eSprite& gameSprite() {
    static TFT_eSprite disp = TFT_eSprite(&tft);
    static bool created = false;
    if (!created) {
        disp.setColorDepth(16);
        disp.createSprite(320, 240);
        created = true;
    }
    return disp;
}

std::string readRepoFile(const std::filesystem::path& p) {
    std::ifstream f(p, std::ios::binary);
    REQUIRE(f);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

void copyGameTree(const std::filesystem::path& src, const std::filesystem::path& sdRoot,
                  const std::string& rel) {
    for (auto& entry : std::filesystem::directory_iterator(src)) {
        if (entry.is_directory()) {
            copyGameTree(entry.path(), sdRoot, rel + "/" + entry.path().filename().string());
        } else {
            writeSdFile(sdRoot, rel + "/" + entry.path().filename().string(),
                        readRepoFile(entry.path()));
        }
    }
}

void saveShot(const std::filesystem::path& dir, const char* name, TFT_eSprite& disp) {
    int w = disp.width(), h = disp.height();
    const uint16_t* fb = (const uint16_t*)disp.getPointer();
    uint32_t rowSize = (w * 3 + 3) & ~3u;
    std::string out(54, 0);
    auto p32 = [&](size_t o, uint32_t v) { for (int i = 0; i < 4; i++) out[o + i] = char((v >> (8 * i)) & 0xFF); };
    auto p16 = [&](size_t o, uint16_t v) { out[o] = char(v & 0xFF); out[o + 1] = char((v >> 8) & 0xFF); };
    out[0] = 'B'; out[1] = 'M';
    p32(2, 54 + rowSize * h); p32(10, 54); p32(14, 40); p32(18, w); p32(22, h);
    p16(26, 1); p16(28, 24); p32(34, rowSize * h);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint16_t n = __builtin_bswap16(fb[(h - 1 - y) * w + x]);  // buffer is byteswapped
            out += char((n & 31) * 255 / 31);                         // B
            out += char(((n >> 5) & 63) * 255 / 63);                  // G
            out += char((n >> 11) * 255 / 31);                        // R
        }
        out.append(rowSize - w * 3, 0);
    }
    std::ofstream f(dir / name, std::ios::binary);
    f << out;
}

int litPixels(TFT_eSprite& disp) {
    const uint16_t* fb = (const uint16_t*)disp.getPointer();
    int n = 0;
    for (int i = 0; i < disp.width() * disp.height(); i++) if (fb[i]) n++;
    return n;
}

}  // namespace

TEST_CASE("3ddemo game boots through real Lua entry points and renders 3D frames", "[engine3d][game]") {
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path gameDir = repoRoot / "WIPSDCardContent" / "games" / "3ddemo";
    if (!std::filesystem::exists(gameDir / "gameconfig.ini"))
        SKIP("3ddemo game lives in WIPSDCardContent, which is not committed");

    std::filesystem::path sdRoot = makeSdRoot("3ddemo");
    copyGameTree(gameDir, sdRoot, "games/3ddemo");

    TFT_eSprite& disp = gameSprite();
    luaDisplay = &disp;

    const char* dataDir = getenv("PEPIT_DATA_DIR");
    std::filesystem::path shots = std::filesystem::path(dataDir ? dataDir : ".") / "3ddemo_shots";
    std::filesystem::create_directories(shots);

    auto MARK=[](const char*m){printf("STAGE %s\n",m);fflush(stdout);};
    String errorMessage = "";
    GameConfig config;
    MARK("readGameConfig"); readGameConfig("/games/3ddemo/", &config, &errorMessage); MARK("after readGameConfig");
    CHECK(config.templateName == String("lua"));
    CHECK(config.pepShortScriptPath == String("shortBlow.lua"));

    String initErrors = "";
    MARK("initGames_lua"); initGames_lua("/games/3ddemo/", &config, &initErrors); MARK("after initGames_lua");
    INFO("initGames_lua errors: " + initErrors.data);
    CHECK(initErrors.data.empty());

    BlowData blow{};
    blow.currentlyBlowing = true;
    blow.ms = 1000;
    blow.pressure = 70.0f;
    blow.peakPressure = 70;
    blow.totalTaskNumber = 1;
    blow.totalBlowCount = 3;
    blow.taskType = 1;

    disp.fillSprite(0); MARK("drawShort1"); drawShortBlowGame_lua(&disp, &blow, &errorMessage); MARK("after drawShort1");
    INFO("shortBlow errors: " + errorMessage.data);
    CHECK(errorMessage.data.empty());
    CHECK(litPixels(disp) > 3000);
    saveShot(shots, "frame1_short_blow.bmp", disp);

    blow.ms = 1100;  // MsDelta 100 -> scene turns further
    disp.fillSprite(0); MARK("drawShort2"); drawShortBlowGame_lua(&disp, &blow, &errorMessage); MARK("after drawShort2");
    CHECK(errorMessage.data.empty());
    CHECK(litPixels(disp) > 3000);
    saveShot(shots, "frame2_short_blow_turned.bmp", disp);

    blow.ms = 1200;
    disp.fillSprite(0); MARK("drawLong"); drawLongBlowGame_lua(&disp, &blow, &errorMessage); MARK("after drawLong");
    CHECK(errorMessage.data.empty());
    CHECK(litPixels(disp) > 3000);
    saveShot(shots, "frame3_long_blow_lifted.bmp", disp);

    blow.ms = 1300;
    disp.fillSprite(0); MARK("drawEqual"); drawEqualBlowGame_lua(&disp, &blow, &errorMessage); MARK("after drawEqual");
    CHECK(errorMessage.data.empty());
    CHECK(litPixels(disp) > 3000);
    saveShot(shots, "frame4_equal_blow_orbit.bmp", disp);

    MARK("endGame"); endGame_lua(&errorMessage); MARK("after endGame");
    INFO("endGame errors: " + errorMessage.data);
    CHECK(errorMessage.data.empty());

    INFO("screenshots written to " + shots.string());
}
TEST_CASE("3ddemo consecutive frames are stable (flicker check)", "[game][flicker]") {
    std::filesystem::path repoRoot = std::filesystem::path(__FILE__).parent_path()
        .parent_path().parent_path();
    std::filesystem::path gameDir = repoRoot / "WIPSDCardContent" / "games" / "3ddemo";
    if (!std::filesystem::exists(gameDir / "gameconfig.ini"))
        SKIP("3ddemo game lives in WIPSDCardContent, which is not committed");

    std::filesystem::path sdRoot = makeSdRoot("flicker");
    copyGameTree(gameDir, sdRoot, "games/3ddemo");

    TFT_eSprite& disp = gameSprite();
    luaDisplay = &disp;

    String errorMessage = "";
    String initErrors = "";
    initGames_lua("/games/3ddemo/", nullptr, &initErrors);

    BlowData blow{};
    blow.currentlyBlowing = true;
    blow.pressure = 70.0f;
    blow.totalTaskNumber = 1;
    blow.totalBlowCount = 12;
    blow.taskType = 1;

    const int FRAMES = 12;
    std::vector<uint16_t> prev, cur(320 * 240);
    int diffs[FRAMES];
    int lit[FRAMES];
    for (int f = 0; f < FRAMES; f++) {
        blow.ms = 1000 + f * 100;
        disp.fillSprite(0);
    drawShortBlowGame_lua(&disp, &blow, &errorMessage);
        const uint16_t* fb = (const uint16_t*)disp.getPointer();
        int n = 0;
        for (int i = 0; i < 320 * 240; i++) {
            cur[i] = fb[i];
            if (fb[i]) n++;
        }
        lit[f] = n;
        diffs[f] = f == 0 ? 0 : 0;
        if (f > 0) {
            int d = 0;
            for (int i = 0; i < 320 * 240; i++) if (cur[i] != prev[i]) d++;
            diffs[f] = d;
        }
        prev = cur;
        if (f == FRAMES - 1) {
            const char* dataDir = getenv("PEPIT_DATA_DIR");
            std::filesystem::path shots = std::filesystem::path(dataDir ? dataDir : ".") / "3ddemo_shots";
            std::filesystem::create_directories(shots);
            saveShot(shots, "flicker_last.bmp", disp);
        }
    }
    int maxDiff = 0;
    for (int f = 1; f < FRAMES; f++) {
        printf("frame %2d: lit=%5d diff=%5d\n", f, lit[f], diffs[f]);
        if (diffs[f] > maxDiff) maxDiff = diffs[f];
        CHECK(lit[f] > 3000);       // every frame renders geometry
        CHECK(diffs[f] < 25000);    // rotation step 3deg/frame accounts for this
    }
    CHECK(errorMessage.data.empty());
}
