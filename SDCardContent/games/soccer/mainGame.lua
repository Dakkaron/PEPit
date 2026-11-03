-- Soccer Penalty — per-frame game logic (runs in all five blow phases).
-- Injected globals: CurrentlyBlowing, Ms, MsDelta, BlowStartMs,
-- CurrentRepetition, NewRepetition, Repetitions, ... (gameLua.cpp).
-- The ball is struck on NewRepetition: the firmware's one-frame pulse on
-- every blow that reaches its target duration (blow END, not blow start).

-- ---------------------------------------------------------------- state
-- Scoreboard log: detect a new task (the firmware resets blowCount per task,
-- so NewRepetition is NOT a reliable boundary signal) and log reps that end
-- without a shot.
local repNow = CurrentRepetition or 0
local totalNow = Repetitions or 1
if repNow < PrevRep or totalNow ~= PrevTotal or (repNow == 1 and PrevRep ~= 1) then
  KickLog = {}
  PrevRep, PrevTotal = repNow, totalNow
else
  if repNow > PrevRep and State ~= "shooting" and #KickLog < repNow - 1 then
    KickLog[#KickLog + 1] = 3
  end
  PrevRep = repNow
end

-- The ball is struck the moment a blow FINISHES: the firmware pulses
-- NewRepetition for one frame on every blow that reaches its target
-- duration (a failed blow pulses nothing, so that rep simply waits to be
-- blown again). The kick may interrupt any animation in flight: short
-- tasks allow a new blow before the previous kick has finished, and every
-- blow gets its own shot (the flight always restarts from the spot).
if NewRepetition then
  ShotTX, ShotTY = AimX, AimY
  KeeperTX, KeeperTY = SampleKeeperTarget(Ms)
  KeeperStartX, KeeperStartY = KeeperX, KeeperY
  ShotStartMs = Ms
  SavedLast = false
  State = "shooting"
end

-- Aim always tracks the input: the shot is only set when the blow FINISHES
-- (NewRepetition), so re-aiming at any point — including mid-blow and
-- mid-flight — moves where the next ball goes.
UpdateAim(Ms, MsDelta)

-- ------------------------------------------------------------ animation
if State == "idle" then
  BallX, BallY, BallR = SPOT_X, SPOT_Y, BALL_R
  -- keeper eases home
  KeeperX = lerp(KeeperX, KEEPER_HOME_X, 1 - math.exp(-MsDelta / 400))
  KeeperY = lerp(KeeperY, KEEPER_HOME_Y, 1 - math.exp(-MsDelta / 400))
elseif State == "shooting" then
  local t = (Ms - ShotStartMs) / FLIGHT_MS
  if t >= 1 then
    t = 1
    local dx = ShotTX - KeeperTX
    local dy = ShotTY - KeeperTY
    SavedLast = math.sqrt(dx * dx + dy * dy) <= SAVE_RADIUS
    if SavedLast then
      Saves = Saves + 1
      KickLog[#KickLog + 1] = 2
    else
      Goals = Goals + 1
      KickLog[#KickLog + 1] = 1
    end
    ResultMs = Ms
    State = "result"
  end
  BallX = lerp(SPOT_X, ShotTX, t)
  BallY = lerp(SPOT_Y, ShotTY, t) - math.sin(t * math.pi) * 16  -- a little arc
  BallR = lerp(BALL_R, 4, t)  -- the ball "goes into" the goal
  local kt = (Ms - ShotStartMs) / DIVE_MS
  if kt > 1 then kt = 1 end
  local ke = kt * kt * (3 - 2 * kt)  -- smoothstep
  KeeperX = lerp(KeeperStartX, KeeperTX, ke)
  KeeperY = lerp(KeeperStartY, KeeperTY, ke)
elseif State == "result" then
  BallX, BallY = ShotTX, ShotTY
  KeeperX, KeeperY = KeeperTX, KeeperTY
  if Ms - ResultMs > RESULT_MS then
    ResetMs = Ms
    State = "reset"
  end
elseif State == "reset" then
  local t = (Ms - ResetMs) / RESET_MS
  if t >= 1 then
    t = 1
    State = "idle"
  end
  BallX = lerp(ShotTX, SPOT_X, t)
  BallY = lerp(ShotTY, SPOT_Y, t)
  BallR = lerp(4, BALL_R, t)
  KeeperX = lerp(KeeperTX, KEEPER_HOME_X, t)
  KeeperY = lerp(KeeperTY, KEEPER_HOME_Y, t)
end

-- --------------------------------------------------------------- drawing
-- backdrop: stands behind the goal, grass in front (the scoreboard bar
-- drawn last covers its top strip)
DrawSprite(SBackground, 0, SBAR_H)

-- goal: posts and net from the sprite, aligned with the game geometry
DrawSprite(SGoal, GOAL_X0 - POST_W, GOAL_TOP - POST_W)
DrawFastHLine(30, GOAL_BOTTOM, 260, COL_FRAME)  -- goal line

-- keeper: bone-animated dive (ke = dive progress, dir = dive side)
local ke, dir
if State == "shooting" then
  local kt = (Ms - ShotStartMs) / DIVE_MS
  if kt > 1 then kt = 1 end
  ke = kt * kt * (3 - 2 * kt)
  dir = KeeperTX >= KeeperStartX and 1 or -1
elseif State == "result" then
  ke, dir = 1, KeeperTX >= KEEPER_HOME_X and 1 or -1
elseif State == "reset" then
  local t = (Ms - ResetMs) / RESET_MS
  if t > 1 then t = 1 end
  ke, dir = 1 - t, KeeperTX >= KEEPER_HOME_X and 1 or -1
else
  ke, dir = 0, 1
end
DrawKeeper(KeeperX, KeeperY, ke, dir)

-- ball: rolling spin frames, shrinking into the goal
local spin = State == "idle" and 0 or ((Ms - ShotStartMs) // 150) % 4
DrawSprite(SBall, BallX, BallY, {scaleX = BallR / 8, scaleY = BallR / 8,
                                 frame = spin, flags = CENTER})

-- aim crosshair (idle only)
if State == "idle" then
  DrawLine(AimX - 8, AimY, AimX + 8, AimY, COL_CROSS)
  DrawLine(AimX, AimY - 8, AimX, AimY + 8, COL_CROSS)
end

-- result banner
if State == "result" then
  SetTextSize(3)
  if SavedLast then
    SetTextColor(COL_SAVETX)
    DrawString("SAVED!", 118, 138)
  else
    SetTextColor(COL_GOALTX)
    DrawString("GOAL!", 122, 138)
  end
  SetTextSize(1)
end

-- TV-style scoreboard (replaces the old "G:.. S:.." / "Rep N/M" text HUD)
DrawScoreboard()
