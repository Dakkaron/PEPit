/*
 * PEPitCompat — Auto-Nav implementation
 *
 * Injects simulated touches so the firmware's own (blocking, interactive)
 * menu flow selects the requested profile and game. No T-HMI-PEPmonitor
 * source is modified: this file only calls the original code's public
 * functions and feeds the shim's simulated-touch channel.
 *
 * Mirrored firmware logic (kept in sync manually — see line refs):
 *   - slot geometry:          gfxHandler.cpp checkSelectionPageSelection()
 *   - profile list:           sdHandler.cpp getNumberOfProfiles() / readProfileData()
 *   - game list + filtering:  sdHandler.cpp getGamePath() + gameSupportsTaskTypesAndIsNotHidden()
 *   - requiredTaskTypes:      physioProtocolHandler.cpp runProfileSelection()
 */

#include "autonav.h"

#include "hardware/sdHandler.h"     // getIniSection, getIniValueFromSection, readGameConfig,
                                    // PROFILE_DATA_INI_PATH, GAMES_ROOT_DIR
#include "systemconfig.h"           // systemConfig (manometerMode)
#include "systemStateHandler.h"     // STATE_*, getSystemState()

#include <cstdio>
#include <cstdlib>

// Original globals / functions not exposed through an includable header:
// `profileData` — the profile the firmware just selected (physioProtocolHandler.cpp,
// non-static global; needed to derive requiredTaskTypes for the game filter).
extern ProfileData profileData;
// gameSupportsTaskTypesAndIsNotHidden — defined in sdHandler.cpp, external
// linkage but not declared in sdHandler.h; same binary, same String ABI.
extern bool gameSupportsTaskTypesAndIsNotHidden(String gamePath, uint32_t requiredTaskTypes, String* errorMessage);

// Simulated-touch channel (shim/touch.cpp): same mechanism as the MCP
// TOUCH_DOWN/TOUCH_UP commands. Must be declared at file scope (NOT inside
// the anonymous namespace below) so the references link against touch.cpp.
extern void simTouchPress(int x, int y);
extern void simTouchRelease();

namespace {

// ------------------------------------------------------------
// Selection-page slot geometry — mirrors checkSelectionPageSelection()
// (gfxHandler.cpp): hit zones are (20 + c*(cWidth+10), 10 + r*(cHeight+10),
// cWidth, cHeight) with cWidth=(290-10*columns)/columns,
// cHeight = rows==1 ? 220 : 105, columns=min(4,nr), rows = nr>4 ? 2 : 1.
// We tap the center of the zone.
// ------------------------------------------------------------

struct Tap {
    int x = 0;
    int y = 0;
};

Tap slotCenter(int index, int pageEntryCount) {
    int columns = pageEntryCount < 4 ? pageEntryCount : 4;
    int rows = pageEntryCount > 4 ? 2 : 1;
    int cWidth = (290 - 10 * columns) / columns;
    int cHeight = rows == 1 ? 220 : 105;
    int c = index % columns;
    int r = index / columns;
    Tap t;
    t.x = 20 + c * (cWidth + 10) + cWidth / 2;
    t.y = 10 + r * (cHeight + 10) + cHeight / 2;
    return t;
}

// ------------------------------------------------------------
// Tap state machine (per menu page)
//
// The firmware confirms a selection the same loop iteration it sees the
// press (level-triggered), but AFTER runProfileSelection() returns it
// drains with `while (touch.pressed()) vTaskDelay(1);` — so the press must
// be released. We therefore: press, hold a few touch queries, release,
// wait; if the page is still there, retry (bounded).
//
// "Touch queries" = updateMouseState() calls, i.e. XPT2046 method calls.
// One selection-loop iteration performs several of them, so these bounds
// are conservative in both directions.
// ------------------------------------------------------------

constexpr int kHoldQueries = 10;     // press duration before release
constexpr int kWaitQueries = 100;    // pause between retries
constexpr int kMaxAttempts = 10;     // then give up (leave page for manual control)
// Settle before the first tap: let the firmware complete at least one full
// selection-loop iteration (touch check + page draw + push) on the page.
// The firmware's menu-sprite array is allocated in refreshMenuSprites(),
// which only runs from drawProfileSelectionPage — i.e. on a *drawn* frame
// (gfxHandler.cpp). A tap consumed inside the page's very first iteration
// (the check runs before the draw) leaves zero drawn frames, and the next
// page that uses the sprites (drawGameSelectionPage) dereferences the
// still-null array and segfaults. 32 queries = ~2-3 iterations, so at
// least one drawn frame has always happened by the time we tap.
constexpr int kSettleQueries = 32;

enum class Phase { Idle, Tapping, Waiting, Done, GaveUp };

struct Stage {
    const char* spec = nullptr;      // raw --profile / --game value
    const char* label = nullptr;     // for log messages
    uint32_t state = 0;              // systemState this stage owns
    bool numeric = false;            // spec parsed as a slot index
    int value = 0;                   // the index (when numeric)
    bool resolved = false;
    int index = -1;                  // resolved slot index (-1 = not found)
    int total = -1;                  // selectable entries (guard + diagnostics)
    Tap tap;
    Phase phase = Phase::Idle;
    int counter = 0;
    int attempts = 0;
    int settle = 0;                  // kSettleQueries countdown before first tap
};

Stage g_profile;
Stage g_game;

bool specIsNumeric(const char* s, int* out) {
    if (!s || !*s) return false;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < 0 || v > 99) return false;
    *out = (int)v;
    return true;
}

