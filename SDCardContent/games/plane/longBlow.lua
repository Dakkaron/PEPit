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

drawShip(SSailShip, 0, 100, true)
drawShip(SSailShip, 400, 400, true)
drawShip(SSailShip, 0, 380, true)
drawShip(SSailShip, 400, 100, true)
drawShip(SSailShip, 200, 200, true)

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
--DrawString("x" .. x, 30, 40)
--DrawString("y" .. y, 30, 50)
--DrawString("z" .. z, 30, 60)
