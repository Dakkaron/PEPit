---
name: Create PEPit Lua game
description: Create or modify a Lua game under SDCardContent/games or WIPSDCardContent/games - file layout, gameconfig.ini, firmware-injected globals, blow semantics, Prefs persistence, sprites and emulator testing.
---

## Rules

- Game `.lua` files ship comment-free (they are parsed on the device; see repository AGENTS.md). No comments unless absolutely essential, then the shortest possible.
- Study an existing game first: `SDCardContent/games/soccer` (blow-timing games) and `SDCardContent/games/balloon` (steering + upgrade shop) are the canonical patterns; `WIPSDCardContent/games/monster2` shows a large multi-file game with data scripts and a dex progression menu.
- Verify every new or touched `.lua` file with `luac -p`.
- Test in the emulator (`PEPitCompat`) before declaring done; screenshots via PEPitMCP.

## Game anatomy

`<games-dir>/<name>/` contains:

- `gameconfig.ini` - keys seen by `gameLua.cpp`: `name`, `template = lua`, `prefsNamespace`, `strictMode`, `pepShortScriptPath`, `pepLongScriptPath`, `pepEqualScriptPath`, `trampolineScriptPath`, `inhalationScriptPath`, `inhalationPepScriptPath`, `progressionMenuScriptPath`, `winScreenScriptPath`. Point all task-type script paths at `mainGame.lua` unless a phase needs different logic.
- `init.lua` - runs once at game start (`initGames_lua`). Load sprites, define constants and helper globals here.
- `mainGame.lua` - runs every frame during every task phase. All mutable state lives in globals it owns.
- `end.lua` - runs once at session end, uncached. Persist scores with `PrefsSet*` and `FreeSprite` everything still loaded.
- `progressionMenu.lua` - runs every frame while the progression menu is open. Only `init.lua` globals plus `Ms` exist here; persist shop/dex state through Prefs.
- `gfx/` - 16-bit RGB565 BMP sprites. `LoadSprite` / `LoadAnimSprite` take a transparent-mask colour (soccer uses pink `0xF81F`, monster2 art uses black `0x0000`, the native default).

## Blow semantics (the core of every game)

`updateBlowData()` injects globals every frame during pep short/long/equal and inhalation phases. Key facts:

- `NewRepetition` is a one-frame pulse that fires at blow **END** (when the blow reaches its target duration), not at blow start. Games act on it (soccer strikes the ball, balloon stokes the flame).
- `CurrentRepetition` resets per task, so `NewRepetition` is not a reliable task boundary; compare against stored previous values or use `IsNewTask`/`CumulatedTaskNumber`.
- Trampoline tasks use `updateJumpData()` globals instead (`CurrentRepetition` = jump count, `MsLeft`, `CurrentlyJumping`).

See `references/lua-api.md` for the full injected-globals list and every registered Lua function, extracted from `T-HMI-PEPmonitor/src/games/gameLua.cpp`.

## Persistence

- Each game has its own Prefs namespace (`prefsNamespace` in gameconfig.ini, e.g. `game-soccer`); call `PrefsSetInt`/`PrefsGetInt` etc. and they land in the game's own `prefs_game-<name>.dat`.
- `PrefsSetInt` raises a Lua error on a nil/NaN value - never persist a global that may not exist.
- `PrefsSetBytes`/`PrefsGetBytes` store blobs for bigger payloads (monster2 dex state).

## Screen and input

320x240 display, RGB565 colors (`rgb()` helper: `((r>>3)<<11)|((g>>2)<<5)|(b>>3)`). Support both input paths like soccer/balloon: joystick (`GetJoystickXY`, `GetJoystickButton`) and touch (`GetTouchX/Y`, `IsTouchInZone` for buttons).

## Workflow

1. Read `references/lua-api.md`.
2. Copy the structure of soccer (simple) or balloon (with shop) as a starting point.
3. Write comment-free Lua; `luac -p` every file.
4. Run the game in the PEPitCompat emulator (profile select -> game select) and check gameplay, persistence across sessions, and clean `end.lua` teardown.
