TurnEntity(car, 0, 0.04 * MsDelta, 0)
PositionEntity(car, 0, carY + Pressure / 100, 0)
if hasWheels then
  wheelRoll = wheelRoll + MsDelta * 0.08
  RotateEntity(wFL, wheelRoll, 0, 0)
  RotateEntity(wFR, wheelRoll, 0, 0)
  RotateEntity(wRL, wheelRoll, 0, 0)
  RotateEntity(wRR, wheelRoll, 0, 0)
end
UpdateWorld()
DrawWorld()
SetTextSize(1)
DrawString("3D demo - blow to lift", 6, 228)
