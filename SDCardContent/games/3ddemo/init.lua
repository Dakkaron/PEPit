-- 3D demo: loads an OBJ model (with automatic texture from its mtl material),
-- sets up a camera, lights and entities. Runs once when the game starts.

cubeMesh = LoadMesh("models/cube.obj")
if (cubeMesh < 0) then
  SerialPrintln("3ddemo: OBJ load failed, falling back to a procedural textured cube")
  cubeMesh = CreateCube()
  SetMeshTexture(cubeMesh, LoadTexture("models/bricks.bmp"))
end
cube = CreateEntity(cubeMesh)

groundMesh = CreatePlane(600, 600)
ground = CreateEntity(groundMesh)
PositionEntity(ground, 0, -32, 0)
EntityColor(ground, 140, 140, 160)

sphereMesh = CreateSphere(22, 14)
sphere = CreateEntity(sphereMesh)
PositionEntity(sphere, 80, -8, 60)
EntityColor(sphere, 255, 220, 80)

cam = CreateCamera()
PositionCamera(cam, 0, 34, -120)
RotateCamera(cam, -14, 0, 0)
CameraRange(cam, 1, 4000)

AmbientLight(35, 35, 40)
keyLight = CreateLight(255, 240, 215, 90, 120, -80, 500, POINTLIGHT)
fillLight = CreateLight(70, 100, 255, -150, 20, 80, 700, POINTLIGHT)
spotLight = CreateLight(255, 100, 70, -30, 170, -60, 600, SPOTLIGHT)
SetLightDirection(spotLight, 0.15, -1, 0.3)
