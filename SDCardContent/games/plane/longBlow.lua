CameraHeight = CameraHeight + (Speed-50) * MsDelta * 0.00001
CameraHeight = Constrain(CameraHeight, 0.1, 2)
if NewRepetition then
  Speed = Speed + 100
end
speedDrop = 1 - (0.0001 * MsDelta)
Speed = Speed * speedDrop
CameraX = CameraX - math.sin(CameraAngle) * Speed * 0.0001 * MsDelta
CameraY = CameraY + math.cos(CameraAngle) * Speed * 0.0001 * MsDelta

DrawMode7(SBackground, CameraX, CameraY, CameraHeight, CameraAngle, 1, HorizonHeight, 20, 180)

for i = 1, #Objects do
  Objects[i][4] = calcDistance2D(CameraX, CameraY, Objects[i][2], Objects[i][3])
end

table.sort(Objects, function (left, right)
  return left[4] > right[4]
end)

local targetShipData = {}
local totalShipCount = 0
for i = 1, #Objects do
  local objectType = Objects[i][1]
  if (objectType == ObjectTypeShip) then
    drawShip(SSailShip, Objects[i][2], Objects[i][3], Objects[i][5] == TargetShip)
    if Objects[i][5] == TargetShip then
      targetShipData = Objects[i]
	end
	totalShipCount = totalShipCount + 1
  elseif (objectType == ObjectTypeStorm) then
    drawBillboard(SStorm, Objects[i][2], Objects[i][3], 1, 10)
  end
end

if (calcDistance2D(CameraX, CameraY, targetShipData[2], targetShipData[3]) < 3) then
  TargetShip = TargetShip + 1
  AddEarnings(5)
  if (TargetShip > totalShipCount) then
    TargetShip = 1
  end
end

local planeScale = 0.85+CameraHeight/8
if (IsTouchInZone(0, 0, 100, 240)) then
  DrawSpriteScaled(SPlaneBankL, 160, 120, planeScale, planeScale, 0x05)
  CameraAngle = CameraAngle + 0.001 * MsDelta
elseif (IsTouchInZone(220, 0, 100, 240)) then
  DrawSpriteScaled(SPlaneBankR, 160, 120, planeScale, planeScale, 0x05)
  CameraAngle = CameraAngle - 0.001 * MsDelta
else
  DrawSpriteScaled(SPlane, 160, 120, planeScale, planeScale, 0x05)
end
FillRect(310, 182 - Speed*0.5, 10, Speed*0.5, 0xf800)

DrawString(CameraHeight, 30, 30)

SetTextSize(2)
DisplayEarnings(180, 80)
DrawString("$" .. Money, 190, 188)
SetTextSize(1)
