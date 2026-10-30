/*
 * PEPitCompat — Unit tests: game selection (src/hardware/sdHandler.cpp)
 *
 * Covers gameSupportsTaskTypesAndIsNotHidden(), getNumberOfGames() and
 * getGamePath() against a fixture /games tree:
 *
 *   monsterGame/   template=monster            -> supports all 4 base types
 *   scriptGame/    4 existing script files     -> supports all 4 base types
 *   hiddenGame/    template=race, isHidden     -> excluded
 *   partialGame/   only 2 script files         -> supports 2 types only
 *   notAGame/      no gameconfig.ini           -> ignored
 *
 * NOTE: getGamePath() on a gameId that matches no game calls
 * checkFailWithMessage() (infinite loop) — deliberately not covered.
 *
 * The directory enumeration order (readdir) is not guaranteed, so the
 * getGamePath() tests assert set membership, not order.
 */

#include "catch_amalgamated.hpp"

#include <algorithm>

#include "sdHandler.h"
#include "gfxHandler.hpp"   // HIDDEN_GAME_ID
#include "constants.h"
#include "test_helpers.h"

// Implemented in sdHandler.cpp but not declared in any header.
bool gameSupportsTaskTypesAndIsNotHidden(String gamePath, uint32_t requiredTaskTypes, String* errorMessage);

using namespace pepit_test;

namespace {

const uint32_t ALL4 = REQUIRED_TASK_TYPE_SHORTBLOWS |
                       REQUIRED_TASK_TYPE_LONGBLOWS |
                       REQUIRED_TASK_TYPE_EQUALBLOWS |
                       REQUIRED_TASK_TYPE_TRAMPOLINE;

const char* MONSTER_INI =
    "[game]\n"
    "name=Monster Game\n"
    "template=monster\n"
    "prefsNamespace=monster\n"
    "isHidden=false\n";

const char* HIDDEN_INI =
    "[game]\n"
    "name=Hidden Game\n"
    "template=race\n"
    "prefsNamespace=hidden\n"
    "isHidden=true\n";

const char* SCRIPT_INI =
    "[game]\n"
    "name=Script Game\n"
    "template=lua\n"
    "prefsNamespace=script\n"
    "pepShortScriptPath=scripts/short.lua\n"
    "pepLongScriptPath=scripts/long.lua\n"
    "pepEqualScriptPath=scripts/equal.lua\n"
    "trampolineScriptPath=scripts/trampoline.lua\n"
    "isHidden=false\n";

const char* SCRIPT_INI_INCOMPLETE =
    "[game]\n"
    "name=Incomplete Script Game\n"
    "template=lua\n"
    "prefsNamespace=scriptincomplete\n"
    "pepShortScriptPath=scripts/short.lua\n"
    "pepLongScriptPath=scripts/long.lua\n"
    "isHidden=false\n";

/** Build the standard fixture tree; returns the SD root path. */
auto buildGameTree() {
    auto root = makeSdRoot("gametree");
    writeSdFile(root, "games/monsterGame/gameconfig.ini", MONSTER_INI);
    writeSdFile(root, "games/scriptGame/gameconfig.ini", SCRIPT_INI);
    writeSdFile(root, "games/scriptGame/scripts/short.lua", "-- short\n");
    writeSdFile(root, "games/scriptGame/scripts/long.lua", "-- long\n");
    writeSdFile(root, "games/scriptGame/scripts/equal.lua", "-- equal\n");
    writeSdFile(root, "games/scriptGame/scripts/trampoline.lua", "-- tramp\n");
    writeSdFile(root, "games/hiddenGame/gameconfig.ini", HIDDEN_INI);
    writeSdFile(root, "games/partialGame/gameconfig.ini", SCRIPT_INI_INCOMPLETE);
    writeSdFile(root, "games/partialGame/scripts/short.lua", "-- short\n");
    writeSdFile(root, "games/partialGame/scripts/long.lua", "-- long\n");
    writeSdFile(root, "games/notAGame/readme.txt", "not a game\n");
    return root;
}

}  // namespace

// ============================================================
// gameSupportsTaskTypesAndIsNotHidden
// ============================================================

TEST_CASE("gameSupportsTaskTypesAndIsNotHidden: template shortcut gives all 4 base types", "[game]") {
    buildGameTree();
    String err;
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/monsterGame/", ALL4, &err) == true);
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/monsterGame/", REQUIRED_TASK_TYPE_TRAMPOLINE, &err) == true);
}

TEST_CASE("gameSupportsTaskTypesAndIsNotHidden: template games do NOT support inhalation", "[game]") {
    buildGameTree();
    String err;
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/monsterGame/",
            ALL4 | REQUIRED_TASK_TYPE_INHALATION, &err) == false);
}

TEST_CASE("gameSupportsTaskTypesAndIsNotHidden: script game gains a type per existing script file", "[game]") {
    buildGameTree();
    String err;
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/scriptGame/", ALL4, &err) == true);
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/scriptGame/",
            ALL4 | REQUIRED_TASK_TYPE_INHALATION, &err) == false);
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/partialGame/",
            REQUIRED_TASK_TYPE_SHORTBLOWS | REQUIRED_TASK_TYPE_LONGBLOWS, &err) == true);
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/partialGame/", ALL4, &err) == false);
}

TEST_CASE("gameSupportsTaskTypesAndIsNotHidden: a hidden game never qualifies, even if it supports everything", "[game]") {
    buildGameTree();
    String err;
    REQUIRE(gameSupportsTaskTypesAndIsNotHidden("/games/hiddenGame/", ALL4, &err) == false);
    REQUIRE(err.isEmpty());  // the function reports nothing — callers check the bool
}

// ============================================================
// getNumberOfGames
// ============================================================

TEST_CASE("getNumberOfGames: counts only qualifying (non-hidden) games", "[game]") {
    buildGameTree();
    String err;
    // monsterGame + scriptGame qualify; hiddenGame/partialGame/notAGame don't
    REQUIRE(getNumberOfGames(&err, ALL4) == 2);
    REQUIRE(err.isEmpty());
}

TEST_CASE("getNumberOfGames: with a single required type, the partial game counts too", "[game]") {
    buildGameTree();
    String err;
    // monster + script + partial all support short blows
    REQUIRE(getNumberOfGames(&err, REQUIRED_TASK_TYPE_SHORTBLOWS) == 3);
}

// ============================================================
// getGamePath
// ============================================================

TEST_CASE("getGamePath: ids 0 and 1 map onto the two qualifying games (order-independent)", "[game]") {
    buildGameTree();
    String err0, err1;
    String p0 = getGamePath(0, ALL4, &err0);
    String p1 = getGamePath(1, ALL4, &err1);

    REQUIRE(err0.isEmpty());
    REQUIRE(err1.isEmpty());

    const std::string s0 = p0.c_str();
    const std::string s1 = p1.c_str();
    const std::string monster = "/games/monsterGame/";
    const std::string script = "/games/scriptGame/";

    REQUIRE((s0 == monster || s0 == script));
    REQUIRE((s1 == monster || s1 == script));
    REQUIRE(s0 != s1);  // each qualifying game is counted exactly once
}

TEST_CASE("getGamePath: HIDDEN_GAME_ID resolves the hidden game", "[game]") {
    buildGameTree();
    String err;
    String path = getGamePath(HIDDEN_GAME_ID, 0, &err);
    REQUIRE(path == "/games/hiddenGame/");
}
