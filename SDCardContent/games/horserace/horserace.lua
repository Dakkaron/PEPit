local speedDrop = (1 - (0.0001 * MsDelta))
Speed = Speed * speedDrop
if (NewRepetition) then
  Speed = Speed + 0.0075 + 0.0005*PlayerHorse.speed
end

local distanceDelta = 0.0
local speedLimit = (5 + PlayerHorse.speed) * 0.0005
distanceDelta = MsDelta * math.min(Speed, speedLimit)
Distance = Distance + distanceDelta
PlayerX = PlayerX - distanceDelta * RoadXOffset * 0.03
PlayerX = PlayerX * speedDrop

local xShift = MsDelta * 0.07 * GetJoystickX() * 1.4 -- 1.4 is a temporary correction for not enough stick range of motion
if GetJoystickButton() and PlayerHorse.jumpEnd<Ms then
  PlayerHorse.jumpEnd = Ms + 750
end
if LeftHandedMode then
  if IsTouchInZone(0, 0, 50, 69) and PlayerHorse.jumpEnd<Ms then
    PlayerHorse.jumpEnd = Ms + 750
  elseif IsTouchInZone(0, 80, 50, 44) then
    xShift = -MsDelta * 0.07
  elseif IsTouchInZone(0, 135, 50, 44) then
    xShift = MsDelta * 0.07
  end
else
  if IsTouchInZone(0, 0, 50, 69) and PlayerHorse.jumpEnd<Ms then
    PlayerHorse.jumpEnd = Ms + 750
  elseif IsTouchInZone(270, 80, 50, 44) then
    xShift = -MsDelta * 0.07
  elseif IsTouchInZone(270, 135, 50, 44) then
    xShift = MsDelta * 0.07
  end
end
PlayerX = PlayerX + xShift

PlayerX = Constrain(PlayerX, -160, 160)

local lastX = 0.0
local lastW = 0.0
local roadYOffset = 0.0
local x = 0.0
local w = 0.0
if Obstacle.distance-Distance < 35 then
	if (RoadXOffset<0) then
    RoadXOffsetDirection = 1
  elseif (RoadXOffset>0) then
    RoadXOffsetDirection = -1
  end
else
  if (RoadXOffset<-160) then
    RoadXOffsetDirection = 1
  elseif (RoadXOffset>160) then
    RoadXOffsetDirection = -1
  end
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

if not Obstacle.used and math.abs(Obstacle.distance-Distance) < 1 and math.abs(Obstacle.xpos-PlayerX) < 50 then
  Obstacle.used = true
  if PlayerHorse.jumpEnd>Ms then -- Jump successful
    AddEarnings(OBSTACLE_REWARD)
    Obstacle.succeeded = true
  else -- Jump failed
    Speed = Speed * 0.75
    Obstacle.succeeded = false
  end
elseif Obstacle.distance-Distance < -10 then
  Obstacle.distance = Distance + math.random(50, 100)
  Obstacle.used = false
end

if Obstacle.used and not Obstacle.succeeded then
  DrawSpriteToRoad(SObstacleBroken, Obstacle.xpos, 2.5+Obstacle.distance-Distance, 1.5, 1)
else
  DrawSpriteToRoad(SObstacle, Obstacle.xpos, 2.5+Obstacle.distance-Distance, 1.5, 1)
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

local animFrame = (PlayerHorse.jumpEnd>Ms) and (math.floor((Ms-PlayerHorse.jumpEnd)*0.0039)%3)+4 or Distance%4
local jumpY = (PlayerHorse.jumpEnd>Ms) and -math.sin(math.pi * (PlayerHorse.jumpEnd-Ms) /750) * 25 or 0
if (PlayerX <= ROAD_W*0.5 and PlayerX >= -ROAD_W*0.5) then
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, jumpY, 1, 1, angle, animFrame)
elseif (PlayerX <= ROAD_W*0.5 + BANK_W and PlayerX >= -ROAD_W*0.5 - BANK_W) then
  Speed = Speed * speedDrop
  if (Speed>0.008) then
    Speed = 0.008
  end
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 2.5-(Ms%2)*5 + jumpY, 1, 1, angle, animFrame)
else
  Speed = Speed * speedDrop * speedDrop
  if (Speed>0.006) then
    Speed = 0.006
  end
  DrawHorse(PlayerHorse.sprite, PlayerX, 2.5, 4-(Ms%2)*8 + jumpY, 1, 1, angle, animFrame)
end

DrawSpriteQueue()
DisplayEarnings(PlayerX+180, 70)

FillRect(0, BASELINE_Y, 320, 240-BASELINE_Y, 0x0000)

SetTextSize(2)
SetTextDatum(2)
DrawString(Position .. " / " .. (#Competitors + 1), 310, 220)
SetTextDatum(0)
SetTextSize(1)

if LeftHandedMode then
  if IsTouchInZone(0, 0, 50, 69) then
    DrawSpriteScaledRotated(SJump, 27, 46, 1, 1, -0.5, 0x05)
  else
    DrawSprite(SJump, 5, 25)
  end

  if IsTouchInZone(0, 80, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 27, 101, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 5, 80)
  end

  if IsTouchInZone(0, 135, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 27, 156, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 5, 135)
  end
else
  if IsTouchInZone(270, 0, 50, 69) then
    DrawSpriteScaledRotated(SJump, 293, 46, 1, 1, -0.5, 0x05)
  else
    DrawSprite(SJump, 271, 25)
  end

  if IsTouchInZone(270, 80, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 293, 101, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 271, 80)
  end

  if IsTouchInZone(270, 135, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 293, 156, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 271, 135)
  end
end
