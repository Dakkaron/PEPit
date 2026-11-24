Cls()
spin = MsDelta * 0.03
TurnEntity(cube, spin * 0.4, spin, 0)
TurnEntity(sphere, 0, -spin * 0.6, 0)
CameraZoom(cam, 1.0 + Pressure / 3000)
UpdateWorld()
DrawWorld()
SetTextSize(1)
DrawString("3D demo - blow to zoom", 6, 228)
