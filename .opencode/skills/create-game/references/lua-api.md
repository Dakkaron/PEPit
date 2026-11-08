# PEPit Lua game API

Extracted from `T-HMI-PEPmonitor/src/games/gameLua.cpp`. Re-verify against the source when the firmware API changes.

## Globals injected at init (`initGames_lua`, before `init.lua` runs)

`Ms`, `LeftHandedMode`

## Globals injected every frame during pep short/long/equal and inhalation phases (`updateBlowData`)

| Global | Meaning |
| --- | --- |
| `CurrentlyBlowing` | a blow is in progress |
| `Ms` | millis |
| `MsDelta` | ms since previous frame (1 on new task) |
| `BlowStartMs`, `BlowEndMs`, `TargetDurationMs` | current blow timing |
| `TaskStartMs` | start of current task |
| `CurrentRepetition` | blow count in current task |
| `NewRepetition` | one-frame `true` pulse at blow END |
| `Repetitions` | total blows per task |
| `CycleNumber`, `TotalCycleNumber` | cycle position |
| `TaskNumber`, `CumulatedTaskNumber`, `TotalTaskNumber`, `IsNewTask` | task identity; Cumulated = taskNumber + cycleNumber * totalTaskNumber |
| `TaskType` | task type code |
| `Pressure`, `PeakPressure`, `MinPressure`, `CumulativeError` | breath metrics |
| `Fails`, `LastBlowStatus` | blow outcomes (`LastBlowStatus`: 1 succeeded, 2 failed, 4 new blow) |
| `TotalTimeSpentBreathing`, `BreathingScore` | session stats |
| `LeftHandedMode` | system setting |

## Globals injected during trampoline/jump tasks (`updateJumpData`)

`Ms`, `MsDelta`, `CycleNumber`, `TotalCycleNumber`, `CumulatedTaskNumber`, `TaskNumber`, `TotalTaskNumber`, `CurrentRepetition` (jump count), `CurrentBonusRepetition`, `CurrentlyJumping`, `NewRepetition`, `NewBonusRepetition`, `MsLeft`, `LastJumpMs`, `LeftHandedMode`

## Registered Lua functions

- Drawing: `DrawString`, `DrawRect`, `FillRect`, `DrawLine`, `DrawFastHLine`, `DrawFastVLine`, `DrawCircle`, `FillCircle`, `DrawSprite`, `FillSprite`, `DrawMode7`, `DrawRaceOutdoor`, `DrawRaceTunnel`
- Sprites: `LoadSprite`, `LoadAnimSprite`, `FreeSprite`, `SpriteWidth`, `SpriteHeight`, `SetDrawTargetFramebuffer`, `SetDrawTargetSprite`
- Text: `SetTextColor`, `SetTextDatum`, `SetTextSize`, `SetCursor`, `Print`, `Println`, `SerialPrint`, `SerialPrintln`
- Mode7/road: `Mode7WorldToScreen`, `CalculateRoadProperties`, `ProjectRoadPointToScreen`, `SetRoadColors`, `SetRoadDimensions`, `SetRoadDrawFlags`
- Input: `GetJoystickX`, `GetJoystickY`, `GetJoystickXY`, `GetJoystickButton`, `IsJoystickPresent`, `GetTouchX`, `GetTouchY`, `IsTouchInZone`
- Prefs: `PrefsGetInt`, `PrefsSetInt`, `PrefsGetNumber`, `PrefsSetNumber`, `PrefsGetString`, `PrefsSetString`, `PrefsGetBytes`, `PrefsSetBytes` (scoped to the game's `prefsNamespace`)
- System: `GetFreeRAM`, `GetFreePSRAM`, `GetFreeSpriteSlots`, `Constrain`, `RunScript`, `DisableCaching`, `CloseProgressionMenu`, `CloseWinScreen`

## Notes

- `PrefsSetInt`/`PrefsSetNumber` use `luaL_checknumber`: passing `nil` raises a Lua error. Persist only globals guaranteed to exist.
- `luaCacheGameCode = true`: game code is cached after first parse; `DisableCaching` exists for games that must reload scripts. `end.lua` is loaded uncached.
- Standard Lua libraries available: `table`, `string`, `math` (Lua 5.4 on device).
