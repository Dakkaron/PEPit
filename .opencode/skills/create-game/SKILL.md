---
name: Create PEPit Lua game
description: Create or modify a PEPit Lua game in SDCardContent/games or WIPSDCardContent/games: layout, gameconfig.ini, firmware globals, blow semantics, Prefs, sprites, emulator testing.
---

## Rules

- Ship `.lua` comment-free (parsed on-device; see AGENTS.md).
- Never set `isHidden` in `gameconfig.ini` (defaults false; only joysticktest sets it - a self-test, not a game).
- `luac -p` every touched file; before declaring done, test in the PEPitCompat emulator.
- Canonical patterns: soccer (blow timing), balloon (steering + shop), monster2 (multi-file, data scripts, dex menu).

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
- `CurrentRepetition` resets per task, so it is not a task boundary; use `IsNewTask`/`CumulatedTaskNumber`.
- Trampoline tasks get `updateJumpData` globals (`CurrentRepetition` = jumps, `MsLeft`, `CurrentlyJumping`).
- Full globals + registered functions: `references/lua-api.md`.

## Prefs

Scoped to `prefsNamespace` (`prefs_game-<name>.dat`). `PrefsSetInt`/`PrefsSetNumber` raise on nil (`luaL_checknumber`) - persist only globals guaranteed to exist. `PrefsGet/SetBytes` for blobs.

## Screen

320x240 RGB565; `rgb(r,g,b) = ((r>>3)<<11)|((g>>2)<<5)|(b>>3)`. Support joystick (`GetJoystickXY`, `GetJoystickButton`) and touch (`GetTouchX/Y`, `IsTouchInZone`).

## Workflow

1. Read `references/lua-api.md`. 2. Base on soccer/balloon. 3. Write comment-free Lua, `luac -p` each file. 4. Test in the emulator (profile select -> game select).
