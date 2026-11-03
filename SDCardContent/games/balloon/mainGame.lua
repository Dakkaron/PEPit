-- Hot Air Balloon — per-frame game logic (runs in all five blow phases).
-- Injected globals: CurrentlyBlowing, Ms, MsDelta, NewRepetition, ...
-- (gameLua.cpp). Fuel is added on NewRepetition: the firmware's one-frame
-- pulse on every blow that reaches its target duration (blow END, not
-- blow start, same semantics as soccer).

-- ------------------------------------------------------------ steering
local mag = UpdateSteering(Ms, MsDelta)

-- ------------------------------------------------------------ obstacles
if State == "flying" then
  if Ms - LastSpawnMs >= SPAWN_MS then
    LastSpawnMs = Ms
    SpawnObstacle()
  end

  for i = #Obstacles, 1, -1 do
    local o = Obstacles[i]
    o.x = o.x - DRIFT_SPEED * (MsDelta / 1000)
    if o.x < -o.r - 2 then
      table.remove(Obstacles, i)
    else
      local dx = o.x - BalloonX
      local dy = o.y - BalloonY
      if math.sqrt(dx * dx + dy * dy) <= BALLOON_R + o.r - 3 then
        Fuel = Fuel - HIT_COST
        table.remove(Obstacles, i)
      elseif not o.passed and o.x < BalloonX - o.r then
        o.passed = true
        Passed = Passed + 1
      end
    end
  end
end

-- ------------------------------------------------------------ fuel
if NewRepetition then
  Fuel = Fuel + FUEL_PER_BLOW      -- the completed blow stoked the flames
  FlameUntilMs = Ms + 900
  VelY = VelY - 20                -- a little lift with the new heat
end
if CurrentlyBlowing then
  Fuel = Fuel + BLOW_GAIN * (MsDelta / 1000)
end
Fuel = Fuel - (IDLE_DRAIN + STEER_DRAIN * mag) * (MsDelta / 1000)
if Fuel > FUEL_MAX then Fuel = FUEL_MAX end

if State == "flying" and Fuel <= 0 then
  Fuel = 0
  Crashes = Crashes + 1
  CrashMs = Ms
  State = "crashing"
end

-- ------------------------------------------------------------ motion
if State == "flying" then
  BalloonX = Constrain(BalloonX + VelX * (MsDelta / 1000), 30, 285)
  BalloonY = Constrain(BalloonY + VelY * (MsDelta / 1000),
                       PLAY_TOP + BALLOON_R + 2, PLAY_BOTTOM - BALLOON_R)
else
  -- the balloon drops out of the sky, then the run resets
  VelY = VelY + 160 * (MsDelta / 1000)
  BalloonY = BalloonY + VelY * (MsDelta / 1000)
  if BalloonY > PLAY_BOTTOM then BalloonY = PLAY_BOTTOM end
  if Ms - CrashMs > CRASH_MS then
    Fuel = FUEL_START
    BalloonX, BalloonY = START_X, START_Y
    VelX, VelY = 0, 0
    Obstacles = {}
    LastSpawnMs = Ms
    FlameUntilMs = 0
    State = "flying"
  end
end

-- --------------------------------------------------------------- drawing
FillRect(0, PLAY_TOP, 320, PLAY_BOTTOM - PLAY_TOP + 1, COL_SKY)
FillRect(0, GROUND_Y, 320, PLAY_BOTTOM - GROUND_Y + 1, COL_HAZE)

for _, o in ipairs(Obstacles) do
  if o.kind == 0 then
    DrawCloud(o.x, o.y, o.r)
  else
    DrawBird(o.x, o.y, o.r)
  end
end

DrawBalloon(Ms)
DrawFuelGauge()

SetTextSize(1)
SetTextColor(COL_WHITE)
DrawString("Passiert: " .. Passed, 232, PLAY_TOP + 2)

if State == "crashing" then
  SetTextSize(3)
  SetTextColor(COL_FUEL_LOW)
  DrawString("ABGESTÜRZT!", 28, 60)
elseif Fuel < 25 and (Ms // 500) % 2 == 0 then
  SetTextColor(COL_FUEL_LOW)
  DrawString("Wenig Tank - blasen!", 110, PLAY_BOTTOM - 8)
end
SetTextSize(1)
