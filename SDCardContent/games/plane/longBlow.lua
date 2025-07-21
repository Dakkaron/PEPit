SetTextSize(1)
CameraHeight = CameraHeight + (Speed-50) * MsDelta * 0.00001
if (DrowningStartMs == 0 and CameraHeight < 0.1 ) then
  DrowningStartMs = Ms
end
CameraHeight = Constrain(CameraHeight, 0.1, TopFlightHeight)
if (DrowningStartMs == 0) then
  if NewRepetition then
    Speed = Speed + 100 + 15*UpgradeSpeed
  end
  speedDrop = 1 - (0.0001 * MsDelta)
  Speed = Speed * speedDrop
  CameraX = CameraX - math.sin(CameraAngle) * Speed * 0.0001 * MsDelta
  CameraY = CameraY + math.cos(CameraAngle) * Speed * 0.0001 * MsDelta
end

local realHorizonHeight = HorizonHeight - 10 + CameraHeight*5
local skyXOffset = math.fmod(CameraAngle*160, SpriteWidth(SSky))
DrawSprite(SSky, skyXOffset, 0)
if (SpriteWidth(SSky) + skyXOffset < 320) then
  DrawSprite(SSky, skyXOffset + SpriteWidth(SSky), 0)
end
if (skyXOffset > 0) then
  DrawSprite(SSky, skyXOffset - SpriteWidth(SSky), 0)
end
DrawMode7(SBackground, CameraX, CameraY, CameraHeight, CameraAngle, 1, realHorizonHeight, 20, 180)

for i = 1, #Objects do
  Objects[i].d = calcDistance2D(CameraX, CameraY, Objects[i].x, Objects[i].y)
end

table.sort(Objects, function (left, right)
  return left.d > right.d
end)

local targetShipData = {}
local totalShipCount = 0
for i = 1, #Objects do
  local object = Objects[i]
  local objectType = object.type
  if (objectType == OBJECT_TYPE_SHIP) then
    drawShip(SSailShip, object.x, object.y)
    if object.id == TargetShip then
      targetShipData = object
    end
    totalShipCount = totalShipCount + 1
  elseif (objectType == OBJECT_TYPE_STORM and object.d < 200) then
    drawBillboard(SStorm, object.x, object.y, 7, 10)
  elseif (objectType == OBJECT_TYPE_BIRD and object.d < 1000) then
    mx,my = getObjectMotionVector(object)
    object.x = object.x + mx * BIRD_SPEED * 0.001 * MsDelta
    object.y = object.y + mx * BIRD_SPEED * 0.001 * MsDelta
    local dir = getObjectRelativeDirection(mx, my)
    drawBillboard(SBird, object.x, object.y, 10, 0.5, dir > 0 and true or false)
    local dx = object.targetX-object.x
    local dy = object.targetY-object.y
    local targetDistance = math.sqrt(dx*dx + dy*dy)
    if (targetDistance < 10) then
      object.targetX = math.random(10, 450)
      object.targetY = math.random(10, 450)
    end
  end
end

drawTarget(SSailShip, targetShipData.x, targetShipData.y)

for i = 1, #Objects do
  local object = Objects[i]
  if (object.type == OBJECT_TYPE_STORM and object.d < 40) then
    CameraHeight = CameraHeight - MsDelta * 0.0001 * 10 - (Speed-50) * MsDelta * 0.00001
    if (DrowningStartMs == 0 and CameraHeight < 0.1 ) then
      DrowningStartMs = Ms
    end
    CameraHeight = Constrain(CameraHeight, 0.1, TopFlightHeight)
  end
end

if (calcDistance2D(CameraX, CameraY, targetShipData.x, targetShipData.y) < 3 and (TopFlightHeight - CameraHeight) < (TopFlightHeight/10.0)) then
  TargetShip = TargetShip + 1
  AddEarnings(5)
  if (TargetShip > totalShipCount) then
    TargetShip = 1
  end
end

local planeScale = 0.85+CameraHeight/8
if (DrowningStartMs == 0) then
  if (IsTouchInZone(0, 0, 100, 240)) then
    DrawSpriteScaled(SPlaneBankL[PlaneType], 160, 120, planeScale, planeScale, 0x05)
    CameraAngle = CameraAngle + (0.001 + 0.0002*UpgradeTurn) * MsDelta
    CameraAngle = math.fmod(CameraAngle, math.pi*2)
  elseif (IsTouchInZone(220, 0, 100, 240)) then
    DrawSpriteScaled(SPlaneBankR[PlaneType], 160, 120, planeScale, planeScale, 0x05)
    CameraAngle = CameraAngle - (0.001 + 0.0002*UpgradeTurn) * MsDelta
    CameraAngle = math.fmod(CameraAngle, math.pi*2)
  else
    DrawSpriteScaled(SPlane[PlaneType], 160, 120, planeScale, planeScale, 0x05)
  end
else
  local drowningTime = Ms - DrowningStartMs
  if (drowningTime<150) then
    DrawSpriteScaled(SSplash[1], 160, 160, 1, drowningTime/200, 0x09)
  elseif (drowningTime<350) then
    DrawSpriteScaled(SSplash[2], 160, 160, 1, drowningTime/350, 0x09)
  elseif (drowningTime<2000) then
    DrawSpriteScaled(SSplash[2], 160, 160, 1, 1, 0x09)
  elseif (drowningTime<3000) then
    DrawSpriteScaled(SSplash[1], 160, 160, 1, (3000-drowningTime)/1000, 0x09)
  elseif (drowningTime<10000) then
    CameraHeight = TopFlightHeight
    if (Speed<100) then
      Speed = 100
    end
    CameraX = CameraX - math.sin(CameraAngle) * Speed * 0.0001 * MsDelta
    CameraY = CameraY + math.cos(CameraAngle) * Speed * 0.0001 * MsDelta
    if (math.fmod(Ms//200, 2) == 0) then
      DrawSpriteScaled(SPlane[PlaneType], 160, 120, planeScale, planeScale, 0x05)
    end
  else
    AddEarnings(-10)
    DrowningStartMs = 0
  end
end
FillRect(310, 182 - Speed*0.5, 10, Speed*0.5, 0xf800)

DrawString(CameraHeight, 30, 30)
DrawString("Free " .. (GetFreeRAM()//1024) .. "k - " .. (GetFreePSRAM()//1024) .. "k - " .. GetFreeSpriteSlots(), 30, 40)

SetTextSize(2)
DisplayEarnings(180, 80)
DrawString("$" .. Money, 190, 188)
SetTextSize(1)
