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

for i = 1, #ShipPositions-1 do
  drawShip(SSailShip, ShipPositions[i][1], ShipPositions[i][2], i == TargetShip)
end

if (calcDistance2D(CameraX, CameraY, ShipPositions[TargetShip][1], ShipPositions[TargetShip][2]) < 3) then
  TargetShip = TargetShip + 1
  AddEarnings(5)
  if (TargetShip >= #ShipPositions) then
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
DrawString("D ".. calcDistance2D(CameraX, CameraY, ShipPositions[TargetShip][1], ShipPositions[TargetShip][2]), 30, 40)
--DrawString("x" .. x, 30, 40)
--DrawString("y" .. y, 30, 50)
--DrawString("z" .. z, 30, 60)

SetTextSize(2)
DisplayEarnings(180, 80)
DrawString("$" .. Money, 190, 188)
SetTextSize(1)