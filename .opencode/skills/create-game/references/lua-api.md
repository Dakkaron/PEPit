# PEPit Lua API (from `T-HMI-PEPmonitor/src/games/gameLua.cpp`; re-verify after firmware changes)

init (`init.lua`): `Ms`, `LeftHandedMode`

`updateBlowData` (per frame, pep short/long/equal + inhalation):
`CurrentlyBlowing` `Ms` `MsDelta` (=1 on new task) `BlowStartMs` `BlowEndMs` `TargetDurationMs` `TaskStartMs` `CurrentRepetition` (blows in task) `NewRepetition` (one-frame true at blow END) `Repetitions` (blows per task) `CycleNumber` `TotalCycleNumber` `TaskNumber` `CumulatedTaskNumber` (=taskNumber+cycleNumber*totalTaskNumber) `TotalTaskNumber` `IsNewTask` `TaskType` `Pressure` `PeakPressure` `MinPressure` `CumulativeError` `Fails` `LastBlowStatus` (1 succeeded = blow ended after lasting >`TargetDurationMs`, 2 failed = ended sooner, 4 new blow; blowing past the target is never a fail) `TotalTimeSpentBreathing` `BreathingScore` `LeftHandedMode`

`updateJumpData` (trampoline):
`Ms` `MsDelta` `CycleNumber` `TotalCycleNumber` `CumulatedTaskNumber` `TaskNumber` `TotalTaskNumber` `CurrentRepetition` (jumps) `CurrentBonusRepetition` `CurrentlyJumping` `NewRepetition` `NewBonusRepetition` `MsLeft` `LastJumpMs` `LeftHandedMode` `TaskType` (=4, added so one script can serve every task type)

Task types: 1 pepShort 2 pepLong 3 pepEqual 4 trampoline 5 inhalation 6 inhalationPep (`PROFILE_TASK_TYPE_*`, `constants.h`). A game that wants one `mainGame.lua` for all types branches on `TaskType`; trampoline globals have no `Pressure`/`LastBlowStatus`/`Repetitions`, so guard them (`or 0` / `or ""`). `NewRepetition` on trampoline only fires for jumps above `taskMinStrength`, so a counted jump is always a success.

Functions:

- Draw: `DrawString` `DrawRect` `FillRect` `DrawLine` `DrawFastHLine` `DrawFastVLine` `DrawCircle` `FillCircle` `DrawSprite` `FillSprite` `DrawMode7` `DrawRaceOutdoor` `DrawRaceTunnel`
- Sprites: `LoadSprite` `LoadAnimSprite` `FreeSprite` `SpriteWidth` `SpriteHeight` `SetDrawTargetFramebuffer` `SetDrawTargetSprite`
- Text: `SetTextColor` `SetTextDatum` `SetTextSize` `SetCursor` `Print` `Println` `SerialPrint` `SerialPrintln`
- Road/Mode7: `Mode7WorldToScreen` `CalculateRoadProperties` `ProjectRoadPointToScreen` `SetRoadColors` `SetRoadDimensions` `SetRoadDrawFlags`
- Input: `GetJoystickX` `GetJoystickY` `GetJoystickXY` `GetJoystickButton` `IsJoystickPresent` `GetTouchX` `GetTouchY` `IsTouchInZone`
- Prefs (scoped to `prefsNamespace`): `PrefsGetInt` `PrefsSetInt` `PrefsGetNumber` `PrefsSetNumber` `PrefsGetString` `PrefsSetString` `PrefsGetBytes` `PrefsSetBytes`
- System: `GetFreeRAM` `GetFreePSRAM` `GetFreeSpriteSlots` `Constrain` `RunScript` `DisableCaching` `CloseProgressionMenu` `CloseWinScreen`

`RunScript` and `LoadSprite` only resolve inside the game directory (`luaGamePath` prefix, `gameLua.cpp`); `..` is not a valid FatFs name, so a game cannot reach the SD-card root - shared code must be copied into the game folder (progression library: master `SDCardContent/lua/progressionlib.lua`, loaded from `init.lua` with `RunScript("progressionlib.lua")`). `lua_dofile` caches each script in one of 10 slots and re-executes the cached chunk each frame; a one-line `RunScript("path")` file is treated as a redirect by `findScriptRedirectPath`.

Progression library (`progressionlib.lua`): the menu is drawn and driven inside its own loop, keeping the systemui area clear (title `HEAD_X=110`, tabs y70, rows y100 with `ROW_STEP` 44, scroll buttons at 275,100 and 275,150, confirm dialog from x110/y40, "Fertig" 240,200,80,40). Navigation is edge-triggered: `ProgRegister` items are walked in registration order (tabs, then visible rows - only unlocked or affordable-requirement rows register, then the two scroll buttons, then "Fertig"), and `ProgNavMoved` only resets when the stick is recentered, so one deflection equals one step and a repeatable step needs a recenter between deflections (same semantics as horserace's `MenuNavMoved`). While a confirm dialog is open the registered items are `[yes, no, done]`; an out-of-range `ProgSel` lands on "Ja", and the press guard keeps a tap that opened the dialog from immediately triggering its buttons.

Notes: `PrefsSetInt`/`PrefsSetNumber` error on nil (`luaL_checknumber`). Game code is cached after first parse (`DisableCaching` exists; `end.lua` is uncached). Lua 5.4: `table` `string` `math` available; only `base` `table` `string` `math` are opened as globals (`LUA_COMPAT_5_3` is off, so `math.atan2` `math.pow` `math.log10` are nil — use `math.atan(y, x)` `x ^ y` and `math.log(x, 10)`).
