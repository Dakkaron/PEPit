-- Soccer Penalty — prototype game (primitives only, no sprite assets)
--
-- Rules: each blow is one penalty kick, struck the moment the blow
-- FINISHES (so you can keep aiming for the whole blow). Aim with the
-- joystick or by dragging the touch anywhere on the goal area. The keeper
-- dives to where you were aiming 500 ms before the blow started.
--
-- init.lua runs once at game start (gameLua.cpp initGames_lua); mainGame.lua
-- runs every frame in every blow phase and keeps the state below.

function rgb(r, g, b)
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
end

-- 565 colors
COL_STANDS = rgb(18, 26, 58)
COL_PITCH  = rgb(0, 158, 60)
COL_PITCH2 = rgb(0, 138, 50)
COL_FRAME  = rgb(200, 208, 220)  -- pale blue-gray goal frame + net (NOT pure white, so the white ball stays the only solid-white blob)
COL_BALL   = 0xFFFF
COL_BLACK  = 0x0000
COL_KEEPER = rgb(28, 68, 208)
COL_KHEAD  = rgb(12, 30, 96)
COL_CROSS  = rgb(255, 196, 0)
COL_GOALTX = rgb(64, 230, 96)
COL_SAVETX = rgb(238, 48, 48)
COL_WHITE  = 0xFFFF

-- TV-style scoreboard (the broadcast penalty-shootout overlay): a dark bar
-- in the stands strip with team tags, the score, and one box per kick of
-- the current task — solid green = goal, dark + red X = save, dim outline
-- = not taken yet, pulsing outline = kick in flight.
COL_SBAR   = rgb(8, 12, 26)       -- bar (darker than the stands)
COL_SBX    = rgb(96, 108, 140)    -- dim outline: kick not taken yet
COL_SBFLY  = rgb(120, 176, 255)   -- pulsing outline: kick in flight
COL_SBSAVE = rgb(16, 22, 40)      -- dark fill of a save box
SBAR_H = 50                       -- the whole stands strip (goal frame starts at y 54)
SB_NAME_Y = 21                    -- "YOU" + score row (the firmware status strip owns y 0..19)
SB_BOX_Y = 38                     -- box row: 10 px boxes, 38..48
SB_PITCH = 13                     -- box + gap

-- Goal geometry (320x240 screen; firmware chrome bars live at y>=190)
GOAL_X0 = 92         -- inside edge of the left post
GOAL_X1 = 228        -- inside edge of the right post
GOAL_TOP = 58        -- underside of the crossbar
GOAL_BOTTOM = 118    -- the goal line
POST_W = 4
AIM_X_RANGE = (GOAL_X1 - GOAL_X0) / 2 - 8
AIM_Y_RANGE = (GOAL_BOTTOM - GOAL_TOP) / 2 - 6
GOAL_MID_X = (GOAL_X0 + GOAL_X1) / 2
GOAL_MID_Y = (GOAL_TOP + GOAL_BOTTOM) / 2

SPOT_X = 160         -- penalty spot
SPOT_Y = 152

KEEPER_HOME_X = GOAL_MID_X
KEEPER_HOME_Y = GOAL_BOTTOM - 9

BALL_R = 7
SAVE_RADIUS = 30     -- keeper covers a 30 px radius around the dive target
FLIGHT_MS = 350      -- ball flight spot -> target
DIVE_MS = 300        -- keeper dive center -> sampled aim
RESULT_MS = 2500     -- "GOAL!"/"SAVED!" display
RESET_MS = 3000      -- ball + keeper travel back
HIST_KEEP = 120      -- aim history samples to keep (~12 s at 10 Hz)
KEEPER_SAMPLE_MS = 500  -- the keeper reads the aim this long before the blow

-- Per-kick state
State = "idle"       -- idle | shooting | result | reset
AimX = GOAL_MID_X
AimY = GOAL_MID_Y
BallX = SPOT_X
BallY = SPOT_Y
BallR = BALL_R
KeeperX = KEEPER_HOME_X
KeeperY = KEEPER_HOME_Y
KeeperStartX = KEEPER_HOME_X
KeeperStartY = KEEPER_HOME_Y
ShotTX = GOAL_MID_X
ShotTY = GOAL_MID_Y
KeeperTX = KEEPER_HOME_X
KeeperTY = KEEPER_HOME_Y
ShotStartMs = 0
ResultMs = 0
ResetMs = 0
Goals = 0
Saves = 0
SavedLast = false
AimHist = {}
AimHistN = 0

-- Scoreboard: outcome of each kick of the CURRENT task, appended as kicks
-- resolve. 1 = goal, 2 = save, 3 = rep that produced no shot (miss).
-- mainGame.lua clears it when the rep counter wraps or the task's rep count
-- changes (the firmware resets blowCount per task, so NewRepetition is not a
-- reliable task-boundary signal).
KickLog = {}
PrevRep = 1
PrevTotal = 1

