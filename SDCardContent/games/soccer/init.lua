-- Soccer Penalty — prototype game (primitives only, no sprite assets)
--
-- Rules: each blow is one penalty kick. Aim with the joystick or by
-- dragging the touch anywhere on the goal area. The keeper dives to where
-- you were aiming 500 ms before you blow.
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
HIST_KEEP = 40       -- aim history samples to keep (~1 s at 30 fps)
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
WasBlowing = false
HasKicked = false
Goals = 0
Saves = 0
SavedLast = false
AimHist = {}
AimHistN = 0

function ResetKick()
  State = "idle"
  BallX = SPOT_X
  BallY = SPOT_Y
  BallR = BALL_R
  KeeperX = KEEPER_HOME_X
  KeeperY = KEEPER_HOME_Y
  KeeperStartX = KEEPER_HOME_X
  KeeperStartY = KEEPER_HOME_Y
  HasKicked = false
end

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

  table.insert(AimHist, {ms = Ms, x = AimX, y = AimY})
  AimHistN = AimHistN + 1
  if AimHistN > HIST_KEEP then
    table.remove(AimHist, 1)
    AimHistN = AimHistN - 1
  end
end

function SampleKeeperTarget(Ms)
  -- The keeper commits to where the player was aiming KEEPER_SAMPLE_MS
  -- before the blow started (the closest history sample to that instant).
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
