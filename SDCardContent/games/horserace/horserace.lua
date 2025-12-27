DisableCaching()
local speedDrop = (1 - (0.0001 * MsDelta))
Speed = Speed * speedDrop
if (NewRepetition) then
  Speed = Speed + 0.015
end

if (Speed < 0.012) then
  Distance = Distance + MsDelta * Speed
  PlayerX = PlayerX - MsDelta * RoadXOffset * 0.0003
else
  Distance = Distance + MsDelta * 0.012
  PlayerX = PlayerX - MsDelta * RoadXOffset * 0.0003
end
PlayerX = PlayerX * speedDrop

if (IsTouchInZone(0, 0, 100, 200)) then
  PlayerX = PlayerX - MsDelta * 0.07
elseif (IsTouchInZone(220, 0, 100, 200)) then
  PlayerX = PlayerX + MsDelta * 0.07
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
  Competitors[i].distance = Competitors[i].distance + MsDelta * Competitors[i].speed
  Competitors[i].distance = Constrain(Competitors[i].distance, Distance - 20, Distance + 55)
  if (Competitors[i].distance > Distance) then
    Position = Position + 1
  end
  DrawHorse(SHorse[i], Competitors[i].xpos, (2.5+Competitors[i].distance-Distance), 0, 1, 1, angle, (Ms/150)%4, CompetitorNames[Competitors[i].nr])
end

if (PlayerX <= ROAD_W*0.5 and PlayerX >= -ROAD_W*0.5) then
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 0, 1, 1, angle, (Ms/150)%4)
elseif (PlayerX <= ROAD_W*0.5 + BANK_W and PlayerX >= -ROAD_W*0.5 - BANK_W) then
  Speed = Speed * speedDrop
  if (Speed>0.008) then
    Speed = 0.008
  end
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 2.5-(Ms%2)*5, 1, 1, angle, (Ms/150)%4)
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
