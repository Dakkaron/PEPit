DisableCaching()
speedDrop = (1 - (0.0001 * MsDelta))
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

lastX = 0
lastY = 0
x = 0
y = 0
w = 0
if (RoadXOffset<-160) then
  RoadXOffsetDirection = 1
elseif (RoadXOffset>160) then
  RoadXOffsetDirection = -1
end
RoadXOffset = RoadXOffset + RoadXOffsetDirection

-- pavementA, pavementB, embankmentA, embankmentB, grassA, grassB, wallA, wallB, railA, railB, lamp, mountain, centerline
SetRoadColors(0xa38a, 0xbbeb, 0x8c0f, 0x8c0f, 0x75a4, 0x8ea4, 0x5aeb, 0x8410, 0xbdf7, 0xce59, 0xbdf7, 0xce59, 0xffff, 0x7bef, 0xffff)
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

angle = RoadXOffset * 0.0015
for d = Distance%10 - 10, 50, 10 do
  if (50-d > 0) then
    drawSpriteToRoad(STree, ROAD_W+100, 50-d, 4)
    drawSpriteToRoad(STree, -ROAD_W-100, 50-d, 4)
  end
end

position = 1
for i = 1, #(Competitors.name) do
  Competitors.distance[i] = Competitors.distance[i] + MsDelta * Competitors.speed[i]
  Competitors.distance[i] = Constrain(Competitors.distance[i], Distance - 20, Distance + 55)
  if (Competitors.distance[i] > Distance) then
    position = position + 1
  end
  drawHorse(SHorse, Competitors.xpos[i], (2.5+Competitors.distance[i]-Distance), 0, 1, 1, angle, (Ms/150)%4, Competitors.name[i])
end

if (PlayerX <= ROAD_W*0.5 and PlayerX >= -ROAD_W*0.5) then
  drawHorse(SHorse, PlayerX, 2.5, 0, 1, 1, angle, (Ms/150)%4)
elseif (PlayerX <= ROAD_W*0.5 + BANK_W and PlayerX >= -ROAD_W*0.5 - BANK_W) then
  Speed = Speed * speedDrop
  if (Speed>0.008) then
    Speed = 0.008
  end
  drawHorse(SHorse, PlayerX, 2.5, 2.5-(Ms%2)*5, 1, 1, angle, (Ms/150)%4)
else
  Speed = Speed * speedDrop * speedDrop
  if (Speed>0.006) then
    Speed = 0.006
  end
  drawHorse(SHorse, PlayerX, 2.5, 4-(Ms%2)*8, 1, 1, angle, (Ms/150)%4)
end

FillRect(0, BASELINE_Y, 320, 240-BASELINE_Y, 0x0000)

DrawString("Distance: " .. Distance, 100, 10)
DrawString("PlayerX:  " .. PlayerX, 100, 20)
DrawString("Speed:    " .. Speed, 100, 30)
SetTextSize(2)
SetTextDatum(2)
DrawString(position .. " / " .. #(Competitors.name), 310, 220)
SetTextDatum(0)
SetTextSize(1)
