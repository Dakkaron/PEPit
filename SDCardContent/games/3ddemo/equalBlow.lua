TurnEntity(car, 0, 0.03 * MsDelta, 0)
camYaw = math.sin(Ms / 900.0) * 30
RotateCamera(cam, -14, camYaw, 0)
if hasWheels then
  wheelRoll = wheelRoll + MsDelta * 0.12
  -- front wheels turn with the camera sweep so left and right steering show
  steer = camYaw * 0.4
  RotateEntity(wFL, wheelRoll, steer, 0)
  RotateEntity(wFR, wheelRoll, steer, 0)
  RotateEntity(wRL, wheelRoll, 0, 0)
  RotateEntity(wRR, wheelRoll, 0, 0)
end
UpdateWorld()
DrawWorld()
SetTextSize(1)
DrawString("3D demo - keep the pressure steady", 6, 228)
