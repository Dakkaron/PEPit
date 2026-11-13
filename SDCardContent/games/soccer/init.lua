
function rgb(r, g, b)
  return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
end

COL_FRAME  = rgb(200, 208, 220)
COL_CROSS  = rgb(255, 196, 0)
COL_GOALTX = rgb(64, 230, 96)
COL_SAVETX = rgb(238, 48, 48)
COL_WHITE  = 0xFFFF

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
SThighL     = LoadSprite("gfx/leg_thigh.bmp", 0, MASK)
SThighR     = LoadSprite("gfx/leg_thigh_r.bmp", 0, MASK)
SShinL      = LoadSprite("gfx/leg_shin.bmp", 0, MASK)
SShinR      = LoadSprite("gfx/leg_shin_r.bmp", 0, MASK)

SHOULDER_DX  = 9
SHOULDER_DY  = -17
ARM_UP_LEN   = 9
ARM_FORE_LEN = 5
DIVE_PHI     = 1.0
DIVE_UP      = 0.6
ARM_FOLD     = 0.35

HIP_DX       = 6
LEG_UP_LEN   = 7
LEG_SHIN_LEN = 14
LEG_IDLE_UP_L, LEG_IDLE_UP_R = 0.767, -0.758
LEG_IDLE_SHIN_L, LEG_IDLE_SHIN_R = 0.377, -0.379
LEG_FOLD     = 0.45

function DiveDir(dx)
  if dx > 8 then return 1 end
  if dx < -8 then return -1 end
  return 0
end

function DrawKeeper(x, y, ke, dir)
  local phi = dir * DIVE_PHI * ke
  local cs, ss = math.cos(phi), math.sin(phi)
  local up = ke * (math.pi / 2 + DIVE_UP)
  local fo = up + ARM_FOLD * ke
  local dxL, dyL = -SHOULDER_DX, SHOULDER_DY
  local dxR, dyR = SHOULDER_DX, SHOULDER_DY
  local shLx = x + cs * dxL - ss * dyL
  local shLy = y + ss * dxL + cs * dyL
  local shRx = x + cs * dxR - ss * dyR
  local shRy = y + ss * dxR + cs * dyR
  local thL = lerp(LEG_IDLE_UP_L, phi, ke)
  local thR = lerp(LEG_IDLE_UP_R, phi, ke)
  local snL = lerp(LEG_IDLE_SHIN_L, phi + dir * LEG_FOLD, ke)
  local snR = lerp(LEG_IDLE_SHIN_R, phi + dir * LEG_FOLD, ke)
  local hipLx, hipLy = x - cs * HIP_DX, y - ss * HIP_DX
  local hipRx, hipRy = x + cs * HIP_DX, y + ss * HIP_DX
  DrawSprite(SKeeperBody, x, y, {angle = phi, flags = CENTER})
  DrawSprite(SThighL, hipLx, hipLy, {angle = thL, flags = CENTER})
  DrawSprite(SThighR, hipRx, hipRy, {angle = thR, flags = CENTER})
  DrawSprite(SShinL, hipLx - LEG_UP_LEN * math.sin(thL), hipLy + LEG_UP_LEN * math.cos(thL),
            {angle = snL, flags = CENTER})
  DrawSprite(SShinR, hipRx - LEG_UP_LEN * math.sin(thR), hipRy + LEG_UP_LEN * math.cos(thR),
            {angle = snR, flags = CENTER})
  DrawSprite(SArmUpL, shLx, shLy, {angle = up, flags = CENTER})
  DrawSprite(SArmUpR, shRx, shRy, {angle = -up, flags = CENTER})
  DrawSprite(SArmForeL, shLx - ARM_UP_LEN * math.sin(up), shLy + ARM_UP_LEN * math.cos(up),
            {angle = fo, flags = CENTER})
  DrawSprite(SArmForeR, shRx + ARM_UP_LEN * math.sin(up), shRy + ARM_UP_LEN * math.cos(up),
            {angle = -fo, flags = CENTER})
  DrawSprite(SGloveL, shLx - (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fo),
            shLy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fo), {angle = fo, flags = CENTER})
  DrawSprite(SGloveR, shRx + (ARM_UP_LEN + ARM_FORE_LEN) * math.sin(fo),
            shRy + (ARM_UP_LEN + ARM_FORE_LEN) * math.cos(fo), {angle = -fo, flags = CENTER})
