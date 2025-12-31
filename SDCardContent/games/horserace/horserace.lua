DisableCaching()
local speedDrop = (1 - (0.0001 * MsDelta))
Speed = Speed * speedDrop
if (NewRepetition) then
  Speed = Speed + 0.015 + 0.001*PlayerHorse.speed
end

local distanceDelta = 0.0
local speedLimit = (10 + PlayerHorse.speed) * 0.001
distanceDelta = MsDelta * math.min(Speed, speedLimit)
Distance = Distance + distanceDelta
PlayerX = PlayerX - distanceDelta * RoadXOffset * 0.03
PlayerX = PlayerX * speedDrop

if LeftHandedMode then
  if IsTouchInZone(0, 50, 50, 44) then
    PlayerX = PlayerX - MsDelta * 0.07
  elseif IsTouchInZone(0, 120, 50, 44) then
    PlayerX = PlayerX + MsDelta * 0.07
  end
else
  if IsTouchInZone(270, 50, 50, 44) then
    PlayerX = PlayerX - MsDelta * 0.07
  elseif IsTouchInZone(270, 120, 50, 44) then
    PlayerX = PlayerX + MsDelta * 0.07
  end
end

PlayerX = Constrain(PlayerX, -160, 160)

local lastX = 0.0
local lastW = 0.0
local roadYOffset = 0.0
local x = 0.0
local w = 0.0
if (RoadXOffset<-160) then
  RoadXOffsetDirection = 1
elseif (RoadXOffset>160) then
  RoadXOffsetDirection = -1
end
RoadXOffset = RoadXOffset + RoadXOffsetDirection

-- pavementA, pavementB, embankmentA, embankmentB, grassA, grassB, wallA, wallB, railA, railB, lamp, mountain, centerline
SetRoadColors(0xa38a, 0xbbeb, 0x8c0f, 0x8c0f, 0x75a4, 0x8ea4, 0x5aeb, 0x8410, 0xbdf7, 0xce59, 0xbdf7, 0xce59, 0xffff)
SetRoadDrawFlags(false, false, false, false, false)
SetRoadDimensions(ROAD_W, BANK_W, 10, 10, 20, 5, 150)

DrawSprite(SSky, RoadXOffset, 0)
if (SpriteWidth(SSky) + RoadXOffset < 320) then
  DrawSprite(SSky, RoadXOffset + SpriteWidth(SSky), 0)
elseif (RoadXOffset > 0) then
  DrawSprite(SSky, RoadXOffset - SpriteWidth(SSky), 0)
end

for y = DRAW_HORIZON, BASELINE_Y do
  lastX, lastW = x, w
  x, w, roadYOffset = CalculateRoadProperties(y, Distance, HORIZON_Y, BASELINE_Y, RoadXOffset)
  DrawRaceOutdoor(x, y, w, roadYOffset, lastX, lastW)
end

local angle = RoadXOffset * 0.0015
for d = Distance%10 - 10, 50, 10 do
  if 48-d > 0 then
    DrawSpriteToRoad(STree, ROAD_W+100, 50-d, 4)
    DrawSpriteToRoad(STree, -ROAD_W-100, 50-d, 4)
  end
end

Position = 1
for i = 1, #(Competitors) do
  local c = Competitors[i]
  c.speed = c.speed * speedDrop + c.targetSpeed * (1-speedDrop)
  c.distance = c.distance + MsDelta * c.speed
  c.distance = Constrain(c.distance, Distance - 20, Distance + 55)
  if c.distance > Distance then
    Position = Position + 1
  end
  if c.distance <= Distance and c.distance > Distance - 0.5 and math.abs(c.xpos - PlayerX) < 20 then
    c.speed = Speed * 0.75
  elseif c.distance > Distance and c.distance < Distance + 0.5 and math.abs(c.xpos - PlayerX) < 20 then
    Speed = c.speed * 0.75
  end
  DrawHorse(SHorse[i], c.xpos, (2.5+c.distance-Distance), 0, 1, 1, angle, Distance%4, CompetitorNames[c.nr])
end

if (PlayerX <= ROAD_W*0.5 and PlayerX >= -ROAD_W*0.5) then
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 0, 1, 1, angle, Distance%4)
elseif (PlayerX <= ROAD_W*0.5 + BANK_W and PlayerX >= -ROAD_W*0.5 - BANK_W) then
  Speed = Speed * speedDrop
  if (Speed>0.008) then
    Speed = 0.008
  end
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 2.5-(Ms%2)*5, 1, 1, angle, Distance%4)
else
  Speed = Speed * speedDrop * speedDrop
  if (Speed>0.006) then
    Speed = 0.006
  end
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 4-(Ms%2)*8, 1, 1, angle, (Ms/150)%4)
end

FillRect(0, BASELINE_Y, 320, 240-BASELINE_Y, 0x0000)

SetTextSize(2)
SetTextDatum(2)
DrawString(Position .. " / " .. (#Competitors + 1), 310, 220)
SetTextDatum(0)
SetTextSize(1)

if LeftHandedMode then
  if IsTouchInZone(0, 50, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 27, 71, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 5, 50)
  end

  if IsTouchInZone(0, 120, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 27, 141, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 5, 120)
  end
else
  if IsTouchInZone(270, 50, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 293, 71, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 271, 50)
  end

  if IsTouchInZone(270, 120, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 293, 141, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 271, 120)
  end
end