void gProfileLog(const char* msg) {
    fprintf(stderr, "[autoNav] profile: %s\n", msg);
}

void gGameLog(const char* msg) {
    fprintf(stderr, "[autoNav] game: %s\n", msg);
}

// ------------------------------------------------------------
// Profile resolution — mirrors getNumberOfProfiles(): sequential
// [profile_N] sections in /profiles.ini, name from the `name` key.
// ------------------------------------------------------------

void resolveProfile(Stage& s) {
    s.resolved = true;
    s.index = -1;
    // Total first — the firmware's own counter (a full INI scan; it stops at
    // the first missing [profile_N] section). Needed for the slot geometry:
    // the page layout depends on how many profiles exist, NOT on where the
    // requested one is.
    String err;
    s.total = (int)getNumberOfProfiles(&err);
    if (!err.isEmpty()) {
        s.total = 0;
    }
    char buf[1024];
    for (int n = 0; n < s.total && n < 64; n++) {
        String e2;
        getIniSection(PROFILE_DATA_INI_PATH, "[profile_" + String(n) + "]", buf, sizeof(buf), &e2);
        if (!e2.isEmpty()) continue;
        if (s.numeric) {
            if (s.value == n) s.index = n;
        } else {
            String name;
            String nameErr;
            getIniValueFromSection(buf, "name", &name, &nameErr);
            if (name == s.spec) s.index = n;
        }
    }
    char msg[128];
    if (s.index < 0) {
        snprintf(msg, sizeof(msg),
                 "not resolved (%s, %d profiles) — leaving page for manual selection",
                 s.numeric ? "index out of range" : "name not found", s.total);
        gProfileLog(msg);
    } else {
        snprintf(msg, sizeof(msg), "resolved slot %d of %d", s.index, s.total);
        gProfileLog(msg);
    }
}

// ------------------------------------------------------------
// Game resolution — mirrors getNumberOfGames()/getGamePath():
// walk /games in the same openNextFile() order, keep entries that have a
// gameconfig.ini and pass gameSupportsTaskTypesAndIsNotHidden(); the slot
// index is the position in that filtered list, the name comes from the
// `name` key of gameconfig.ini.
// ------------------------------------------------------------

// requiredTaskTypes — mirrors runProfileSelection() (physioProtocolHandler.cpp):
// OR the REQUIRED_TASK_TYPE bit of each task of the just-selected profile,
// stopping at a trampoline task.
uint32_t requiredTaskTypesFromProfile() {
    uint32_t required = 0;
    for (uint8_t i = 0; i < profileData.tasks; i++) {
        if (profileData.taskType[i] == PROFILE_TASK_TYPE_SHORTBLOWS) {
            required |= REQUIRED_TASK_TYPE_SHORTBLOWS;
        } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_LONGBLOWS) {
            required |= REQUIRED_TASK_TYPE_LONGBLOWS;
        } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_EQUALBLOWS) {
            required |= REQUIRED_TASK_TYPE_EQUALBLOWS;
        } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_INHALATION) {
            required |= REQUIRED_TASK_TYPE_INHALATION;
        } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_INHALATIONPEP) {
            required |= REQUIRED_TASK_TYPE_INHALATIONPEP;
        } else if (profileData.taskType[i] == PROFILE_TASK_TYPE_TRAMPOLINE) {
            required |= REQUIRED_TASK_TYPE_TRAMPOLINE;
            break;
        }
    }
    return required;
}

