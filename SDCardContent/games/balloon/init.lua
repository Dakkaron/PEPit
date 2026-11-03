-- Hot Air Balloon — prototype game (primitives only, no sprite assets)
--
-- Rules: steer the balloon with the joystick or by dragging the touch.
-- Each successful blow stokes the flame and adds fuel (NewRepetition
-- pulses at blow END, like in soccer). Fuel drains constantly and
-- maneuvering drains it faster. Hitting an obstacle costs fuel. When the
-- fuel runs out the balloon falls and resets.
--
-- init.lua runs once at game start (gameLua.cpp initGames_lua); mainGame.lua
-- runs every frame in every blow phase and keeps the state below.

function rgb(r, g, b)
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
end

-- 565 colors
COL_SKY      = rgb(30, 110, 180)
COL_HAZE     = rgb(150, 200, 235)
COL_BALLOON  = rgb(225, 55, 45)
COL_BALLOON2 = rgb(250, 120, 40)
COL_DEAD     = rgb(120, 124, 130)  -- envelope after a crash
COL_BASKET   = rgb(110, 70, 30)
COL_ROPE     = rgb(210, 216, 226)
COL_FLAME    = rgb(255, 170, 40)
COL_FLAME2   = rgb(255, 60, 20)
COL_CLOUD    = rgb(210, 216, 226)
COL_BIRD     = rgb(20, 24, 30)
COL_FUEL_OK  = rgb(0, 170, 90)
COL_FUEL_LOW = rgb(220, 40, 40)
COL_WHITE    = 0xFFFF
COL_BLACK    = 0x0000

-- Playfield (320x240 screen; the firmware status strip owns y 0..19 and
-- the bottom bars y >= 190, so game content lives between them).
PLAY_TOP    = 22
PLAY_BOTTOM = 188
GROUND_Y    = PLAY_BOTTOM - 18  -- haze/ground strip inside the playfield

-- Physics / game balance
BALLOON_R     = 16
START_X       = 70
START_Y       = 105
MAX_SPEED     = 70   -- px/s, joystick full deflection
FUEL_MAX      = 100
FUEL_START    = 60
FUEL_PER_BLOW = 40   -- one successful blow
IDLE_DRAIN    = 2.0  -- fuel/s
STEER_DRAIN   = 5.0  -- extra fuel/s at full input
BLOW_GAIN     = 6.0  -- fuel/s while actively blowing (the flame is stoked)
HIT_COST      = 30   -- fuel lost per obstacle hit
CRASH_MS      = 2200 -- how long the balloon falls before the reset
SPAWN_MS      = 2200 -- obstacle spawn interval
DRIFT_SPEED   = 32   -- px/s, obstacles drift leftward

-- Mutable state (globals so it survives between scripts)
Fuel        = FUEL_START
BalloonX    = START_X
BalloonY    = START_Y
VelX        = 0
VelY        = 0
Obstacles   = {}     -- {x, y, r, kind, passed}; kind 0 = cloud, 1 = bird
LastSpawnMs = 0
Passed      = 0
Crashes     = 0
State       = "flying"  -- flying | crashing
CrashMs     = 0
FlameUntilMs = 0

function SpawnObstacle()
  local r = 10 + math.random(0, 8)
  local y = PLAY_TOP + r + 8 + math.random(0, PLAY_BOTTOM - PLAY_TOP - 2 * (r + 8))
  table.insert(Obstacles, { x = 322 + r, y = y, r = r,
                            kind = math.random(0, 1), passed = false })
end

-- Joystick axes or touch drag -> eased target velocity.
function UpdateSteering(Ms, MsDelta)
  local jx, jy = GetJoystickXY()
  if math.abs(jx) < 0.12 and math.abs(jy) < 0.12 then
    jx, jy = 0, 0
  end
  local tvx, tvy = jx * MAX_SPEED, jy * MAX_SPEED
  if IsTouchInZone(0, PLAY_TOP, 320, PLAY_BOTTOM - PLAY_TOP) then
    -- dragging steers toward the finger
    tvx = Constrain(GetTouchX() - BalloonX, -MAX_SPEED, MAX_SPEED)
    tvy = Constrain(GetTouchY() - BalloonY, -MAX_SPEED, MAX_SPEED)
  end
  local mag = math.min(math.sqrt(jx * jx + jy * jy), 1)
  if IsTouchInZone(0, PLAY_TOP, 320, PLAY_BOTTOM - PLAY_TOP) then
    mag = math.min(math.sqrt(tvx * tvx + tvy * tvy) / MAX_SPEED, 1)
  end
  local k = 1 - math.exp(-MsDelta / 180)  -- ~180 ms time constant
  VelX = VelX + (tvx - VelX) * k
  VelY = VelY + (tvy - VelY) * k
  return mag
end

function DrawCloud(x, y, r)
  FillCircle(x, y, r, COL_CLOUD)
  FillCircle(x - r * 0.7, y + r * 0.3, r * 0.6, COL_CLOUD)
  FillCircle(x + r * 0.7, y + r * 0.3, r * 0.5, COL_CLOUD)
end

function DrawBird(x, y, r)
  DrawLine(x - r, y - r * 0.6, x, y, COL_BIRD)
  DrawLine(x, y, x + r, y - r * 0.6, COL_BIRD)
  FillCircle(x, y, 2, COL_BIRD)
end

function DrawBalloon(Ms)
  local lit = CurrentlyBlowing or Ms < FlameUntilMs
  local col = State == "crashing" and COL_DEAD or COL_BALLOON
  FillCircle(BalloonX, BalloonY, BALLOON_R, col)
  DrawCircle(BalloonX, BalloonY, BALLOON_R, COL_BLACK)
  DrawCircle(BalloonX, BalloonY, BALLOON_R - 5, State == "crashing" and COL_DEAD or COL_BALLOON2)
  -- ropes + basket
  DrawLine(BalloonX - 8, BalloonY + BALLOON_R, BalloonX - 5, BalloonY + BALLOON_R + 5, COL_ROPE)
  DrawLine(BalloonX + 8, BalloonY + BALLOON_R, BalloonX + 5, BalloonY + BALLOON_R + 5, COL_ROPE)
  FillRect(BalloonX - 6, BalloonY + BALLOON_R + 5, 12, 7, COL_BASKET)
  DrawRect(BalloonX - 6, BalloonY + BALLOON_R + 5, 12, 7, COL_BLACK)
  -- burner flame in the gap between envelope and basket
  if lit then
    local fr = (Ms < FlameUntilMs) and 5 or 3
    FillCircle(BalloonX, BalloonY + BALLOON_R + 2, fr, COL_FLAME)
    FillCircle(BalloonX, BalloonY + BALLOON_R + 1, fr - 2, COL_FLAME2)
  end
end

function DrawFuelGauge()
  FillRect(4, PLAY_TOP + 2, 16, 46, COL_BLACK)
  local h = (Fuel * 44) // FUEL_MAX
  FillRect(5, PLAY_TOP + 47 - h, 14, h, Fuel < 35 and COL_FUEL_LOW or COL_FUEL_OK)
  DrawRect(4, PLAY_TOP + 2, 16, 46, COL_WHITE)
  SetTextColor(COL_WHITE)
  SetTextSize(1)
  DrawString("Tank", 4, PLAY_TOP + 50)
end