-- Read joystick/touch, ease the aim point toward it, and record it in the
-- history ring the keeper samples from.
function UpdateAim(Ms, MsDelta)
  local jx, jy = GetJoystickXY()
  local tx, ty
  if IsTouchInZone(0, GOAL_TOP - 20, 320, 160) then
    -- touch drag aims directly at the finger
    tx = Constrain(GetTouchX(), GOAL_X0 + 8, GOAL_X1 - 8)
    ty = Constrain(GetTouchY(), GOAL_TOP + 6, GOAL_BOTTOM - 6)
  elseif math.abs(jx) > 0.12 or math.abs(jy) > 0.12 then
    tx = GOAL_MID_X + jx * AIM_X_RANGE
    ty = GOAL_MID_Y + jy * AIM_Y_RANGE
  else
    tx = GOAL_MID_X
    ty = GOAL_MID_Y
  end
  local k = 1 - math.exp(-MsDelta / 120)  -- ~120 ms time constant
  AimX = AimX + (tx - AimX) * k
  AimY = AimY + (ty - AimY) * k

  -- Record the history at ~10 Hz, not every frame: the keeper samples
  -- blowStart - 500 AFTER the blow ends (the kick fires on NewRepetition),
  -- so the ring must span a whole blow (up to ~5 s). At 10 Hz, 120 samples
  -- cover 12 s, and 100 ms resolution is finer than the aim's 120 ms ease,
  -- so "closest sample" is unaffected by the thinning.
  if AimHistN == 0 or Ms - AimHist[AimHistN].ms >= 100 then
    table.insert(AimHist, {ms = Ms, x = AimX, y = AimY})
    AimHistN = AimHistN + 1
    if AimHistN > HIST_KEEP then
      table.remove(AimHist, 1)
      AimHistN = AimHistN - 1
    end
  end
end

function SampleKeeperTarget(Ms)
  -- The keeper commits to where the player was aiming KEEPER_SAMPLE_MS
  -- before the blow started (the closest history sample to that instant).
  -- Note this is called at blow END (the kick fires on NewRepetition), so
  -- the instant is a full blow in the past — the ring must reach back
  -- across the whole blow (see UpdateAim / HIST_KEEP).
  local want = BlowStartMs - KEEPER_SAMPLE_MS
  local best = nil
  for i = 1, #AimHist do
    local s = AimHist[i]
    if best == nil or math.abs(s.ms - want) < math.abs(best.ms - want) then
      best = s
    end
  end
  if best == nil then
    return KEEPER_HOME_X, KEEPER_HOME_Y
  end
  return best.x, best.y
end

function lerp(a, b, t)
  return a + (b - a) * t
end

-- One penalty box. kind: 1 = goal, 2 = save, 3 = missed rep, 0 = pending.
-- s = box size in px: DrawScoreboard shrinks it together with the pitch for
-- long tasks; defaults to 10 (the SB_PITCH=13 spacing).
function DrawKickBox(x, y, kind, current, s)
  s = s or 10
  if kind == 1 then
    FillRect(x, y, s, s, COL_GOALTX)
  elseif kind == 2 then
    FillRect(x, y, s, s, COL_SBSAVE)
    DrawRect(x, y, s, s, COL_SAVETX)
    DrawLine(x + 2, y + 2, x + s - 2, y + s - 2, COL_SAVETX)
    DrawLine(x + s - 2, y + 2, x + 2, y + s - 2, COL_SAVETX)
  elseif kind == 3 then
    DrawRect(x, y, s, s, COL_SBX)
    DrawLine(x + 2, y + 2, x + s - 2, y + s - 2, COL_SAVETX)
    DrawLine(x + s - 2, y + 2, x + 2, y + s - 2, COL_SAVETX)
  else
    DrawRect(x, y, s, s, COL_SBX)
  end
  if current then
    DrawRect(x - 1, y - 1, s + 2, s + 2, COL_SBFLY)
  end
end

-- The scoreboard bar. Reads the current task's kick log (KickLog) plus the
-- firmware rep counters; safe to call every frame.
--
-- The box row shows one box per rep OF THE CURRENT TASK — `Repetitions` is
-- the firmware's totalBlowCount for this task and is re-read every frame,
-- so a 10-rep task shows 10 boxes, a 5-rep task 5, and a 20-rep task 20
-- (the pitch shrinks to keep the row on screen beyond ~24). The row is
-- cleared when the task changes (see mainGame.lua).
function DrawScoreboard()
  FillRect(0, 0, 320, SBAR_H, COL_SBAR)
  DrawFastHLine(0, SBAR_H - 1, 320, COL_SBX)

  local n = Repetitions or 10
  if n < 1 then n = 1 end
  local rep = CurrentRepetition or 0
  local pitch = SB_PITCH
  if n * pitch > 296 then pitch = math.floor(296 / n) end  -- fit long tasks full-width
  local s = math.max(3, pitch - 3)

  local inFlight = (State == "shooting")
  local w = n * pitch - (pitch - s)
  local x0 = 160 - w // 2

  for i = 0, n - 1 do
    local ki = i + 1
    local kind = KickLog[ki] or 0
    local cur = (kind == 0 and ki == rep and inFlight)
    DrawKickBox(x0 + i * pitch, SB_BOX_Y, kind, cur, s)
  end

  -- (the firmware status strip — battery/wifi/clock + "Rep N/M" — owns
  -- y 0..19 of this bar, so the scoreboard content starts at y 21)
  SetTextSize(2)
  SetTextColor(COL_WHITE)
  DrawString("YOU", 8, SB_NAME_Y)
  local sc = Goals .. " : " .. Saves
  DrawString(sc, 160 - (#sc * 10) // 2, SB_NAME_Y)
  SetTextSize(1)
end
