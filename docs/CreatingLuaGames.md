# Creating a Lua Game (Case Study: Soccer Penalty)

This document walks through the process of making a Lua game for the PEPit,
using the development of **Soccer Penalty** (`SDCardContent/games/soccer/`) as
the concrete example. It complements the API reference in [lua/](lua/README.md)
(functions + injected variables) — read that first if you have not seen it.

The overall flow:

```
1. Design the game around the therapy cycle
2. Scaffold the game folder (gameconfig.ini + scripts + logo)
3. Write the scripts (init.lua, per-frame script, end.lua, …)
4. Develop against the PEPitCompat emulator (GUI or headless + MCP)
5. Test (load → rulebot → frames → verifier → visual audit → lua sim)
6. Ship (SD card content, commits)
```

---

## 1. Design the game around the therapy cycle

A PEPit game is a **wrapper around physiotherapy**: the patient's therapy
action (blow, inhalation, jump) *is* the game input. The design question is
therefore "what fun thing happens when the patient does a good therapy rep?"

- **PEP/inhalation tasks**: each successful blow is one game action.
- **Trampoline tasks**: each successful jump is one game action.

A game is selected *after* the profile: the firmware shows a game selection
page listing all games whose script coverage matches the profile's task types
(see [Step 2](#2-scaffold-the-game-folder)), then plays the profile's tasks
in-game for `cycles` times, showing the (optional) progression menu and the
win screen around it.

### Soccer Penalty — design summary

- **Each blow is one penalty kick.** Aim with the joystick (both axes) or by
  dragging the touch anywhere in the goal zone; the aim eases toward the input
  with a ~120 ms time constant and is clamped inside the goal mouth.
- **Shoot**: the ball is kicked the moment a blow **finishes** — the
  firmware's `NewRepetition` one-frame pulse (see below). A failed blow
  (released early) pulses nothing, so no kick: the rep simply waits to be
  blown again.
- **Goalkeeper (NPC)**: dives to where the player was aiming **500 ms before
  the blow started** — sampled from an aim-history ring. The player wins the
  mind game by moving the aim after the keeper "commits".
- **Outcome**: `GOAL!` / `SAVED!` banner, score accumulates, a TV-style
  scoreboard bar shows one box per rep of the current task.
- **Persistence**: goals/saves are saved to the game's prefs namespace in
  `end.lua`.

### The firmware's per-rep signals — semantics you must get right

These come from the firmware ([`gameLua.cpp` `updateBlowData()`](../T-HMI-PEPmonitor/src/games/gameLua.cpp)
and [`physioProtocolHandler.cpp`](../T-HMI-PEPmonitor/src/physioProtocolHandler.cpp))
and were verified while building soccer:

| Variable | Meaning (verified in firmware) |
|---|---|
| `CurrentlyBlowing` | `true` from the frame pressure rises above `minPressure` until it falls below it again. |
| `BlowStartMs` / `BlowEndMs` | timestamps of that rising / falling edge. |
| `NewRepetition` | **one-frame pulse on the frame a *successful* blow *ended*** (pressure fell below `minPressure` and the blow lasted ≥ `TargetDurationMs`, so `blowCount` was incremented). A failed blow pulses nothing. |
| `CurrentRepetition` | `blowCount` — completed blows **in the current task** (0 before the first). |
| `Repetitions` | `totalBlowCount` for the current task. |

Consequences for game design (all hit us during soccer development):

1. **`NewRepetition` pulses at blow *end*, not blow *start*.** The first
   soccer version kicked the ball on the `CurrentlyBlowing` rising edge (blow
   start); the user-reported fix moved the kick to `NewRepetition`, which
   feels right: the patient aims for the whole blow and the shot fires on
   release. *(Note: [lua/variables.md](lua/variables.md) currently describes
   `NewRepetition` as "a new repetition just started" — this doc and the
   soccer code treat it as blow-end; one of the two needs correcting.)*
2. **The firmware resets `blowCount` per task** (and a task switch produces
   no spurious pulse). Therefore `NewRepetition` is **not** a reliable
   task-boundary signal. Soccer detects task changes by watching
   `CurrentRepetition`/`Repetitions` (rep counter wrapped down, or the total
   changed) and clears its per-task scoreboard log then.
3. **Tasks end** when `blowCount >= totalBlowCount` — for PEP tasks after a
   2 s quiet timeout, for inhalation tasks on a "done" touch zone or an
   automatic inactivity timeout. Between tasks the firmware shows its own
   device-confirmation screen (valve photo + OK button); **the game never
   sees it** — no game code needed.
4. **Inhalation tasks are unbounded** (minimum rep count, continue until the
   player stops). Anything you draw "per rep" must not assume a fixed bound —
   soccer's box-per-rep scoreboard row is planned to be gated to the bounded
   PEP task types (`TaskType` 1/2/3), keeping only the numeric score for
   inhalation (`TaskType` 5/6). *(planned, not yet implemented)*

For trampoline tasks the injected jump variables instead are:
`CurrentlyJumping`, `CurrentRepetition` (= `jumpCount`),
`NewRepetition` (one-frame pulse per successful jump),
`CurrentBonusRepetition` / `NewBonusRepetition` (jump above target height),
`MsLeft` (task time left) and `LastJumpMs` (ms of the last successful jump) —
there is currently **no** miss signal in Lua. The miner's
`trampoline.lua` is the reference for a jump-driven sub-game.

---

## 2. Scaffold the game folder

A game is a folder under `/games/` on the SD card. In the repo the canonical
location is `SDCardContent/games/<name>/`; `PEPitCompat/data/games` is a
**symlink to it**, so the emulator (whose default data dir is
`PEPitCompat/data/`) picks up your edits directly.

Soccer's file set (the same shape as the plane game):

```
SDCardContent/games/soccer/
├── gameconfig.ini       # the contract with the firmware
├── init.lua             # runs ONCE when the game starts
├── mainGame.lua         # runs EVERY frame in all five blow phases
├── progressionMenu.lua  # runs every frame while the progression menu is open
├── end.lua              # runs ONCE when the game ends
└── logo.bmp             # selection-page icon, ≤ 40×40 (soccer: 32×18)
```

### `gameconfig.ini` — the contract

```ini
[game]
name = Soccer Penalty
template = lua
prefsNamespace = game-soccer
strictMode = true
pepShortScriptPath = mainGame.lua
pepLongScriptPath = mainGame.lua
pepEqualScriptPath = mainGame.lua
inhalationPepScriptPath = mainGame.lua
inhalationScriptPath = mainGame.lua
progressionMenuScriptPath = progressionMenu.lua
```

The keys the firmware reads ([`sdHandler.cpp` `readGameConfig()`](../T-HMI-PEPmonitor/src/hardware/sdHandler.cpp)):

| Key | Meaning |
|---|---|
| `name` | Display name on the game selection page. |
| `template` | `lua` selects the Lua game engine. (The legacy C games use `monster` / `race`.) |
| `prefsNamespace` | Namespace for the `Prefs*` functions (persistent key-value storage). |
| `pepShortScriptPath` / `pepLongScriptPath` / `pepEqualScriptPath` / `inhalationScriptPath` / `inhalationPepScriptPath` | One script per blow-phase task type, run every frame while that task is active. Pointing several phases at the **same file** is the standard pattern (plane + soccer both use `mainGame.lua` for all five). |
| `trampolineScriptPath` | Script for trampoline tasks (soccer does not have one yet; miner does). |
| `progressionMenuScriptPath` | Shown via the "Progression" entry on the profile page; loops until `CloseProgressionMenu()`. |
| `winScreenScriptPath` | Optional — run when the exercise finishes; loops until `CloseWinScreen()`. Without it the firmware shows its stock win image (soccer relies on that). |
| `isHidden` | `true` removes the game from the selection page (there is a hidden self-test game that uses this). |
| `strictMode` | Read by the Lua engine: any Lua error / failed sprite load stops the device on a frozen `FEHLER: …` screen instead of being silently logged. Keep it on while developing. |

**Visibility rule:** a game appears on a profile's game page only if it
provides a script (that exists on the SD card) for **every** task type the
profile schedules. This is how the profile's task types decide the game list:
soccer covers the five blow phases, so it appears on PEP/inhalation profiles
but *not* on the Trampolin profile — adding
`trampolineScriptPath = trampolineGame.lua` would make it appear there too.

### `logo.bmp`

The game selection page (up to 8 tiles: 4×2, with pager arrows) draws
`<game>/logo.bmp` centered in each tile. **Keep it at 40×40 or smaller.** (The shipped logos are 32×18, 27×31 and
33×36 — all within the limit.)

The firmware BMP loader ([`gfxHandler.cpp` `loadBmpAnim()`](../T-HMI-PEPmonitor/src/hardware/gfxHandler.cpp))
accepts exactly these combinations of `planes`/`bitcount`/`compression`:
**16-bit + `BI_BITFIELDS` (3)**, 24-bit + `BI_RGB` (0), or **32-bit +
`BI_BITFIELDS`**. Anything else prints "BMP format not recognized" to the
serial log and the logo **silently fails to load** (the game still runs; the
tile just shows no icon). For the 16-bit path the loader additionally reads
one byte at absolute file offset `0x43` as an alpha flag
(`0x80` → alpha/mask handling on) — in the working logos that byte is the
high byte of the `alphaMask` field of the **124-byte BITMAPV5HEADER**.

Verified headers of the shipped logos:

| logo | size | header | bitcount | compression | alpha byte @ `0x43` |
|---|---|---|---|---|---|
| soccer | 32×18 | 124 (V5) | 16 | 3 (`BI_BITFIELDS`) | `0x80` |
| plane | 32×18 | 124 (V5) | 16 | 3 | `0x80` |
| farm | 33×36 | 124 (V5) | 16 | 3 | `0x80` |
| miner | 27×31 | 124 (V5) | 32 | 3 | `0x00` |

**Gotcha from soccer:** the first logo was generated as 24-bit `BI_RGB` and
did not end up on screen. The fix that worked: regenerate the logo to
**byte-match an already-working logo** (the plane's — the soccer and plane
logos are identical in size and header: 16-bit 565, `BI_BITFIELDS`, V5
header, alpha flag `0x80`). If your logo does not show up, diff the file
header against `games/plane/logo.bmp` and check the serial log for
"BMP format not recognized" / "File not found".

---

## 3. Write the scripts

The host runs each script as its own Lua chunk in one shared Lua state
(5.4.7, with `table`/`string`/`math`). Consequences:

- **State that must survive between scripts must be *global*** (no `local`):
  `player = LoadSprite(...)` in `init.lua` is visible to the per-frame script.
  `local` variables vanish when the chunk finishes.
- Each script re-runs (per-frame ones) or runs once (`init.lua`, `end.lua`);
  **all function definitions and one-time setup belong in `init.lua`**, the
  per-frame script only does per-frame work.
- Heavy work (sprite loading, state setup) in `init.lua`; the per-frame script
  must be lightweight — it runs every frame.

### `init.lua` (soccer, 222 lines — pattern summary)

Runs once. Receives only the *Init* variables: `Ms`, `LeftHandedMode`
([lua/variables.md](lua/variables.md)). Soccer's `init.lua` contains:

1. **Color helpers + palette** — 565 colors via a local `rgb(r,g,b)`
   (bit-shifted 16-bit values). Note the *deliberate* choices: the goal frame
   and net are pale blue-gray, **not** pure white, so the ball is the only
   solid-white blob on screen — this makes the game machine-readable by the
   test infrastructure (pixel census by color), and generally makes the scene
   easier for any automatic observer.
2. **Geometry constants** — goal mouth, penalty spot, keeper home position.
   Screen is 320×240; the firmware chrome owns **y 0..19** (status strip:
   battery/wifi/clock + rep counter) and **y ≥ 190** (bottom bars) — draw
   game content between.
3. **All mutable state** as globals (aim, ball, keeper, animation state
   machine, score, per-task `KickLog`).
4. **Functions**: `UpdateAim` (input → eased aim + 10 Hz-thinned 120-sample
   aim-history ring), `SampleKeeperTarget`, `DrawKickBox`/`DrawScoreboard`.

### The per-frame script (`mainGame.lua`, runs in all five blow phases)

Structure (soccer):

1. **Task-boundary detection** — compare `CurrentRepetition`/`Repetitions`
   with last frame's values; on change, clear the per-task `KickLog`.
2. **One-shot events** — `if NewRepetition then …` starts the kick (snapshots
   aim → shot target, samples the keeper target, resets timers). The kick may
   interrupt any animation in flight; the flight always restarts from a fixed
   origin, and each outcome is logged exactly once (when its flight resolves),
   so re-entries never double-count.
3. **Per-frame input handling** — `UpdateAim(Ms, MsDelta)`; aim tracks the
   input at all times (the shot is only *decided* on blow end, so re-aiming
   mid-blow is legal and used by players/strategies).
4. **Animation state machine** — `idle → shooting → result → reset → idle`,
   all timed with the injected `Ms`/`MsDelta` (frame-rate independent):
   exponential easing `1 - exp(-MsDelta/τ)`, `lerp` with a normalized `t`,
   smoothstep for the keeper dive.
5. **Full-scene redraw** — pitch, goal, keeper, ball, crosshair, result
   banner, scoreboard. The game draws the whole scene into the framebuffer
   every frame (the firmware double-buffers and pushes it to the LCD).

Input APIs used: `GetJoystickXY()` (normalized axes; soccer applies a
0.12 dead zone), `IsTouchInZone(x,y,w,h)` / `GetTouchX()` / `GetTouchY()`
(drag-aim), `Constrain(val,min,max)`. Full list in
[lua/functions.md](lua/functions.md).

Persistence: `PrefsSetInt/GetInt/SetString/...` are namespaced by the
game's `prefsNamespace` (soccer: `game-soccer`) and survive reboots. Soccer
writes `goals`/`saves` in `end.lua`.

### `progressionMenu.lua` and `end.lua`

- `progressionMenu.lua` runs every frame while the menu is open (`Ms` is
  injected, blow variables are **not**). Soccer shows the score recap and a
  "Fertig" button (`IsTouchInZone` → `CloseProgressionMenu()`), the same
  pattern as the plane game.
- `end.lua` runs once when the game ends. Just save state here; **do not**
  free sprites or clean up — the whole Lua engine is torn down afterwards.

---

## 4. Develop against the PEPitCompat emulator

[PEPitCompat](../PEPitCompat/README.md) runs the **unmodified ESP32 firmware**
on Linux (SDL2 shims for display/touch/sensors, POSIX for the SD card). This
is the development target: what you see in the emulator is what runs on the
device. See [PEPitCompat/README.md](../PEPitCompat/README.md) for build steps
and [PEPitCompat/API_MAP.md](../PEPitCompat/API_MAP.md) for the shim mapping.

| Emulator input | Emulates |
|---|---|
| key `A` (configurable) | pressure at ~50 % of task target (not blowing → 0) |
| key `S` (configurable) | pressure at ~100 % of task target |
| arrow keys / WASD + Space/Enter | I2C joystick (axes + button) |
| mouse | touch screen |

Three ways to drive it, in increasing automation:

1. **GUI session** — `./build/pepit_compat --scale 2`
   (add `--data-dir` if you want a different SD card root). Manually blow
   with A/S and steer with the mouse/keys. Fine for the first smoke test;
   the default data dir `PEPitCompat/data/` has a `games` symlink into
   `SDCardContent/games/`, so your game is already there.
2. **Direct game start (autonav)** —
   `./build/pepit_compat --profile "PEP S" --game "Soccer Penalty"`. The shim
   auto-navigates profile → game → rotate screens for you; you still play.
   Use a profile whose tasks exercise the game (e.g. `PEP S` in
   `PEPitCompat/data/profiles.ini`: 10× pepLong 5 s 110/200 + pepShort,
   3 cycles).
3. **Headless + MCP** — the [PEPitMCP](../PEPitMCP/README.md) server spawns
   `pepit_compat` with `SDL_VIDEODRIVER=dummy` and exposes tools
   (`pepit_session_start(profile, game)`, `pepit_screenshot`,
   `pepit_touch_start/move/stop`, `pepit_joystick_control`,
   `pepit_set_pressure`). The agent-facing loop is
   **screenshot → act → screenshot → verify**. Fully scriptable, so it works
   unattended and in CI.

The policy runner (`PEPitMCP/policy/runner.py`) sits on top of the MCP layer
and **plays the game for you** (see Step 5):

```bash
python3 -m PEPitMCP.policy.runner --game soccer --source rulebot \
    --data-dir /tmp/opencode/pepit_data_lua --profile 0 --duration 180
```

Per-game knowledge lives in `PEPitMCP/policy/games/<name>.py` — a spec with
(frame) **extractors** (pixel census → structured state), a **rulebot**
strategy, and (optionally) LLM decision questions. Adding a new game means
adding a spec there (soccer's spec was shipped together with the game).

A fourth, much cheaper development tool: a **headless Lua sim** — a scratch
`lua` script that stubs the firmware globals (`Ms`, `NewRepetition`, …) and
the draw calls, then `dofile`s your *real* `init.lua`/`mainGame.lua` and
asserts on the state machine (soccer's `sb_sim.lua`: 19/19 checks for the
kick-trigger change, run in subprocesses). This tests game logic with zero
emulator cost and is ideal for state-machine regressions.

---

## 5. Test

The soccer test plan (now the standard ladder for a new game; full details in
the development notes):

| # | Test | What it proves |
|---|---|---|
| T1 | **Loads and is stable** — direct start `--profile … --game …`; watch for: logo loads, scene renders, session survives > 20 min, `QUIT` exits cleanly. | The game boots inside the firmware, draws, does not crash (incl. the game-selection page with the new logo). |
| T2 | **Full rulebot run** — policy runner, fixed duration. Check: firmware-confirmed blow successes, HUD ground truth (score, per-task results), task-boundary crossings, device-confirmation screens auto-tapped, clean exit (`phase: timeout` or `win`, `error: null`). | The game is *playable end-to-end* through a real profile; the rulebot is your automated player. |
| T3 | **Frame capture + contact sheet** — the runner saves ~1 frame/s + event frames with JSONL metadata under `/tmp/opencode/pepit_frames/<run>/`. | Visual audit trail of what actually happened. |
| T4 | **In-loop verifier** — a fast VLM check (`has_game_ui` / `is_change_screen`) during the run; mismatches are logged, never abort. | Sanity: the screen shows a game while playing. |
| T5 | **Post-run visual audit** — slow VLM (or human) reads key frames (idle / goal / save / banner) and checks consistency with the JSONL. | The *content* is right, not just present. |
| T6 | **LLM-source run** — the decision model plays instead of the rulebot. | Confirms the game is *legible* to a vision model (colors/contrast/layout choices pay off here). |
| T7 | **MCP plumbing** — `pepit_session_start(profile, game)` + `pepit_policy_run` (the agent-facing by-name API) end to end for the new game. | The MCP path works, not just the CLI runner. |
| T8 | **Headless Lua sim** (see Step 4) — pure logic regression. | Cheap re-run after any state-machine change. |

(T-numbering follows the soccer test plan.)

Soccer's T2 result for reference: 16/16 blow successes, 10/10 goals on the
pepLong task, exactly one save in the whole run (the rulebot's flick never
registered — a real, explainable loss), 0 verifier mismatches.

Design-for-testability takeaway: soccer's palette is deliberately
machine-readable (see Step 3) — keep your game's key objects as unique,
high-contrast blobs and your state legible from a screenshot.

---

## 6. Ship

- The game lives in `SDCardContent/games/<name>/` (canonical). Get it onto a
  real device via the SD card (`/games/`), or over USB with the file manager
  in `scripts/usb/` (SD file manager over the CDC bridge).
- Commit structure used for soccer — keep game / framework / docs separate:
  1. **game** — `SDCardContent/games/soccer/**`
  2. **framework** — `PEPitMCP/policy/**` (new game spec) + any
     `PEPitCompat/shim/**` or firmware changes the game needed
  3. **docs** — development notes / test results
  Deliberately exclude local runtime data (`PEPitCompat/data/prefs*.dat`,
  untracked test artifacts).
- `PEPitCompat/data/` (repo) holds a *committed* test data dir (profiles,
  system config, games symlink) so `cmake`-build-and-run works out of the box;
  live agent testing uses a scratch copy (e.g. `/tmp/opencode/pepit_data_lua`)
  so prefs/logs do not dirty the repo.

---

## Gotchas (learned the hard way on soccer)

1. **`NewRepetition` fires at blow *end*.** If you key gameplay on
   "start of rep", use `CurrentlyBlowing`'s rising edge *and* know that a
   failed blow gives you neither — decide deliberately which edge your game
   wants (soccer: end).
2. **The keeper sample was a full blow in the past.** The kick fires at blow
   end, but the keeper commits to the aim *500 ms before blow start* — so the
   aim-history ring must span a whole blow (up to ~5 s). Soccer's original
   1 s ring could not see it; 10 Hz × 120 samples (12 s) can.
3. **`blowCount` resets per task** → detect task boundaries via
   `CurrentRepetition`/`Repetitions`, not via `NewRepetition`.
4. **Logo BMP format is fussy** (16-bit 565 `BI_BITFIELDS` + V5 header;
   everything else fails silently) — byte-match a working logo.
5. **Unbounded inhalation tasks** — don't grow per-rep UI without a cap;
   gate per-rep widgets on the task type (`TaskType` 1/2/3 = bounded PEP,
   5/6 = unbounded inhalation, 4 = trampoline).
6. **The firmware status strip owns y 0..19** (and the bottom bar y ≥ 190) —
   keep your HUD between them.
7. **The shared firmware log** (emulator serial → file) accumulates across
   sessions; test tooling that parses it must scope to its own session
   (runner's session-scoped `LogFollower`; check `PRE_SETUP_MARKER` in
   `PEPitCompat/shim/main.cpp` if blow counts ever look phantom).
8. **Menu-sprite SEGV guard** — the game page once segfaulted on freshly
   added logos (sprite array allocation); the firmware fix self-allocates,
   the emulator keeps a settle gate as defense-in-depth. A crash *at the game
   page* with a new game usually points here.
9. **Strict mode is a double-edged sword** — great for development (errors
   stop the device loudly on a `FEHLER` screen instead of misbehaving
   silently), but a shipped game with *any* Lua error freezes the device on
   that screen until someone resets it. Decide per game whether
   `strictMode = true` ships (soccer currently does).

---

## References

| Doc | What |
|---|---|
| [lua/README.md](lua/README.md) | Game lifecycle, script phases, draw target, state-sharing rules |
| [lua/functions.md](lua/functions.md) | Every function registered into Lua |
| [lua/variables.md](lua/variables.md) | Injected variables, availability per game state |
| [LuaAPI.md](LuaAPI.md) | Older, minimal API stub (superseded by `lua/`) |
| [../PEPitCompat/README.md](../PEPitCompat/README.md) | Emulator build/run/CLI |
| [../PEPitCompat/API_MAP.md](../PEPitCompat/API_MAP.md) | ESP32→Linux shim mapping |
| [../PEPitMCP/README.md](../PEPitMCP/README.md) | MCP control channel (headless sessions, screenshots, input) |
| `PEPitMCP/policy/` | Policy runner + per-game specs |
| [../T-HMI-PEPmonitor/src/games/gameLua.cpp](../T-HMI-PEPmonitor/src/games/gameLua.cpp) | The Lua engine host (variable injection, script dispatch) |
| [../T-HMI-PEPmonitor/src/physioProtocolHandler.cpp](../T-HMI-PEPmonitor/src/physioProtocolHandler.cpp) | Blow/jump detection, task & game loop |
| `SDCardContent/games/plane/`, `SDCardContent/games/miner/` | Mature reference games (plane = same shape as soccer; miner = trampoline sub-game) |
