
local mag = UpdateSteering(Ms, MsDelta)

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
      end
    end
  end
end

if State == "flying" then
  GroundOffset = GroundOffset + DRIFT_SPEED * (MsDelta / 1000)
  Distance = GroundOffset + (BalloonX - START_X)
  if Distance < 0 then Distance = 0 end
  local earned = Distance // DIST_PER_COIN
  if earned > EarnedSaved then
    PrefsSetInt("coins", PrefsGetInt("coins") + (earned - EarnedSaved))
    EarnedSaved = earned
  end
end

if NewRepetition then
  Fuel = Fuel + FUEL_PER_BLOW
  FlameUntilMs = Ms + 900
  VelY = VelY - 20
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

if State == "flying" then
  BalloonX = Constrain(BalloonX + VelX * (MsDelta / 1000), 30, 285)
  BalloonY = Constrain(BalloonY + VelY * (MsDelta / 1000),
                       PLAY_TOP + BALLOON_R + 2, GROUND_Y - BASKET_DROP)
else
  VelY = VelY + 160 * (MsDelta / 1000)
  BalloonY = BalloonY + VelY * (MsDelta / 1000)
  if BalloonY > GROUND_Y - BASKET_DROP then BalloonY = GROUND_Y - BASKET_DROP end
  if Ms - CrashMs > CRASH_MS then
    Fuel = FUEL_START
    GroundOffset = GroundOffset + (BalloonX - START_X)
    BalloonX, BalloonY = START_X, START_Y
    VelX, VelY = 0, 0
    Obstacles = {}
    LastSpawnMs = Ms
    FlameUntilMs = 0
    State = "flying"
  end
end

DrawSprite(SBackground, 0, PLAY_TOP)

for i = GroundOffset // GROUND_SPACING, (GroundOffset + 320) // GROUND_SPACING do
  DrawSprite(STuft, i * GROUND_SPACING - GroundOffset, GROUND_Y + 4)
end

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
DrawString("Distanz: " .. (Distance // PX_PER_M) .. " m", 232, PLAY_TOP + 2)

if State == "crashing" then
  SetTextSize(3)
  SetTextColor(COL_FUEL_LOW)
  DrawString("ABGESTÜRZT!", 28, 60)
elseif Fuel < 25 and (Ms // 500) % 2 == 0 then
  SetTextColor(COL_FUEL_LOW)
  DrawString("Wenig Tank - blasen!", 110, PLAY_BOTTOM - 8)
end
SetTextSize(1)
