/*
 * PEPitCompat — Unit tests: config parsing (src/hardware/sdHandler.cpp)
 *
 * Covers readSystemConfig(), readProfileData(), getNumberOfProfiles() and
 * gameconfig parsing, run against the POSIX-backed SD_MMC shim with per-test
 * fixture files.
 *
 * FIXTURES MUST BE COMPLETE for the task types they use: missing required
 * keys make readProfileData() call checkFailWithMessage(), which is an
 * infinite loop even on the shim.
 */

#include "catch_amalgamated.hpp"

#include <cstring>

#include "sdHandler.h"
#include "systemconfig.h"
#include "constants.h"
#include "test_helpers.h"

using namespace pepit_test;

namespace {

// Mirrors data/systemConfig.ini key set (all keys present, non-default values)
const char* FULL_SYSTEM_CONFIG =
    "[system]\n"
    "wifiSSID = NetA\n"
    "wifiPassword = passA\n"
    "wifiSSID2 = NetB\n"
    "wifiPassword2 = passB\n"
    "wifiSSID3 = NetC\n"
    "wifiPassword3 = passC\n"
    "touchScreenSensitivity = 40\n"
    "simulateTrampoline = true\n"
    "simulateBlowing = 1\n"
    "simulateInhalation = false\n"
    "debugLogBlowPressure = true\n"
    "debugLogTrampoline = false\n"
    "logExecutions = true\n"
    "timezoneOffset = 2\n"
    "leftHandMode = false\n"
    "manometerMode = true\n";

}  // namespace

// ============================================================
// readSystemConfig
// ============================================================

