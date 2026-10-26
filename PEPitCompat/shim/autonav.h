/*
 * PEPitCompat — Auto-Nav (menu fast-path)
 *
 * Lets `pepit_compat --profile <name|id> --game <name|id>` drop directly
 * into a game. The whole menu flow (profile selection -> game selection)
 * runs inside the original, blocking setup(); the only input the firmware
 * consumes there is the touch controller (XPT2046::pressed/X/Y via
 * checkSelectionPageSelection, level-triggered, polled every loop).
 *
 * This module therefore never touches the original source: it feeds the
 * same simulated-touch channel the MCP control file uses, from inside the
 * emulator. When the firmware's selection page appears (detected via
 * getSystemState), the shim presses the exact slot the user asked for.
 *
 * Geometry mirrors checkSelectionPageSelection() and list resolution
 * mirrors getNumberOfProfiles()/getGamePath() in the original source —
 * see autonav.cpp for the mirrored formulas.
 */

#ifndef PEPIT_AUTONAV_H
#define PEPIT_AUTONAV_H

/**
 * Register the auto-nav targets. Either argument may be nullptr to disable
 * that stage (manual menu interaction, exactly as before).
 *
 * Specs are resolved lazily when the respective page first appears:
 *   - profile: by `name` in /profiles.ini, or as a 0-based slot index
 *   - game:    by `name` in gameconfig.ini, or as a 0-based slot index
 * (Slot indexes above 7 are rejected: the selection page shows at most
 * 8 entries — paging is not implemented in the firmware.)
 *
 * The pointers must stay valid for the process lifetime (argv strings do).
 */
void autoNavConfigure(const char* profileSpec, const char* gameSpec);

/**
 * Called from the touch shim once per touch query (i.e. every time the
 * original code asks for pressed/X/Y — including inside the blocking
 * selection loops of setup()). Advances the tap state machine.
 */
void autoNavTick();

#endif // PEPIT_AUTONAV_H
