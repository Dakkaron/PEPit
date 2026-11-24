Cls()
TurnEntity(cube, 0, 0.03 * MsDelta, 0)
camYaw = math.sin(Ms / 900.0) * 30
RotateCamera(cam, -14, camYaw, 0)
UpdateWorld()
DrawWorld()
SetTextSize(1)
DrawString("3D demo - keep the pressure steady", 6, 228)
