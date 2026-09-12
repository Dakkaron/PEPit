# Lua Variables

This document describes the **global variables** that the host application injects into the Lua
state before running each game script. They are set in three places in
[`src/games/gameLua.cpp`](../../src/games/gameLua.cpp):

| C function                                   | Game state            | Script run afterwards                          |
|----------------------------------------------|-----------------------|------------------------------------------------|
| [`initGames_lua()`](../../src/games/gameLua.cpp#L1144)   | **Init**              | `init.lua`                                    |
| [`updateBlowData()`](../../src/games/gameLua.cpp#L1165)  | **PEP / Inhalation**  | `pepShort` / `pepLong` / `pepEqual` / `inhalation` / `inhalationPep` scripts |
| [`updateJumpData()`](../../src/games/gameLua.cpp#L1203)  | **Trampoline**        | `trampoline` script                           |

## How the variables are passed

Each of these functions builds a string of Lua `name=value` assignments and executes it with
`lua_dostring()`. Because the values are written as Lua literals, they arrive in the script as
**real numbers and booleans** (not strings):

```cpp
// produces the Lua source:  "Ms=12345\nLeftHandedMode=true\n"
String variablesString = "Ms="+String(millis())+"\n"+
                         "LeftHandedMode="+String(systemConfig.leftHandMode ? "true\n" : "false\n");
lua_dostring(variablesString.c_str(), "initGames_lua()");
```

So in a game script you can use them directly, e.g. `if CurrentlyBlowing then ... end` or
`local t = Ms / 1000`.

> **Timing.** `updateBlowData()` and `updateJumpData()` are called on every frame *before* the
> game's draw script runs, so the values always reflect the latest sensor/therapy state. The
> `init.lua` script only receives the **Init** variables (see table below) — it runs once when
> the game starts and does **not** get the per-frame blow/jump data.

---

## Availability by state

A check (✓) means the variable is set in that state. Variables without a check are **not**
defined (they will be `nil`) in that state.

| Variable                 | Type      | Init | PEP / Inhalation | Trampoline |
|--------------------------|-----------|:----:|:----------------:|:----------:|
| `Ms`                     | number    | ✓    | ✓                | ✓          |
| `LeftHandedMode`         | boolean   | ✓    | ✓                | ✓          |
| `MsDelta`                | number    |      | ✓                | ✓          |
| `CycleNumber`            | number    |      | ✓                | ✓          |
| `TotalCycleNumber`       | number    |      | ✓                | ✓          |
| `CurrentRepetition`      | number    |      | ✓                | ✓          |
| `NewRepetition`          | boolean   |      | ✓                | ✓          |
| `CumulatedTaskNumber`    | number    |      | ✓                | ✓          |
| `TaskNumber`             | number    |      | ✓                | ✓          |
| `TotalTaskNumber`        | number    |      | ✓                | ✓          |
| `CurrentlyBlowing`       | boolean   |      | ✓                |            |
| `BlowStartMs`            | number    |      | ✓                |            |
| `BlowEndMs`              | number    |      | ✓                |            |
| `TargetDurationMs`       | number    |      | ✓                |            |
| `Repetitions`            | number    |      | ✓                |            |
| `Pressure`               | number    |      | ✓                |            |
| `PeakPressure`           | number    |      | ✓                |            |
| `MinPressure`            | number    |      | ✓                |            |
| `CumulativeError`        | number    |      | ✓                |            |
| `Fails`                  | number    |      | ✓                |            |
| `TaskType`               | number    |      | ✓                |            |
| `LastBlowStatus`         | number    |      | ✓                |            |
| `TotalTimeSpentBreathing`| number    |      | ✓                |            |
| `TaskStartMs`            | number    |      | ✓                |            |
| `IsNewTask`              | boolean   |      | ✓                |            |
| `BreathingScore`         | number    |      | ✓                |            |
| `CurrentlyJumping`       | boolean   |      |                  | ✓          |
| `CurrentBonusRepetition` | number    |      |                  | ✓          |
| `NewBonusRepetition`     | boolean   |      |                  | ✓          |
| `MsLeft`                 | number    |      |                  | ✓          |
| `LastJumpMs`             | number    |      |                  | ✓          |

---

## Variable reference

### Common to all states

#### `Ms` — number
Current time in **milliseconds**. In the Init state this is `millis()` (time since boot); during
a game it is the therapy/jump clock. Use together with `MsDelta` for frame-rate-independent
animation.

#### `LeftHandedMode` — boolean
`true` when the device is configured for left-handed use. Lets a game mirror its layout/controls.

### Common to PEP/Inhalation and Trampoline

#### `MsDelta` — number
Elapsed time in **milliseconds** since the previous update. For blow data this is forced to `1`
on a new task (to avoid a large jump); otherwise it is `ms - lastMs`.

#### `CycleNumber` — number
Index of the current **cycle** (0-based). A cycle is one full pass through all tasks.

#### `TotalCycleNumber` — number
Total number of cycles in the exercise.

#### `CurrentRepetition` — number
Number of repetitions completed so far within the current task. For blow games this is the blow
count; for trampoline it is the jump count.

#### `NewRepetition` — boolean
`true` on the frame where a new repetition just started. Useful for triggering one-shot effects
(e.g. a sound or animation) at the start of each rep.

#### `CumulatedTaskNumber` — number
Global task index across the whole exercise: `taskNumber + cycleNumber * totalTaskNumber`.

#### `TaskNumber` — number
Same value as `CumulatedTaskNumber` (the global task index).

#### `TotalTaskNumber` — number
Number of tasks per cycle.

### PEP / Inhalation only (blow data)

#### `CurrentlyBlowing` — boolean
`true` while the patient is actively blowing.

#### `BlowStartMs` — number
Timestamp (ms) when the current blow started.

#### `BlowEndMs` — number
Timestamp (ms) when the current blow ended.

#### `TargetDurationMs` — number
Target duration of a single blow, in milliseconds.

#### `Repetitions` — number
Total number of repetitions expected in the current task (`totalBlowCount`).

#### `Pressure` — number
Current measured pressure (float).

#### `PeakPressure` — number
Highest pressure reached during the current blow.

#### `MinPressure` — number
Lowest pressure reached during the current blow.

#### `CumulativeError` — number
Accumulated error over the task (deviation from target).

#### `Fails` — number
Number of failed blows in the current task.

#### `TaskType` — number
Identifier for the type of the current task (see the therapy/task definitions).

#### `LastBlowStatus` — number
Status code of the most recently completed blow.

#### `TotalTimeSpentBreathing` — number
Total time spent breathing, in milliseconds.

#### `TaskStartMs` — number
Timestamp (ms) when the current task started.

#### `IsNewTask` — boolean
`true` on the frame where a new task just began.

#### `BreathingScore` — number
Score for the current breathing task.

### Trampoline only (jump data)

#### `CurrentlyJumping` — boolean
`true` while the patient is currently on / jumping on the trampoline.

#### `CurrentBonusRepetition` — number
Number of **bonus** jumps completed so far in the current task (`bonusJumpCount`).

#### `NewBonusRepetition` — boolean
`true` on the frame where a new bonus repetition just started.

#### `MsLeft` — number
Time remaining in the current task, in milliseconds.

#### `LastJumpMs` — number
Timestamp (ms) of the most recent jump. Used to time animations relative to the last jump.
