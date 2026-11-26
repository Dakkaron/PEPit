# Lua API

```lua
Delay(seconds)

LoadBmp(path): returns sprite handle as integer

DrawSprite(handle, x, y)
DrawString(str, x, y)

```

## 3D functions

The 3D engine is inspired by the Blitz3D function reference. All 3D objects
(models, textures, entities, cameras, lights) are referenced by integer
handles, just like the 2D sprite handles. Handles are globally unique across
the kinds: pivots, cameras and lights are entities and share the entity
handle space, while mesh and texture handles live in their own ranges, so a
handle can only be used with the family of functions it came from. A handle
of -1 means the creation failed. A new frame is rendered by calling
`UpdateWorld()` followed by `DrawWorld()`, which draws into the game display
sprite.

Coordinate system: `+x` right, `+y` up, `+z` forward (into the scene).
Camera and entity rotations are in degrees: `pitch` turns about the x-axis
(positive pitch looks up), `yaw` about the y-axis (positive yaw turns right),
`roll` about the z-axis. Faces use the standard counter-clockwise OBJ
winding; if an imported model looks inside out, call `SetMeshFlipped`.

```lua
-- Models: load an .obj file, or create a procedural mesh
LoadMesh(path)                     -- mesh handle; a mtllib material file is
                                   -- loaded automatically and its first map_Kd
                                   -- BMP texture is applied when available
CreateCube()                       -- mesh handle, 50-unit Blitz3D-style cube, -25..25 on all axes
CreateSphere(radius, segments)     -- mesh handle, segments defaults to 12
CreatePlane(width, height)         -- mesh handle, flat XZ plane, normal +y
FreeMesh(meshHandle)
SetMeshColor(meshHandle, r, g, b)  -- 0..255 per channel, default 255,255,255
SetMeshTexture(meshHandle, textureHandle)  -- textureHandle -1 disables texture
SetMeshFlipped(meshHandle, flipped)        -- flip winding for mirrored models

-- Textures (16-bit BMP files, same loader as 2D sprites)
LoadTexture(path)                  -- texture handle
FreeTexture(textureHandle)

-- Entities: instances of a mesh in the world, plus pivots, cameras and
-- lights, which are entities too and accept every transform below.
CreateEntity(meshHandle)           -- entity handle; several entities may share one mesh
CreatePivot()                      -- entity handle, an invisible transform node to parent others to
FreeEntity(entityHandle)
MoveEntity(entityHandle, x, y, z)  -- translate along the entity's own rotated axes
PositionEntity(entityHandle, x, y, z)
RotateEntity(entityHandle, pitch, yaw, roll)   -- absolute angles
TurnEntity(entityHandle, pitch, yaw, roll)     -- relative angles
ScaleEntity(entityHandle, sx, sy, sz)
EntityColor(entityHandle, r, g, b)             -- multiplied with the mesh color
HideEntity(entityHandle, hidden)
LinkEntity(childHandle, parentHandle)          -- child follows parent transform
EntityOrder(entityHandle, order)               -- shifts the painter sort order

-- Cameras: entities that look along their local +z axis; the lowest active
-- camera handle is the active one. Camera handles also work with
-- PositionEntity/RotateEntity/TurnEntity/MoveEntity and can be linked to a
-- pivot to build orbit rigs. HideEntity(cam, true) disables a camera.
-- CreateCamera() starts with zoom 1, near plane 1, far plane 5000.
CreateCamera()                     -- entity handle
FreeCamera(cameraHandle)
PositionCamera(cameraHandle, x, y, z)
RotateCamera(cameraHandle, pitch, yaw, roll)
TurnCamera(cameraHandle, pitch, yaw, roll)
CameraZoom(cameraHandle, zoom)     -- zoom 1 == 60 degree vertical field of view
CameraRange(cameraHandle, near, far)

-- Lights: entities with per-vertex Gouraud lighting; distance attenuation
-- and spot cones. The beam (spot/directional) runs along the light entity's
-- local +z axis, so RotateEntity/TurnEntity aim it and LinkEntity moves it
-- with a rig.
AmbientLight(r, g, b)              -- default 127,127,127
CreateLight(r, g, b, x, y, z, range, lightType)  -- entity handle
  -- lightType: POINTLIGHT, SPOTLIGHT or DIRECTIONALLIGHT
  -- range 0 means infinite range; an unrotated light beams along +z
FreeLight(lightHandle)
SetLightDirection(lightHandle, dx, dy, dz)   -- aim the entity at a direction vector
SetLightCone(lightHandle, innerAngle, outerAngle) -- spot cone in degrees

-- Rendering
UpdateWorld()                      -- transforms, culls, lights and sorts all faces
DrawWorld()                        -- rasterizes the sorted faces onto the display
SetViewport(x, y, w, h)            -- 3D render region; defaults to (0, 21, 320, 168),
                                   -- the screen area not covered by the system UI bars

-- Housekeeping
FreeAll3D()                        -- frees every mesh, texture, entity, camera and light
```

Example:

```lua
cubeMesh = LoadMesh("models/cube.obj")
cube = CreateEntity(cubeMesh)
cam = CreateCamera()
PositionCamera(cam, 0, 30, -110)
RotateCamera(cam, -12, 0, 0)
AmbientLight(40, 40, 40)
lamp = CreateLight(255, 240, 215, 90, 120, -80, 450, POINTLIGHT)

function drawFrame()
  TurnEntity(cube, 0, 1, 0)
  UpdateWorld()
  DrawWorld()
end
```

See the in-development 3ddemo game (`WIPSDCardContent/games/3ddemo`, not committed) for a complete runnable example.

