spin = MsDelta * 0.03
TurnEntity(car, 0, spin, 0)
if hasWheels then
  wheelRoll = wheelRoll + MsDelta * 0.12
  steer = 12
  RotateEntity(wFL, wheelRoll, steer, 0)
  RotateEntity(wFR, wheelRoll, steer, 0)
  RotateEntity(wRL, wheelRoll, 0, 0)
  RotateEntity(wRR, wheelRoll, 0, 0)
end
TurnEntity(sphere, 0, -spin * 0.6, 0)
CameraZoom(cam, 1.0 + Pressure / 3000)
UpdateWorld()
DrawWorld()
SetTextSize(1)
DrawString("3D demo - blow to zoom", 6, 228)