end

COL_SBAR   = rgb(8, 12, 26)
COL_SBX    = rgb(96, 108, 140)
COL_SBFLY  = rgb(120, 176, 255)
COL_SBSAVE = rgb(16, 22, 40)
SBAR_H = 50
SB_NAME_Y = 21
SB_BOX_Y = 38
SB_PITCH = 13

GOAL_X0 = 62
GOAL_X1 = 256
GOAL_TOP = 62
GOAL_BOTTOM = 124
AIM_X_RANGE = (GOAL_X1 - GOAL_X0) / 2 - 8
AIM_Y_RANGE = (GOAL_BOTTOM - GOAL_TOP) / 2 - 6
GOAL_MID_X = (GOAL_X0 + GOAL_X1) / 2
GOAL_MID_Y = (GOAL_TOP + GOAL_BOTTOM) / 2

SPOT_X = 160
SPOT_Y = 152

KEEPER_FOOT_DROP = 18
KEEPER_HOME_X = GOAL_MID_X
KEEPER_HOME_Y = GOAL_BOTTOM - KEEPER_FOOT_DROP
KEEPER_DIVE_MIN_Y = GOAL_TOP + 18
KEEPER_DIVE_MAX_Y = GOAL_BOTTOM - KEEPER_FOOT_DROP

BALL_R = 7
SAVE_RADIUS = 30
FLIGHT_MS = 350
DIVE_MS = 300
RESULT_MS = 2500
RESET_MS = 3000
KEEPER_RETURN_MS = 900
AIM_DEADBAND = 0.12
AIM_SAMPLE_MS = 100
RING_KEEP = 8
KEEPER_SAMPLE_MS = 500

State = "idle"
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
AimRing = {}
AimRingN = 0

KickLog = {}
PrevRep = 1
PrevTotal = 1

function UpdateAim(Ms, MsDelta)
  local jx, jy = GetJoystickXY()
  local tx, ty
  if IsTouchInZone(0, GOAL_TOP - 20, 320, 160) then
    tx = Constrain(GetTouchX(), GOAL_X0 + 8, GOAL_X1 - 8)
    ty = Constrain(GetTouchY(), GOAL_TOP + 6, GOAL_BOTTOM - 6)
  elseif math.abs(jx) > AIM_DEADBAND or math.abs(jy) > AIM_DEADBAND then
    tx = GOAL_MID_X + jx * AIM_X_RANGE
    ty = GOAL_MID_Y + jy * AIM_Y_RANGE
  else
    tx = GOAL_MID_X
    ty = GOAL_MID_Y
  end
  local k = 1 - math.exp(-MsDelta / 120)
  AimX = AimX + (tx - AimX) * k
  AimY = AimY + (ty - AimY) * k

  if AimRingN == 0 or Ms - AimRing[AimRingN].ms >= AIM_SAMPLE_MS then
    table.insert(AimRing, {ms = Ms, x = AimX, y = AimY})
    AimRingN = AimRingN + 1
    if AimRingN > RING_KEEP then
      table.remove(AimRing, 1)
      AimRingN = AimRingN - 1
    end
  end
end

function CommitKeeperTarget(Ms)
  local want = Ms - KEEPER_SAMPLE_MS
  local best = nil
  for i = 1, #AimRing do
    local s = AimRing[i]
    if best == nil or math.abs(s.ms - want) < math.abs(best.ms - want) then
      best = s
    end
  end
  if best == nil then
    KeeperCommitX, KeeperCommitY = AimX, AimY
  else
    KeeperCommitX, KeeperCommitY = best.x, best.y
  end
end

function lerp(a, b, t)
  return a + (b - a) * t
end

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

function DrawScoreboard()
  FillRect(0, 0, 320, SBAR_H, COL_SBAR)
  DrawFastHLine(0, SBAR_H - 1, 320, COL_SBX)

  local n = Repetitions or 10
  if n < 1 then n = 1 end
  local rep = CurrentRepetition or 0
  local pitch = SB_PITCH
  if n * pitch > 296 then pitch = math.floor(296 / n) end
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

  SetTextSize(2)
  SetTextColor(COL_WHITE)
  DrawString("YOU", 8, SB_NAME_Y)
  local sc = Goals .. " : " .. Saves
  DrawString(sc, 160 - (#sc * 10) // 2, SB_NAME_Y)
  SetTextSize(1)
end
