# Lua Game API

Games on the PEPit are written in **Lua** and run inside an embedded Lua state
([`src/games/gameLua.cpp`](../../src/games/gameLua.cpp)). This folder documents the API that
game scripts can use:

- **[functions.md](functions.md)** — every function registered with `lua_register()`, organized
  by category (sprites, graphics, text, input, Mode 7, road renderer, preferences, …).
- **[variables.md](variables.md)** — the global variables injected into Lua each frame, with a
  table showing which variable is available in which game state (Init / PEP-Inhalation /
  Trampoline).

## How a Lua game works

A game lives in its own folder on the SD card and is described by a `gameconfig.ini`. The host
application reads this file into a [`GameConfig`](../../src/constants.h#L84) which maps each game
phase to a Lua script:

| `GameConfig` field              | Script (relative to game folder) | When it runs                          |
|---------------------------------|----------------------------------|---------------------------------------|
| —                               | `init.lua`                       | Once, when the game starts            |
| `pepShortScriptPath`            | e.g. `short.lua`                 | Each frame of a short-blow PEP task   |
| `pepLongScriptPath`             | e.g. `long.lua`                  | Each frame of a long-blow PEP task    |
| `pepEqualScriptPath`            | e.g. `equal.lua`                 | Each frame of an equal-blow PEP task  |
| `inhalationScriptPath`          | e.g. `inhale.lua`                | Each frame of an inhalation task      |
| `inhalationPepScriptPath`       | e.g. `inhalePep.lua`             | Each frame of an inhalation+PEP task  |
| `trampolineScriptPath`          | e.g. `trampoline.lua`            | Each frame of a trampoline task       |
| `progressionMenuScriptPath`     | e.g. `menu.lua`                  | While the progression menu is shown   |
| `winScreenScriptPath`           | e.g. `win.lua`                   | While the win screen is shown         |
| —                               | `end.lua`                        | Once, when the game ends              |

The `gameconfig.ini` also supports a `[game] strictMode = true` option. In strict mode any Lua
error (or failed sprite load) aborts the program via `checkFailWithMessage()` instead of being
silently logged — useful while developing.

### Lifecycle

```
initGames_lua()          →  sets Init variables, runs init.lua
        │
        ▼   (repeats every frame while the task is active)
updateBlowData()         →  sets PEP/Inhalation variables, runs the task's draw script
updateJumpData()         →  sets Trampoline variables, runs the trampoline draw script
        │
        ▼   (when the exercise finishes)
displayWinScreen_lua()   →  sets Ms, runs winScreenScriptPath (loops until CloseWinScreen())
endGame_lua()            →  runs end.lua, then collects Lua garbage
```

- **`init.lua`** runs once at start. It only receives the *Init* variables (`Ms`,
  `LeftHandedMode`) — see [variables.md](variables.md). Use it to load sprites and set up state.
- **Per-frame draw scripts** run every frame with the latest therapy/jump data already injected.
  They should be lightweight; heavy one-time work belongs in `init.lua`.
- **`end.lua`** runs once at the end. Use it to free sprites (`FreeSprite`) and clean up.

### The draw target

All drawing functions render to a **current draw target**, which is either the main framebuffer
(the screen) or one of the sprite buffers. Switch it with
[`SetDrawTargetSprite(handle)`](functions.md#setdrawtargetspritehandle) and
[`SetDrawTargetFramebuffer()`](functions.md#setdrawtargetframebuffer). It starts as the
framebuffer.

### Sprites

Sprites are loaded from BMP files into a fixed pool of **200** slots and referenced by an integer
handle. See [Sprites — Loading & Freeing](functions.md#sprites--loading--freeing) and
[Sprites — Drawing](functions.md#sprites--drawing).

### Standard libraries

The `table`, `string` and `math` Lua libraries are available, plus all the custom functions in
[functions.md](functions.md).

## Example

```lua
-- init.lua  (runs once)
local player = LoadSprite("player.bmp", 0, -1)   -- handle to a static sprite
local bg     = LoadSprite("bg.bmp")

-- short.lua  (runs every frame of a short-blow task)
Cls()
DrawSprite(bg, 0, 0)

-- Animate the player based on how hard the patient is blowing.
local progress = Constrain(Pressure / 100, 0, 1)
DrawSprite(player, 240 - progress * 100, 200)

-- Show a countdown using the injected variables.
DrawString("Cycle " .. CycleNumber .. "/" .. TotalCycleNumber, 10, 10)
```
