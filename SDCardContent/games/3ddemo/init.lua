-- 3D demo: loads an OBJ model (with automatic texture from its mtl material),
-- sets up a camera, lights and entities. Runs once when the game starts.

carMesh = LoadMesh("models/racecar.obj")
carY = -32  -- car model sits on its wheel bottoms; align them with the ground
hasWheels = false
if (carMesh < 0) then
  SerialPrintln("3ddemo: OBJ load failed, falling back to a procedural textured cube")
  carMesh = CreateCube()
  SetMeshTexture(carMesh, LoadTexture("models/bricks.bmp"))
  carY = -7
end
car = CreateEntity(carMesh)
if (carMesh >= 0 and carY == -32) then
  -- wheels are separate meshes parented to the car so they can roll and steer;
  -- positions are car-local hub centers (front = +z), matching tools/gen_racecar.py
  wfMesh = LoadMesh("models/wheel_front.obj")
  wrMesh = LoadMesh("models/wheel_rear.obj")
  if (wfMesh >= 0 and wrMesh >= 0) then
    wFL = CreateEntity(wfMesh); wFR = CreateEntity(wfMesh)
    wRL = CreateEntity(wrMesh); wRR = CreateEntity(wrMesh)
    LinkEntity(wFL, car); LinkEntity(wFR, car)
    LinkEntity(wRL, car); LinkEntity(wRR, car)
    PositionEntity(wFL, -9.0, 4.6, 15.12)
    PositionEntity(wFR, 9.0, 4.6, 15.12)
    PositionEntity(wRL, -9.4, 5.1, -14.16)
    PositionEntity(wRR, 9.4, 5.1, -14.16)
    wheelRoll = 0
    hasWheels = true
  end
end
PositionEntity(car, 0, carY, 0)

groundMesh = CreatePlane(600, 600)
ground = CreateEntity(groundMesh)
PositionEntity(ground, 0, -32, 0)
EntityColor(ground, 140, 140, 160)

sphereMesh = CreateSphere(22, 14)
sphere = CreateEntity(sphereMesh)
PositionEntity(sphere, 80, -8, 60)
EntityColor(sphere, 255, 220, 80)

cam = CreateCamera()
PositionCamera(cam, 0, 2, -76)
RotateCamera(cam, -14, 0, 0)
CameraRange(cam, 1, 4000)

AmbientLight(35, 35, 40)
keyLight = CreateLight(255, 240, 215, 90, 120, -80, 500, POINTLIGHT)
fillLight = CreateLight(70, 100, 255, -150, 20, 80, 700, POINTLIGHT)
spotLight = CreateLight(255, 100, 70, -30, 170, -60, 600, SPOTLIGHT)
SetLightDirection(spotLight, 0.15, -1, 0.3)
