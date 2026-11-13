
function rgb(r, g, b)
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
end

COL_FUEL_OK  = rgb(0, 170, 90)
COL_FUEL_LOW = rgb(220, 40, 40)
COL_WHITE    = 0xFFFF
COL_BLACK    = 0x0000

PLAY_TOP    = 22
PLAY_BOTTOM = 188
GROUND_Y    = PLAY_BOTTOM - 23

MASK        = 0xF81F
SBackground = LoadSprite("gfx/sky.bmp")
BalloonSkin = {
  LoadSprite("gfx/balloon.bmp", 0, MASK),
  LoadSprite("gfx/balloon_blue.bmp", 0, MASK),
  LoadSprite("gfx/balloon_green.bmp", 0, MASK),
  LoadSprite("gfx/balloon_gold.bmp", 0, MASK),
}
SBalloonDead = LoadSprite("gfx/balloon_dead.bmp", 0, MASK)
SFlame      = LoadAnimSprite("gfx/flame.bmp", 14, 10, 0, MASK)
SCloud      = LoadSprite("gfx/cloud.bmp", 0, MASK)
SBird       = LoadAnimSprite("gfx/bird.bmp", 32, 14, 0, MASK)
SGauge      = LoadSprite("gfx/gauge.bmp", 0, MASK)
STuft       = LoadSprite("gfx/tuft.bmp", 0, MASK)

ENV_CX, ENV_CY = 22, 17
BASKET_DROP = 33
FLAME_DX, FLAME_DY = 7, 16
GAUGE_X, GAUGE_Y = 4, PLAY_TOP + 2

BALLOON_R     = 16
START_X       = 70
START_Y       = 105
FUEL_MAX      = 100
FUEL_START    = 60

DIST_PER_COIN    = 200
PX_PER_M         = 10
SPEED_STEP       = 15
DRIFT_STEP       = 12
INSUL_IDLE_STEP  = 0.7
INSUL_STEER_STEP = 1.5
SPEED_PRICE      = { 80, 160, 320 }
INSUL_PRICE      = { 120, 240 }
SKIN_PRICE       = { 0, 80, 180, 350 }

SPEED_LEVEL      = PrefsGetInt("speedLevel")
INSULATION_LEVEL = PrefsGetInt("insulationLevel")
SKINS_UNLOCKED   = PrefsGetInt("skinsUnlocked") | 1
SKIN             = Constrain(PrefsGetInt("skin"), 0, 3)
if ((SKINS_UNLOCKED >> SKIN) & 1) == 0 then SKIN = 0 end

MAX_SPEED     = 70 + SPEED_LEVEL * SPEED_STEP
DRIFT_SPEED   = 32 + SPEED_LEVEL * DRIFT_STEP
IDLE_DRAIN    = 2.0 - INSULATION_LEVEL * INSUL_IDLE_STEP
STEER_DRAIN   = 5.0 - INSULATION_LEVEL * INSUL_STEER_STEP
BLOW_GAIN     = 6.0
FUEL_PER_BLOW = 40
HIT_COST      = 30
CRASH_MS      = 2200
SPAWN_MS      = 2200
GROUND_SPACING = 48

Fuel        = FUEL_START
GroundOffset = 0
Distance    = 0
EarnedSaved = 0
BalloonX    = START_X
BalloonY    = START_Y
VelX        = 0
VelY        = 0
Obstacles   = {}
LastSpawnMs = 0
Crashes     = 0
State       = "flying"
CrashMs     = 0
FlameUntilMs = 0

function SpawnObstacle()
  local r = 10 + math.random(0, 8)
  local y = PLAY_TOP + r + 8 + math.random(0, GROUND_Y - PLAY_TOP - 2 * (r + 8))
  table.insert(Obstacles, { x = 322 + r, y = y, r = r, kind = math.random(0, 1) })
end

function UpdateSteering(Ms, MsDelta)
  local jx, jy = GetJoystickXY()
  if math.abs(jx) < 0.12 and math.abs(jy) < 0.12 then
    jx, jy = 0, 0
  end
  local tvx, tvy = jx * MAX_SPEED, jy * MAX_SPEED
  if IsTouchInZone(0, PLAY_TOP, 320, PLAY_BOTTOM - PLAY_TOP) then
    tvx = Constrain(GetTouchX() - BalloonX, -MAX_SPEED, MAX_SPEED)
    tvy = Constrain(GetTouchY() - BalloonY, -MAX_SPEED, MAX_SPEED)
  end
  local mag = math.min(math.sqrt(jx * jx + jy * jy), 1)
  if IsTouchInZone(0, PLAY_TOP, 320, PLAY_BOTTOM - PLAY_TOP) then
    mag = math.min(math.sqrt(tvx * tvx + tvy * tvy) / MAX_SPEED, 1)
  end
  local k = 1 - math.exp(-MsDelta / 180)
  VelX = VelX + (tvx - VelX) * k
  VelY = VelY + (tvy - VelY) * k
  return mag
end

function DrawCloud(x, y, r)
  local s = r / 10
  DrawSprite(SCloud, x, y, { scaleX = s, scaleY = s, flags = ALIGN_H_CENTER | ALIGN_V_CENTER })
end

function DrawBird(x, y, r)
  local s = r / 16
  DrawSprite(SBird, x, y, {
    scaleX = s, scaleY = s,
    frame = (Ms // 180) % 4,
    flags = ALIGN_H_CENTER | ALIGN_V_CENTER,
  })
end

function DrawBalloon(Ms)
  local lit = CurrentlyBlowing or Ms < FlameUntilMs
  DrawSprite(State == "crashing" and SBalloonDead or BalloonSkin[SKIN + 1],
             BalloonX - ENV_CX, BalloonY - ENV_CY)
  if lit then
    DrawSprite(SFlame, BalloonX - FLAME_DX, BalloonY + FLAME_DY,
               { frame = (Ms // 180) % 2 })
  end
end

function DrawFuelGauge()
  DrawSprite(SGauge, GAUGE_X, GAUGE_Y)
  local h = (Fuel * 46) // FUEL_MAX
  FillRect(GAUGE_X + 3, GAUGE_Y + 49 - h, 16, h, Fuel < 35 and COL_FUEL_LOW or COL_FUEL_OK)
  SetTextColor(COL_WHITE)
  SetTextSize(1)
  DrawString("Tank", GAUGE_X, GAUGE_Y + 52)
end