TEST_CASE("readSystemConfig: maps all fields", "[config]") {
    auto root = makeSdRoot("sysconfig-full");
    writeSdFile(root, "systemConfig.ini", FULL_SYSTEM_CONFIG);

    SystemConfig cfg = SystemConfig();
    String err;
    readSystemConfig(&cfg, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(cfg.wifiSsid == "NetA");
    REQUIRE(cfg.wifiPassword == "passA");
    REQUIRE(cfg.wifiSsid2 == "NetB");
    REQUIRE(cfg.wifiPassword2 == "passB");
    REQUIRE(cfg.wifiSsid3 == "NetC");
    REQUIRE(cfg.wifiPassword3 == "passC");
    REQUIRE(cfg.touchScreenZThreshold == 150);   // 2.5 * (100 - 40)
    REQUIRE(cfg.simulateTrampoline == true);
    REQUIRE(cfg.simulateBlows == true);          // "1" is true
    REQUIRE(cfg.simulateInhalation == false);
    REQUIRE(cfg.debugLogBlowPressure == true);
    REQUIRE(cfg.debugLogTrampoline == false);
    REQUIRE(cfg.logExecutions == true);
    REQUIRE(cfg.timezoneOffset == 120);          // 60 * 2
    REQUIRE(cfg.leftHandMode == false);
    REQUIRE(cfg.manometerMode == true);
}

TEST_CASE("readSystemConfig: absent keys fall back to defaults", "[config]") {
    auto root = makeSdRoot("sysconfig-min");
    writeSdFile(root, "systemConfig.ini", "[system]\nlogExecutions = false\n");

    SystemConfig cfg = SystemConfig();
    String err;
    readSystemConfig(&cfg, &err);

    REQUIRE(err.isEmpty());  // only logExecutions feeds the real errorMessage
    REQUIRE(cfg.wifiSsid.isEmpty());
    REQUIRE(cfg.touchScreenZThreshold == 250);  // sensitivity 0 -> 2.5 * 100
    REQUIRE(cfg.simulateTrampoline == false);
    REQUIRE(cfg.simulateBlows == false);
    REQUIRE(cfg.leftHandMode == true);          // default
    REQUIRE(cfg.manometerMode == false);        // default
    REQUIRE(cfg.timezoneOffset == 0);
    REQUIRE(cfg.logExecutions == false);
}

TEST_CASE("REGRESSION readSystemConfig: wifiSSID and wifiSSID2 resolve independently regardless of order", "[config]") {
    // Regression test: the old parser prefix-matched keys (strncmp without
    // a length check), so a file listing wifiSSID2 BEFORE wifiSSID gave
    // both fields the same value. Keys now match exactly, so each field
    // must get its own line's value.
    auto root = makeSdRoot("sysconfig-order");
    writeSdFile(root, "systemConfig.ini",
                "[system]\n"
                "wifiSSID2 = NetB\n"
                "wifiSSID = NetA\n"
                "logExecutions = false\n");

    SystemConfig cfg = SystemConfig();
    String err;
    readSystemConfig(&cfg, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(cfg.wifiSsid == "NetA");
    REQUIRE(cfg.wifiSsid2 == "NetB");
}

// ============================================================
// readProfileData
// ============================================================

TEST_CASE("readProfileData: multi-task profile with legacy alias", "[config]") {
    auto root = makeSdRoot("profile-multi");
    writeSdFile(root, "profiles.ini",
                "[profile_7]\n"
                "name=Testy\n"
                "imagePath=/gfx/t.bmp\n"
                "cycles=4\n"
                "task_0_type=pepLong\n"
                "task_0_minStrength=110\n"
                "task_0_targetStrength=200\n"
                "task_0_repetitions=10\n"
                "task_0_time=5000\n"
                "task_1_type=shortBlows\n"
                "task_1_minStrength=170\n"
                "task_1_targetStrength=300\n"
                "task_1_repetitions=5\n"
                "task_1_time=1000\n");

    ProfileData pd = ProfileData();
    String err;
    readProfileData(7, &pd, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(pd.name == "Testy");
    REQUIRE(pd.imagePath == "/gfx/t.bmp");
    REQUIRE(pd.cycles == 4);
    REQUIRE(pd.tasks == 2);
    REQUIRE(pd.taskType[0] == PROFILE_TASK_TYPE_LONGBLOWS);
    REQUIRE(pd.taskType[1] == PROFILE_TASK_TYPE_SHORTBLOWS);  // "shortBlows" alias
    REQUIRE(pd.taskMinStrength[0] == 110);
    REQUIRE(pd.taskTargetStrength[0] == 200);
    REQUIRE(pd.taskRepetitions[0] == 10);
    REQUIRE(pd.taskTime[0] == 5000);
    REQUIRE(pd.taskNegativeStrength[0] == false);
    REQUIRE(pd.taskMinStrength[1] == 170);
    REQUIRE(pd.taskTargetStrength[1] == 300);
    REQUIRE(pd.taskRepetitions[1] == 5);
    REQUIRE(pd.taskTime[1] == 1000);
}

TEST_CASE("readProfileData: trampoline profile uses defaults for optional strengths", "[config]") {
    auto root = makeSdRoot("profile-tramp");
    writeSdFile(root, "profiles.ini",
                "[profile_8]\n"
                "name=Tramp\n"
                "imagePath=/gfx/tr.bmp\n"
                "cycles=1\n"
                "task_0_type=trampoline\n"
                "task_0_time=300000\n");

    ProfileData pd = ProfileData();
    String err;
    readProfileData(8, &pd, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(pd.tasks == 1);
    REQUIRE(pd.taskType[0] == PROFILE_TASK_TYPE_TRAMPOLINE);
    REQUIRE(pd.taskTime[0] == 300000);
    REQUIRE(pd.taskMinStrength[0] == 250);   // default minJumpHeight
    REQUIRE(pd.taskTargetStrength[0] == 500); // default targetJumpHeight
}

TEST_CASE("readProfileData: inhalation+PEP dual-phase profile", "[config]") {
    auto root = makeSdRoot("profile-inhpep");
    writeSdFile(root, "profiles.ini",
                "[profile_9]\n"
                "name=InhPep\n"
                "imagePath=/gfx/i.bmp\n"
                "cycles=1\n"
                "task_0_type=inhalationPep\n"
                "task_0_minRepetitions=10\n"
                "task_0_inhalationTime=2500\n"
                "task_0_inhalationMinStrength=15\n"
                "task_0_inhalationTargetStrength=30\n"
                "task_0_exhalationTime=5000\n"
                "task_0_exhalationMinStrength=100\n"
                "task_0_exhalationTargetStrength=170\n");

    ProfileData pd = ProfileData();
    String err;
    readProfileData(9, &pd, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(pd.tasks == 1);
    REQUIRE(pd.taskType[0] == PROFILE_TASK_TYPE_INHALATIONPEP);
    REQUIRE(pd.taskRepetitions[0] == 10);
    REQUIRE(pd.taskNegativeStrength[0] == true);
    REQUIRE(pd.taskNegativeStrength2[0] == false);
    REQUIRE(pd.taskTime[0] == 2500);
    REQUIRE(pd.taskMinStrength[0] == 15);
    REQUIRE(pd.taskTargetStrength[0] == 30);
    REQUIRE(pd.taskTime2[0] == 5000);
    REQUIRE(pd.taskMinStrength2[0] == 100);
    REQUIRE(pd.taskTargetStrength2[0] == 170);
}

TEST_CASE("readProfileData: equalBlows alias maps to EQUALBLOWS", "[config]") {
    auto root = makeSdRoot("profile-alias");
    writeSdFile(root, "profiles.ini",
                "[profile_10]\n"
                "name=Alias\n"
                "imagePath=/gfx/a.bmp\n"
                "cycles=1\n"
                "task_0_type=equalBlows\n"
                "task_0_minStrength=100\n"
                "task_0_targetStrength=170\n"
                "task_0_repetitions=10\n"
                "task_0_time=5000\n");

    ProfileData pd = ProfileData();
    String err;
    readProfileData(10, &pd, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(pd.tasks == 1);
    REQUIRE(pd.taskType[0] == PROFILE_TASK_TYPE_EQUALBLOWS);
}

// NOTE: the "unknown task type" branch and missing required blow parameters
// both call checkFailWithMessage() (infinite loop) — not unit-testable
// without a firmware change. Deliberately not covered.

// ============================================================
// getNumberOfProfiles
// ============================================================

TEST_CASE("getNumberOfProfiles: counts until the first missing profile section", "[config]") {
    auto root = makeSdRoot("profile-count");
    writeSdFile(root, "profiles.ini",
                "[profile_0]\nname=A\n"
                "[profile_1]\nname=B\n"
                "[profile_2]\nname=C\n");

    String err;
    REQUIRE(getNumberOfProfiles(&err) == 3);
    REQUIRE(err.isEmpty());
}

TEST_CASE("getNumberOfProfiles: empty file yields 0 and an error message", "[config]") {
    auto root = makeSdRoot("profile-count-empty");
    writeSdFile(root, "profiles.ini", "");

    String err;
    REQUIRE(getNumberOfProfiles(&err) == 0);
    REQUIRE(err == "No profiles found!\n");
}

// ============================================================
// readGameConfig
// ============================================================

TEST_CASE("readGameConfig: maps game fields", "[config]") {
    auto root = makeSdRoot("gameconfig");
    writeSdFile(root, "games/testgame/gameconfig.ini",
                "[game]\n"
                "name=Test Game\n"
                "template=monster\n"
                "prefsNamespace=game-test\n"
                "pepShortScriptPath=scripts/short.lua\n"
                "isHidden=false\n");

    GameConfig gc = GameConfig();
    String err;
    readGameConfig("/games/testgame/", &gc, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(gc.name == "Test Game");
    REQUIRE(gc.templateName == "monster");
    REQUIRE(gc.prefsNamespace == "game-test");
    REQUIRE(gc.pepShortScriptPath == "scripts/short.lua");
    REQUIRE(gc.isHidden == false);
}

TEST_CASE("readGameConfig: trailing slash is added automatically", "[config]") {
    auto root = makeSdRoot("gameconfig-slash");
    writeSdFile(root, "games/testgame/gameconfig.ini",
                "[game]\n"
                "name=Slash Test\n"
                "template=monster\n"
                "prefsNamespace=game-test\n"
                "isHidden=true\n");

    GameConfig gc = GameConfig();
    String err;
    readGameConfig("/games/testgame", &gc, &err);

    REQUIRE(err.isEmpty());
    REQUIRE(gc.name == "Slash Test");
    REQUIRE(gc.isHidden == true);
}

TEST_CASE("readGameConfig: missing script paths are optional, but template/prefsNamespace are required", "[config]") {
    auto root = makeSdRoot("gameconfig-min");
    writeSdFile(root, "games/min/gameconfig.ini",
                "[game]\n"
                "name=Minimal\n"
                "template=race\n");

    GameConfig gc = GameConfig();
    String err;
    readGameConfig("/games/min/", &gc, &err);

    // All script paths are optional (ignoreErrors), but template and
    // prefsNamespace feed the real errorMessage.
    REQUIRE(err == "Key prefsNamespace not found in INI section.\n");
    REQUIRE(gc.name == "Minimal");
    REQUIRE(gc.templateName == "race");
    REQUIRE(gc.pepShortScriptPath.isEmpty());
    REQUIRE(gc.isHidden == false);
}
