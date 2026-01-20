SetTextSize(1)
CameraHeight = CameraHeight + (Speed-50) * MsDelta * 0.00001
if (DrowningStartMs == 0 and CameraHeight < 0.1 ) then
  DrowningStartMs = Ms
end
CameraHeight = Constrain(CameraHeight, 0.1, TopFlightHeight)
if (DrowningStartMs == 0) then
  if NewRepetition then
    Speed = Speed + 120 + 15*UpgradeSpeed
  end
  local speedDropUpgradeFactor = 1 + 0.12*UpgradeSpeed
  local speedDrop = (1 - (0.0001 * MsDelta)) ^ speedDropUpgradeFactor
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
  Objects[i].d = CalcDistance2D(CameraX, CameraY, Objects[i].x, Objects[i].y)
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
    DrawShip(SShip[object.shipType], object.x, object.y)
    if object.id == TargetShip then
      targetShipData = object
    end
    totalShipCount = totalShipCount + 1
  elseif (objectType == OBJECT_TYPE_STORM and object.d < 200) then
    DrawBillboard(SStorm, object.x, object.y, 7, 10)
  elseif (objectType == OBJECT_TYPE_BIRD and object.d < 1000) then
    local mx,my = GetObjectMotionVector(object)
    object.x = object.x + mx * BIRD_SPEED * 0.001 * MsDelta
    object.y = object.y + mx * BIRD_SPEED * 0.001 * MsDelta
    local dir = GetObjectRelativeDirection(mx, my)
    DrawBillboard(SBird, object.x, object.y, TopFlightHeight, 0.5, dir < 0 and true or false)
    local dx = object.targetX-object.x
    local dy = object.targetY-object.y
    local targetDistance = math.sqrt(dx*dx + dy*dy)
    if (targetDistance < 10) then
      object.targetX = math.random(10, 450)
      object.targetY = math.random(10, 450)
    end
  end
end

DrawTarget(targetShipData.x, targetShipData.y)

for i = 1, #Objects do
  local object = Objects[i]
  if (object.type == OBJECT_TYPE_STORM and object.d < 40) then
    CameraHeight = CameraHeight - MsDelta * 0.0001 * 10 - (Speed-50) * MsDelta * 0.00001
    if (DrowningStartMs == 0 and CameraHeight < 0.1 ) then
      DrowningStartMs = Ms
    end
    CameraHeight = Constrain(CameraHeight, 0.1, TopFlightHeight)
  elseif (object.type == OBJECT_TYPE_BIRD and object.d < 10) then
    object.x = math.random(10, 450)
    object.y = math.random(10, 450)
    AddEarnings(5)
  end
end

if (CalcDistance2D(CameraX, CameraY, targetShipData.x, targetShipData.y) < 3 and (TopFlightHeight - CameraHeight) < (TopFlightHeight/10.0)) then
  TargetShip = TargetShip + 1
  AddEarnings(3 + targetShipData.shipType * 2)
  if (TargetShip > totalShipCount) then
    TargetShip = 1
  end
end

local planeScale = 0.85+CameraHeight/8
if (DrowningStartMs == 0) then
  local joystickX = GetJoystickX()
  local planeAngleChange = 40 * 0.0001 * MsDelta * joystickX * 1.4 -- 1.4 is a temporary correction for not enough stick range of motion
  if LeftHandedMode then
    if IsTouchInZone(0, 50, 50, 44) then
      planeAngleChange = -40 * 0.0001 * MsDelta
    elseif IsTouchInZone(0, 125, 50, 44) then
      planeAngleChange = 40 * 0.0001 * MsDelta
    elseif math.abs(joystickX)<0.1 then
      local angleReturn = -(0.005 * MsDelta)
      planeAngleChange = PlaneAngle * angleReturn
    end
  else
    if IsTouchInZone(270, 50, 50, 44) then
      planeAngleChange = -40 * 0.0001 * MsDelta
    elseif IsTouchInZone(270, 125, 50, 44) then
      planeAngleChange = 40 * 0.0001 * MsDelta
    elseif math.abs(joystickX)<0.1 then
      local angleReturn = -(0.005 * MsDelta)
      planeAngleChange = PlaneAngle * angleReturn
    end
  end
  PlaneAngle = Constrain(PlaneAngle + planeAngleChange, -1, 1)
  CameraAngle = CameraAngle - (0.001 + 0.0002*UpgradeTurn) * MsDelta * PlaneAngle * 1.0
  CameraAngle = math.fmod(CameraAngle, math.pi*2)
  if (math.abs(PlaneAngle)<0.05) then
    DrawSpriteScaled(SPlane[PlaneType], 160, 120, planeScale, planeScale, 0x05)
  else
    DrawSpriteScaledRotated(SPlane[PlaneType], 160, 120, planeScale, planeScale, PlaneAngle, 0x05)
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
--FillRect(310, 182 - Speed*0.5, 10, Speed*0.5, 0xf800)

DrawSprite(SSpeedDial, 256, 116)
local speedDialAngle = math.pi*2.0 * math.min(Speed, 600) * 0.0015
DrawSpriteScaledRotated(SSpeedDialNeedle, 288, 148, 1, 1, speedDialAngle, 0x05)

--DrawString("Free " .. (GetFreeRAM()//1024) .. "k - " .. (GetFreePSRAM()//1024) .. "k - " .. GetFreeSpriteSlots(), 30, 40)

if LeftHandedMode then
  if IsTouchInZone(0, 50, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 27, 71, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 5, 50)
  end

  if IsTouchInZone(0, 125, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 27, 146, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 5, 125)
  end
else
  if IsTouchInZone(270, 50, 50, 44) then
    DrawSpriteScaledRotated(STurnLeft, 293, 71, 1, 1, -0.5, 0x05)
  else
    DrawSprite(STurnLeft, 271, 50)
  end

  if IsTouchInZone(270, 125, 50, 44) then
    DrawSpriteScaledRotated(STurnRight, 293, 146, 1, 1, 0.5, 0x05)
  else
    DrawSprite(STurnRight, 271, 125)
  end
end

SetTextSize(2)
DisplayEarnings(180, 80)
DrawString("$" .. Money, 190, 188)
SetTextSize(1)

DrawString("JX " .. GetJoystickX(), 100, 100)