void resolveGame(Stage& s) {
    s.resolved = true;
    s.index = -1;
    s.total = 0;

    uint32_t required = requiredTaskTypesFromProfile();

    File root = SD_MMC.open(GAMES_ROOT_DIR);
    if (!root || !root.isDirectory()) {
        gGameLog("could not open /games — leaving page for manual selection");
        s.total = 0;
        return;
    }
    File f = root.openNextFile();
    while (f) {
        if (f.isDirectory()) {
            String dir = String(GAMES_ROOT_DIR) + "/" + f.name() + "/";
            File ini = SD_MMC.open(dir + "gameconfig.ini");
            if (ini && !ini.isDirectory()) {
                String err;
                if (gameSupportsTaskTypesAndIsNotHidden(dir, required, &err)) {
                    if (s.numeric) {
                        if (s.value == s.total) s.index = s.total;
                    } else {
                        GameConfig cfg;
                        String ignoreErrors;
                        readGameConfig(dir, &cfg, &ignoreErrors);
                        if (cfg.name == s.spec) s.index = s.total;
                    }
                    s.total++;
                }
            }
        }
        f.close();
        f = root.openNextFile();
    }
    root.close();

    char msg[160];
    if (s.index < 0) {
        snprintf(msg, sizeof(msg),
                 "not resolved (%s, %d selectable games) — leaving page for manual selection",
                 s.numeric ? "index out of range" : "name not found", s.total);
        gGameLog(msg);
    } else {
        snprintf(msg, sizeof(msg), "resolved slot %d of %d", s.index, s.total);
        gGameLog(msg);
    }
}

void tap(Stage& s) {
    int pageCount = s.total < 8 ? (s.total > 0 ? s.total : 8) : 8;
    s.tap = slotCenter(s.index, pageCount);
    simTouchPress(s.tap.x, s.tap.y);
}

void tickStage(Stage& s, uint32_t st, void (*logFn)(const char*)) {
    if (!s.spec || s.phase == Phase::Done || s.phase == Phase::GaveUp) return;

    if (st != s.state) {
        // Not on our page (boot, other screen, or the page already left).
        if (s.phase == Phase::Tapping) {
            simTouchRelease();          // page left mid-press; end it cleanly
            s.phase = Phase::Waiting;
            s.counter = 0;
        } else if (s.phase == Phase::Waiting) {
            s.phase = Phase::Done;
            logFn("page left — selection succeeded");
            if (&s == &g_profile && st == STATE_GAME_RUNNING && g_game.spec &&
                systemConfig.manometerMode) {
                logFn("note: manometerMode skips game selection — game target ignored");
            }
        }
        return;
    }

    // On our page.
    switch (s.phase) {
        case Phase::Idle:
            if (!s.resolved) {
                if (&s == &g_profile) resolveProfile(s);
                else resolveGame(s);
            }
            if (s.index < 0 || (s.total >= 0 && s.index >= s.total)) {
                s.phase = Phase::GaveUp;
                return;
            }
            if (s.index >= 8) {
                s.phase = Phase::GaveUp;
                logFn("not selectable: the page shows at most 8 entries (firmware has no paging) — leaving for manual selection");
                return;
            }
            if (s.settle < kSettleQueries) {
                s.settle++;
                if (s.settle == 1) {
                    logFn("settling: waiting for a full page-draw frame before the first tap");
                }
                break;
            }
            tap(s);
            s.phase = Phase::Tapping;
            s.counter = 0;
            s.attempts = 1;
            char msg[96];
            snprintf(msg, sizeof(msg), "tapping slot %d at (%d, %d)", s.index, s.tap.x, s.tap.y);
            logFn(msg);
            break;

        case Phase::Tapping:
            if (++s.counter >= kHoldQueries) {
                simTouchRelease();
                s.phase = Phase::Waiting;
                s.counter = 0;
            }
            break;

        case Phase::Waiting:
            if (++s.counter >= kWaitQueries) {
                s.counter = 0;
                if (s.attempts >= kMaxAttempts) {
                    simTouchRelease();
                    s.phase = Phase::GaveUp;
                    logFn("gave up after retries — leaving page for manual selection");
                } else {
                    tap(s);
                    s.phase = Phase::Tapping;
                    s.attempts++;
                }
            }
            break;

        default:
            break;
    }
}

} // namespace

void autoNavConfigure(const char* profileSpec, const char* gameSpec) {
    g_profile.spec = profileSpec;
    g_profile.label = "profile";
    g_profile.state = STATE_PROFILE_SELECTION;
    g_profile.numeric = specIsNumeric(profileSpec, &g_profile.value);

    g_game.spec = gameSpec;
    g_game.label = "game";
    g_game.state = STATE_GAME_SELECTION;
    g_game.numeric = specIsNumeric(gameSpec, &g_game.value);

    if (profileSpec) {
        fprintf(stderr, "[autoNav] configured: profile '%s' (%s)\n",
                profileSpec, g_profile.numeric ? "slot index" : "name");
    }
    if (gameSpec) {
        fprintf(stderr, "[autoNav] configured: game '%s' (%s)\n",
                gameSpec, g_game.numeric ? "slot index" : "name");
    }
}

void autoNavTick() {
    if (!g_profile.spec && !g_game.spec) return;
    uint32_t st = getSystemState();
    tickStage(g_profile, st, gProfileLog);
    tickStage(g_game, st, gGameLog);
}
