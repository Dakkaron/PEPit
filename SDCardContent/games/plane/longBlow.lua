DisableCaching()
CameraHeight = CameraHeight + CameraHeightDir
CameraAngle = math.pi
if (CameraHeight>10 and CameraHeightDir>0) then
  CameraHeightDir = -0.1
elseif (CameraHeight<1 and CameraHeightDir<0) then
  CameraHeightDir = 0.1
end
CameraAngle = CameraAngle + 0.005
Speed = 5
CameraX = CameraX - math.sin(CameraAngle)
CameraY = CameraY + math.cos(CameraAngle)

DrawMode7(SBackground, CameraX, CameraY, CameraHeight, CameraAngle, 1, 50, 20, 180)
DrawString(CameraHeight, 30, 30)
DrawString(CameraHeightDir, 30, 40)
