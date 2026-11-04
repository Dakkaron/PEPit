-- Soccer Penalty — penalty shootout with a bone-animated keeper.
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
COL_FRAME  = rgb(200, 208, 220)  -- goal line (NOT pure white, so the ball stays the only solid-white blob)
COL_CROSS  = rgb(255, 196, 0)
COL_GOALTX = rgb(64, 230, 96)
COL_SAVETX = rgb(238, 48, 48)
COL_WHITE  = 0xFFFF

-- Sprites: 16-bit RGB565 BMPs, pink is the transparent mask colour.
MASK        = 0xF81F
CENTER      = ALIGN_H_CENTER | ALIGN_V_CENTER
SBackground = LoadSprite("gfx/bg.bmp")
SBall       = LoadAnimSprite("gfx/ball.bmp", 16, 16, 0, MASK)
SKeeperBody = LoadSprite("gfx/keeper_body.bmp", 0, MASK)
SArmUpL     = LoadSprite("gfx/arm_up.bmp", 0, MASK)
SArmUpR     = LoadSprite("gfx/arm_up_r.bmp", 0, MASK)
SArmForeL   = LoadSprite("gfx/arm_fore.bmp", 0, MASK)
SArmForeR   = LoadSprite("gfx/arm_fore_r.bmp", 0, MASK)
SGloveL     = LoadSprite("gfx/glove.bmp", 0, MASK)
SGloveR     = LoadSprite("gfx/glove_r.bmp", 0, MASK)

-- Keeper skeleton geometry (from gen_sprites.py): every arm part is
-- padded so its joint pixel is the exact sprite centre, which is the
-- pivot DrawSprite rotates around. Bone sprites are authored hanging
-- DOWN, so at angle a a bone points along (-sin a, cos a); angles are
-- radians, positive = clockwise on screen.
SHOULDER_DX  = 8
SHOULDER_DY  = -10
ARM_UP_LEN   = 11
ARM_FORE_LEN = 6
DIVE_PHI     = 1.0   -- body tilt at full dive
DIVE_UP      = 0.6   -- arm raise past horizontal at full dive
ARM_FOLD     = 0.35  -- elbow bend past the upper arm at full dive

-- Dive side: 0 for a purely vertical stretch (the keeper jumps without
-- leaning), otherwise the sign of the horizontal travel.
function DiveDir(dx)
  if dx > 8 then return 1 end
  if dx < -8 then return -1 end
  return 0
end

-- Draw the keeper at pelvis (x, y), dove by ke (0..1) toward sign dir.
function DrawKeeper(x, y, ke, dir)
  local phi = dir * DIVE_PHI * ke
  local cs, ss = math.cos(phi), math.sin(phi)
  local tgt = dir == 0 and math.pi or -dir * (math.pi / 2 + DIVE_UP)
  local up = lerp(0, tgt, ke)
  local fo = up - dir * ARM_FOLD * ke
  local dxL, dyL = -SHOULDER_DX, SHOULDER_DY
  local dxR, dyR = SHOULDER_DX, SHOULDER_DY
  local shLx = x + cs * dxL - ss * dyL
  local shLy = y + ss * dxL + cs * dyL
  local shRx = x + cs * dxR - ss * dyR
  local shRy = y + ss * dxR + cs * dyR
  DrawSprite(SArmUpL, shLx, shLy, {angle = up, flags = CENTER})
  DrawSprite(SArmUpR, shRx, shRy, {angle = up, flags = CENTER})
  DrawSprite(SKeeperBody, x, y, {angle = phi, flags = CENTER})
  DrawSprite(SArmForeL, shLx - ARM_UP_LEN * math.sin(up), shLy + ARM_UP_LEN * math.cos(up),
            {angle = fo, flags = CENTER})
  DrawSprite(SArmForeR, shRx - ARM_UP_LEN * math.sin(up), shRy + ARM_UP_LEN * math.cos(up),
            {angle = fo, flags = CENTER})
  DrawSprite(SGloveL, shLx - (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fo),
            shLy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fo), {angle = fo, flags = CENTER})
  DrawSprite(SGloveR, shRx - (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fo),
            shRy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fo), {angle = fo, flags = CENTER})
end

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

-- Goal geometry: taken straight from the backdrop photo (gen_sprites.py
-- measures the frame in the 320x240 background and prints these numbers).
GOAL_X0 = 62         -- inside edge of the left post
GOAL_X1 = 256        -- inside edge of the right post
GOAL_TOP = 62        -- underside of the crossbar
GOAL_BOTTOM = 124    -- the goal line
AIM_X_RANGE = (GOAL_X1 - GOAL_X0) / 2 - 8
AIM_Y_RANGE = (GOAL_BOTTOM - GOAL_TOP) / 2 - 6
GOAL_MID_X = (GOAL_X0 + GOAL_X1) / 2
GOAL_MID_Y = (GOAL_TOP + GOAL_BOTTOM) / 2

SPOT_X = 160         -- penalty spot
SPOT_Y = 152

KEEPER_FOOT_DROP = 26   -- pelvis -> shoe sole in keeper_body.bmp
KEEPER_HOME_X = GOAL_MID_X
KEEPER_HOME_Y = GOAL_BOTTOM - KEEPER_FOOT_DROP
KEEPER_DIVE_MIN_Y = GOAL_TOP + 18
KEEPER_DIVE_MAX_Y = GOAL_BOTTOM - KEEPER_FOOT_DROP

BALL_R = 7
SAVE_RADIUS = 30     -- keeper covers a 30 px radius around the dive target
FLIGHT_MS = 350      -- ball flight spot -> target
DIVE_MS = 300        -- keeper dive center -> sampled aim
RESULT_MS = 2500     -- "GOAL!"/"SAVED!" display
RESET_MS = 3000      -- ball + keeper travel back
AIM_DEADBAND = 0.12       -- smaller joystick deflection: no aim at all

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
KeeperCommitX = GOAL_MID_X
KeeperCommitY = GOAL_MID_Y
WasBlowing = false

-- Scoreboard: outcome of each kick of the CURRENT task, appended as kicks
-- resolve. 1 = goal, 2 = save, 3 = rep that produced no shot (miss).
-- mainGame.lua clears it when the rep counter wraps or the task's rep count
-- changes (the firmware resets blowCount per task, so NewRepetition is not a
-- reliable task-boundary signal).
KickLog = {}
PrevRep = 1
PrevTotal = 1

-- Read joystick/touch and ease the crosshair toward it. Both inputs only ever
-- move the crosshair — nothing else reads them — so the keeper's reaction is
-- always defined by where the crosshair is.
function UpdateAim(Ms, MsDelta)
  local jx, jy = GetJoystickXY()
  local tx, ty
  if IsTouchInZone(0, GOAL_TOP - 20, 320, 160) then
    -- touch drag aims directly at the finger
    tx = Constrain(GetTouchX(), GOAL_X0 + 8, GOAL_X1 - 8)
    ty = Constrain(GetTouchY(), GOAL_TOP + 6, GOAL_BOTTOM - 6)
  elseif math.abs(jx) > AIM_DEADBAND or math.abs(jy) > AIM_DEADBAND then
    tx = GOAL_MID_X + jx * AIM_X_RANGE
    ty = GOAL_MID_Y + jy * AIM_Y_RANGE
  else
    tx = GOAL_MID_X
    ty = GOAL_MID_Y
  end
  local k = 1 - math.exp(-MsDelta / 120)  -- ~120 ms time constant
  AimX = AimX + (tx - AimX) * k
  AimY = AimY + (ty - AimY) * k
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
