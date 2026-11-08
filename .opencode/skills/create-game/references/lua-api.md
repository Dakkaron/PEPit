# PEPit Lua API (from `T-HMI-PEPmonitor/src/games/gameLua.cpp`; re-verify after firmware changes)

init (`init.lua`): `Ms`, `LeftHandedMode`

`updateBlowData` (per frame, pep short/long/equal + inhalation):
`CurrentlyBlowing` `Ms` `MsDelta` (=1 on new task) `BlowStartMs` `BlowEndMs` `TargetDurationMs` `TaskStartMs` `CurrentRepetition` (blows in task) `NewRepetition` (one-frame true at blow END) `Repetitions` (blows per task) `CycleNumber` `TotalCycleNumber` `TaskNumber` `CumulatedTaskNumber` (=taskNumber+cycleNumber*totalTaskNumber) `TotalTaskNumber` `IsNewTask` `TaskType` `Pressure` `PeakPressure` `MinPressure` `CumulativeError` `Fails` `LastBlowStatus` (1 ok, 2 failed, 4 new blow) `TotalTimeSpentBreathing` `BreathingScore` `LeftHandedMode`

`updateJumpData` (trampoline):
`Ms` `MsDelta` `CycleNumber` `TotalCycleNumber` `CumulatedTaskNumber` `TaskNumber` `TotalTaskNumber` `CurrentRepetition` (jumps) `CurrentBonusRepetition` `CurrentlyJumping` `NewRepetition` `NewBonusRepetition` `MsLeft` `LastJumpMs` `LeftHandedMode`

Functions:

- Draw: `DrawString` `DrawRect` `FillRect` `DrawLine` `DrawFastHLine` `DrawFastVLine` `DrawCircle` `FillCircle` `DrawSprite` `FillSprite` `DrawMode7` `DrawRaceOutdoor` `DrawRaceTunnel`
- Sprites: `LoadSprite` `LoadAnimSprite` `FreeSprite` `SpriteWidth` `SpriteHeight` `SetDrawTargetFramebuffer` `SetDrawTargetSprite`
- Text: `SetTextColor` `SetTextDatum` `SetTextSize` `SetCursor` `Print` `Println` `SerialPrint` `SerialPrintln`
- Road/Mode7: `Mode7WorldToScreen` `CalculateRoadProperties` `ProjectRoadPointToScreen` `SetRoadColors` `SetRoadDimensions` `SetRoadDrawFlags`
- Input: `GetJoystickX` `GetJoystickY` `GetJoystickXY` `GetJoystickButton` `IsJoystickPresent` `GetTouchX` `GetTouchY` `IsTouchInZone`
- Prefs (scoped to `prefsNamespace`): `PrefsGetInt` `PrefsSetInt` `PrefsGetNumber` `PrefsSetNumber` `PrefsGetString` `PrefsSetString` `PrefsGetBytes` `PrefsSetBytes`
- System: `GetFreeRAM` `GetFreePSRAM` `GetFreeSpriteSlots` `Constrain` `RunScript` `DisableCaching` `CloseProgressionMenu` `CloseWinScreen`

Notes: `PrefsSetInt`/`PrefsSetNumber` error on nil (`luaL_checknumber`). Game code is cached after first parse (`DisableCaching` exists; `end.lua` is uncached). Lua 5.4: `table` `string` `math` available.
