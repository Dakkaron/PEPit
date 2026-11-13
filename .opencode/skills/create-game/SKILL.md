---
name: Create PEPit Lua game
description: Create or modify a PEPit Lua game in SDCardContent/games or WIPSDCardContent/games: layout, gameconfig.ini, firmware globals, blow semantics, Prefs, sprites, emulator testing.
---

## Rules

- Ship `.lua` comment-free (parsed on-device; see AGENTS.md).
- Never set `isHidden` in `gameconfig.ini` (defaults false; only joysticktest sets it - a self-test, not a game).
- `luac -p` every touched file; before declaring done, test in the PEPitCompat emulator.
- Canonical patterns: soccer (blow timing), balloon (steering + shop), monster2 (multi-file, data scripts, dex menu); design history in `TASK_INFO/`. Do not edit this skill unless asked.

## Layout

`<games-dir>/<name>/`:

- `gameconfig.ini`: `name`, `template = lua`, `prefsNamespace`, `strictMode`; script paths `pepShortScriptPath` `pepLongScriptPath` `pepEqualScriptPath` `trampolineScriptPath` `inhalationScriptPath` `inhalationPepScriptPath` `progressionMenuScriptPath` `winScreenScriptPath` - usually all point to `mainGame.lua`.
- `init.lua`: runs once at game start - sprites, constants, helpers.
- `mainGame.lua`: runs every frame of every task phase; mutable state lives in its globals.
- `end.lua`: runs once at session end, loaded uncached - persist scores, FreeSprite everything.
- `progressionMenu.lua`: runs every frame while open; only `init.lua` globals + `Ms` exist; persist via Prefs.
- `gfx/`: RGB565 BMP sprites; `LoadSprite`/`LoadAnimSprite` take a transparent mask colour (pink `0xF81F`, or black `0x0000`, native default).

## Blows

- `updateBlowData` injects globals every frame of pep short/long/equal and inhalation. `NewRepetition` is a one-frame pulse at blow END - act on it (strike the ball, stoke the flame).
- A blow ends when pressure drops below `minPressure`; firmware grades it: lasted `> TargetDurationMs` -> success (`blowCount++`, `LastBlowStatus=1`), shorter -> fail (`LastBlowStatus=2`). There is no upper bound: blowing past `TargetDurationMs` is never penalized, so game logic must not punish long blows either.
- `TargetDurationMs` is also available as a lua global (profile `taskTime`; `taskTime2` during the PEP phase of inhalationPep). Scale time-based game progress by it (rate = distance / TargetDurationMs) so finishing at the target time yields the same game state regardless of how long the target time is.
- pepLong and pepEqual gameplay are identical: "equal" means "no short/long combination" (every blow shares one task spec), NOT "keep each blow equal". Treat type 3 exactly like type 2. The types exist separately so games that gate gameplay on short-vs-long (e.g. monstercatcher) can react correctly to a profile whose blows all share one spec.
- pepShort (`TaskType=1`): the user must blow as hard as they can, so overshooting the game's goal is never a penalty; and don't draw "stay under / don't overshoot" UI (e.g. an outer dim tolerance circle) for type 1.
- `CurrentRepetition` resets per task, so it is not a task boundary; use `IsNewTask`/`CumulatedTaskNumber`.
- Trampoline tasks get `updateJumpData` globals (`CurrentRepetition` = jumps, `MsLeft`, `CurrentlyJumping`).
- Full globals + registered functions: `references/lua-api.md`.

## Prefs

Scoped to `prefsNamespace` (`prefs_game-<name>.dat`). `PrefsSetInt`/`PrefsSetNumber` raise on nil (`luaL_checknumber`) - persist only globals guaranteed to exist. `PrefsGet/SetBytes` for blobs.

## Screen

320x240 RGB565; `rgb(r,g,b) = ((r>>3)<<11)|((g>>2)<<5)|(b>>3)`. Support joystick (`GetJoystickXY`, `GetJoystickButton`) and touch (`GetTouchX/Y`, `IsTouchInZone`).

The font covers German umlauts (ä ö ü), ß and the € sign - use them directly in `DrawString`/`Print`/`Println`, never transliterate (write `Meisterstück!`, not `Meisterstueck!`). `DrawString` splits on `\n` into lines; the engine substitutes `€` with the `¶` glyph internally because € is far down the unicode table (`gameLua.cpp`).

### Reserved areas (system overlays, drawn on top of your frame every tick)

Never draw game content in these rectangles (`physioProtocolHandler.cpp`, `gfxHandler.cpp::drawSystemStats`; re-verify after firmware changes):

- systemui top-left: battery 32x20 @(0,0), wifi 28x20 @(34,0), 8x16 FPS + battery-volts/clock text at x=64,1 and 64,11 - clear x 0..104, y 0..28. (Low-battery warning icon 32x20 @(144,110) appears mid-screen when the battery is low.)
- Pressure bar @(1,190) 180x10 and blow-duration bar @(1,215) 180x10 (`PRESSURE_BAR_X/Y/W/H` in `constants.h`; trampoline replaces the pressure bar with a time bar @(1,190)) - clear x 0..182, y 189..226.
- Semi-reserved strip above the bars, x 0..188, y 175..214: system draws bar-% labels @(1,179)/(1,204), blow-count @(41,176) and task/cycle @(81,176) in 16x32 font, jump-count @(21,176), inhale/exhale icon 38x32 @(149,158). Background graphics allowed; no game elements or text.
- Trampoline countdown (24x48 digits) from @(100,5) - clear top-center on trampoline tasks.
- Fertig/Weiter button `(240,210,80,30)` (text is "Weiter" except on the final task/cycle) bottom-right on inhalation/PEP tasks (system also handles its touch zone) - clear x 240..319, y 210..239.

## Workflow

1. Read `references/lua-api.md`. 2. Base on soccer/balloon. 3. Write comment-free Lua, `luac -p` each file. 4. Test in the emulator (profile select -> game select).

WIP games: `PEPitCompat/build/pepit_compat --data-dir <dir> --wip-games` reads `/games` straight from `WIPSDCardContent/games` (prefs/profiles/system files stay in the data dir); `--wip` swaps the whole SD root to `WIPSDCardContent` (profiles.ini + prefs_*.dat seeded from the data dir). The MCP `pepit_session_start`/`pepit_policy_run` do not pass these flags - for MCP-driven tests sync the game dir into `<data-dir>/games/` instead.
