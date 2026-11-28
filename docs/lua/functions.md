# Lua Functions

This document lists every C function that is exposed to Lua games via `lua_register()` in
[`gameLua.cpp`](../../T-HMI-PEPmonitor/src/games/gameLua.cpp). All functions are registered as **global**
functions on the Lua state, so they can be called directly by name from any game script.

The standard Lua libraries `table`, `string` and `math` are also available (see
[`gameLua.cpp:942-948`](../../T-HMI-PEPmonitor/src/games/gameLua.cpp#L942)).

> **Note on types.** The C wrappers read arguments with `luaL_check*` / `luaL_opt*`. In practice
> Lua numbers are passed for all numeric parameters. "int" and "float" below describe the C type
> the wrapper expects; from Lua you can simply pass a number.

## Contents

- [System & Debug](#system--debug)
- [Scripting](#scripting)
- [Sprites — Loading & Freeing](#sprites--loading--freeing)
- [Sprites — Drawing](#sprites--drawing)
- [Sprites — Info & Draw Target](#sprites--info--draw-target)
- [Mode 7 (3D Ground)](#mode-7-3d-ground)
- [3D Engine (Blitz3D-inspired)](#3d-engine-blitz3d-inspired)
- [Graphics Primitives](#graphics-primitives)
- [Text](#text)
- [Preferences (Persistent Storage)](#preferences-persistent-storage)
- [Game Flow](#game-flow)
- [Math Utilities](#math-utilities)
- [Touch Input](#touch-input)
- [Joystick Input](#joystick-input)
- [Road (OutRun-style)](#road-outrun-style)
- [Flag Constants](#flag-constants)

---

## System & Debug

### `SerialPrintln(s)`
Prints a string to the serial console followed by a newline.

| Param | Type   | Description            |
|-------|--------|------------------------|
| `s`   | string | Text to print          |

**Returns:** nothing.

### `SerialPrint(s)`
Prints a string to the serial console without a newline.

| Param | Type   | Description            |
|-------|--------|------------------------|
| `s`   | string | Text to print          |

**Returns:** nothing.

### `GetFreeRAM()`
**Returns:** free internal heap in bytes (number).

### `GetFreePSRAM()`
**Returns:** free PSRAM in bytes (number).

### `DisableCaching()`
Disables the Lua script source cache. By default game scripts are read from the SD card once and
cached in RAM for the lifetime of the game; calling this makes subsequent `RunScript` / script
loads re-read from disk.

**Returns:** nothing.

---

## Scripting

### `RunScript(path)`
Loads and executes another Lua script. The path is resolved **relative to the current game's
directory** (the directory of `init.lua`).

| Param  | Type   | Description                              |
|--------|--------|------------------------------------------|
| `path` | string | Script path relative to the game folder  |

**Returns:** nothing. Errors are reported on the serial console (and, in strict mode, abort).

---

## Sprites — Loading & Freeing

Sprites are stored in a fixed pool of **200** slots (`SPRITE_COUNT_LIMIT`). Loading a sprite
returns an integer **handle** (the slot index) that is used by all the drawing functions. A
failed load returns `-1`.

### `LoadSprite(path, options = 0, maskingColor = -1)`
Loads a static BMP sprite into the first free slot.

| Param          | Type   | Default | Description                                             |
|----------------|--------|---------|---------------------------------------------------------|
| `path`         | string | —       | BMP path relative to the game folder                    |
| `options`      | int    | `0`     | Bitmap options (see [Flag Constants](#flag-constants))  |
| `maskingColor` | int    | `-1`    | Color treated as transparent, or `-1` for none          |

**Returns:** sprite handle (int), or `-1` on failure.

### `LoadAnimSprite(path, frameW, frameH, options = 0, maskingColor = -1)`
Loads an animated (sprite-sheet) BMP sprite. The sheet is divided into frames of `frameW` ×
`frameH` pixels, laid out left-to-right, top-to-bottom.

| Param          | Type   | Default | Description                                             |
|----------------|--------|---------|---------------------------------------------------------|
| `path`         | string | —       | BMP path relative to the game folder                    |
| `frameW`       | number | —       | Width of a single frame in pixels                        |
| `frameH`       | number | —       | Height of a single frame in pixels                      |
| `options`      | int    | `0`     | Bitmap options (see [Flag Constants](#flag-constants))  |
| `maskingColor` | int    | `-1`    | Color treated as transparent, or `-1` for none          |

**Returns:** sprite handle (int), or `-1` on failure.

### `FreeSprite(handle)`
Frees a previously loaded sprite, making its slot available again. If the sprite is currently the
active draw target, the draw target is reset to the main framebuffer.

| Param    | Type | Description        |
|----------|------|--------------------|
| `handle` | int  | Sprite handle      |

**Returns:** nothing.

### `GetFreeSpriteSlots()`
**Returns:** number of currently free sprite slots (int, 0–200).

---

## Sprites — Drawing

All drawing functions render to the **current draw target** (see
[SetDrawTargetSprite](#setdrawtargetspritehandle) /
[SetDrawTargetFramebuffer](#setdrawtargetframebuffer)).

### `DrawSprite(handle, x, y, [opts])`
Draws a sprite (or a single frame of an animated sprite) at position `(x, y)` with optional
scaling, rotation, and opacity. This is the unified drawing function that replaces the former
`DrawAnimSprite`, `DrawSpriteScaled`, `DrawAnimSpriteScaled`, `DrawSpriteScaledRotated`, and
`DrawAnimSpriteScaledRotated`.

| Param    | Type   | Default | Description                          |
|----------|--------|---------|--------------------------------------|
| `handle` | int    | —       | Sprite handle                        |
| `x`      | number | —       | Destination X coordinate             |
| `y`      | number | —       | Destination Y coordinate             |
| `opts`   | table  | `nil`   | Optional drawing options (see below) |

**Options table fields:**

| Field      | Type   | Default | Description                                          |
|------------|--------|---------|------------------------------------------------------|
| `scaleX`   | number | `1.0`   | Horizontal scale factor                              |
| `scaleY`   | number | `1.0`   | Vertical scale factor                                |
| `angle`    | number | `0.0`   | Rotation in radians                                  |
| `frame`    | int    | `-1`    | Frame index for animated sprites (`-1` = whole sprite) |
| `flags`    | int    | `0`     | Draw flags (see [Flag Constants](#flag-constants))   |
| `alpha`    | number | `1.0`   | Opacity, `0` (transparent)–`1`                       |

The function automatically selects the most efficient rendering path based on which options
are set (plain blit → scaled → transformed).

**Examples:**

```lua
-- Simple draw
DrawSprite(1, 100, 200)

-- With alpha
DrawSprite(1, 100, 200, {alpha = 0.5})

-- Scaled
DrawSprite(1, 100, 200, {scaleX = 2.0, scaleY = 2.0})

-- Rotated and scaled
DrawSprite(1, 100, 200, {scaleX = 1.5, angle = math.pi / 4})

-- Animated sprite, frame 3, centered
DrawSprite(2, 100, 200, {frame = 3, flags = ALIGN_H_CENTER | ALIGN_V_CENTER})
```

**Returns:** nothing.

---

## Sprites — Info & Draw Target

### `SpriteWidth(handle)`
**Returns:** width of the sprite (or a single frame for animated sprites) in pixels.

### `SpriteHeight(handle)`
**Returns:** height of the sprite (or a single frame for animated sprites) in pixels.

### `SetDrawTargetSprite(handle)`
Sets the current draw target to a sprite, so subsequent drawing calls render into that sprite's
buffer instead of the screen. If `handle` is invalid, the target resets to the framebuffer.

| Param    | Type | Description        |
|----------|------|--------------------|
| `handle` | int  | Sprite handle      |

**Returns:** nothing.

### `SetDrawTargetFramebuffer()`
Sets the current draw target back to the main framebuffer (the screen).

**Returns:** nothing.

---

## Mode 7 (3D Ground)

Mode 7 rendering produces a pseudo-3D ground plane (à la classic racing games) from a texture
sprite, using a virtual camera.

### `DrawMode7(textureHandle, camX, camY, camHeight, yawAngle, zoom, horizonHeight, startY, endY)`
Renders the Mode 7 ground into the region between `startY` and `endY`.

| Param           | Type   | Description                                          |
|-----------------|--------|------------------------------------------------------|
| `textureHandle` | int    | Handle of the ground texture sprite                  |
| `camX`          | number | Camera world X position                              |
| `camY`          | number | Camera world Y position                              |
| `camHeight`     | float  | Camera height above the ground plane                 |
| `yawAngle`      | float  | Camera yaw (rotation) in radians                     |
| `zoom`          | float  | Zoom / focal factor                                  |
| `horizonHeight` | float  | Y position of the horizon line                       |
| `startY`        | float  | Top screen row to start rendering at                 |
| `endY`          | float  | Bottom screen row to render up to                    |

**Returns:** nothing.

### `Mode7WorldToScreen(worldX, worldY, camX, camY, camHeight, yawAngle, zoom, horizonHeight, startY, endY)`
Projects a world-space point onto the screen using the same camera parameters as `DrawMode7`.

| Param           | Type   | Description                                  |
|-----------------|--------|----------------------------------------------|
| `worldX`        | number | World X of the point                         |
| `worldY`        | number | World Y of the point                         |
| `camX`          | number | Camera world X position                      |
| `camY`          | number | Camera world Y position                      |
| `camHeight`     | float  | Camera height above the ground plane         |
| `yawAngle`      | float  | Camera yaw in radians                        |
| `zoom`          | float  | Zoom / focal factor                          |
| `horizonHeight` | float  | Y position of the horizon line               |
| `startY`        | float  | Top screen row                               |
| `endY`          | float  | Bottom screen row                            |

**Returns:** three values — `screenX` (int), `screenY` (int), `scale` (float).

```lua
sx, sy, s = Mode7WorldToScreen(wx, wy, camX, camY, camH, yaw, zoom, horizon, startY, endY)
```

---

## 3D Engine (Blitz3D-inspired)

The 3D engine ([`engine3d.cpp`](../../T-HMI-PEPmonitor/src/hardware/engine3d.cpp)) is driven by
**meshes**, **textures**, **entities**, **cameras**, **lights**, **planes** and **billboards** — all
referenced by integer handles like the 2D sprite handles. Handles are globally unique per kind:
pivots, cameras, lights and planes are entities and share the entity handle space; mesh, texture and
billboard handles occupy their own ranges, so a handle only works with the functions of the family
that returned it. A mesh is
object data, not a scene object: `CreateEntity` instantiates a visible entity from it, and
several entities may share one mesh. Creation functions return `-1` on failure; the `Free*`
functions return a boolean. A frame is rendered by calling `UpdateWorld()` and then `DrawWorld()`.
The engine is internally pipelined and split over both cores; that is invisible to Lua and never
changes the rendered result.

Fixed capacities ([`engine3d.hpp`](../../T-HMI-PEPmonitor/src/hardware/engine3d.hpp)): **32
meshes**, **64 textures**, **128 entities** (pivots, cameras, lights and planes included),
**8 lights**, **4 cameras**, **64 billboards**, **16 planes**.

Coordinate system: `+x` right, `+y` up, `+z` forward (into the scene). All rotations are in
degrees: `pitch` turns about the x-axis (positive looks up), `yaw` about the y-axis (positive
turns right), `roll` about the z-axis. Faces use the standard counter-clockwise OBJ winding; if
an imported model looks inside out, call `SetMeshFlipped`.

### `LoadMesh(path)`
Loads a Wavefront `.obj` file. If the file references a `mtllib` material file, it is loaded
automatically and the first `map_Kd` BMP texture is applied when available. Ngons (faces with
more than 3 corners) are triangulated on load.

| Param  | Type   | Description                             |
|--------|--------|-----------------------------------------|
| `path` | string | OBJ path relative to the game folder    |

**Returns:** mesh handle (int), or `-1` on failure.

### `CreateCube()`
Creates a procedural Blitz3D-style cube.

**Returns:** mesh handle (int), or `-1` on failure. The cube spans −25..25 on all three axes
(50 units edge length).

### `CreateSphere(radius, segments = 12)`
Creates a procedural sphere centered at the origin.

| Param      | Type   | Default | Description                  |
|------------|--------|---------|------------------------------|
| `radius`   | float  | —       | Sphere radius                |
| `segments` | int    | `12`    | Segments per axis            |

**Returns:** mesh handle (int), or `-1` on failure.

### `CreateQuad(width, height)`
Creates a flat finite quad in the XZ plane with normal `+y`, centered at the origin. For an infinite
ground or cloud layer use `CreatePlane` instead.

| Param    | Type  | Description                    |
|----------|-------|--------------------------------|
| `width`  | float | Extent along x                 |
| `height` | float | Extent along z                 |

**Returns:** mesh handle (int), or `-1` on failure.

### `FreeMesh(meshHandle)`
Frees a mesh and its view scratch buffers. Entities using it stop rendering.

| Param        | Type | Description          |
|--------------|------|----------------------|
| `meshHandle` | int  | Handle to free       |

**Returns:** `true` if the handle was in use.

### `SetMeshColor(meshHandle, r, g, b)`
Base color of every instance of the mesh.

| Param        | Type  | Description                     |
|--------------|-------|---------------------------------|
| `meshHandle` | int   | Mesh handle                     |
| `r`, `g`, `b`| int   | 0..255 per channel, default 255 |

**Returns:** nothing.

### `SetMeshEmissive(meshHandle, r, g, b)`
Self-illumination added to the light the mesh receives, so its instances never render dark.
`0,0,0` (default) means the mesh is lit only by ambient and lights. Emission adds to incoming
light and is capped at 1.0 per channel, so a surface can never be brighter than its base color.

| Param        | Type  | Description                     |
|--------------|-------|---------------------------------|
| `meshHandle` | int   | Mesh handle                     |
| `r`, `g`, `b`| int   | 0..255 per channel, default 0   |

**Returns:** nothing.

### `SetMeshTexture(meshHandle, textureHandle)`
Assigns a loaded texture to the mesh, or disables texturing with `-1`.

| Param          | Type | Description                        |
|----------------|------|------------------------------------|
| `meshHandle`   | int  | Mesh handle                        |
| `textureHandle`| int  | Texture handle, or `-1` to clear   |

**Returns:** nothing.

### `SetMeshFlipped(meshHandle, flipped)`
Reverses face winding at load level for models that appear mirrored / inside out.

| Param        | Type | Description                          |
|--------------|------|--------------------------------------|
| `meshHandle` | int  | Mesh handle                          |
| `flipped`    | int  | Non-zero flips the winding           |

**Returns:** nothing.

### `LoadTexture(path)`
Loads a 16-bit BMP texture (same loader as 2D sprites) for use on meshes.

| Param  | Type   | Description                            |
|--------|--------|----------------------------------------|
| `path` | string | BMP path relative to the game folder   |

**Returns:** texture handle (int), or `-1` on failure.

### `FreeTexture(textureHandle)`
Frees a texture; meshes and entities using it fall back to untextured.

| Param           | Type | Description        |
|-----------------|------|--------------------|
| `textureHandle` | int  | Handle to free     |

**Returns:** `true` if the handle was in use.

### `CreateEntity(meshHandle)`
Places an instance of a mesh into the world. Several entities may share one mesh; each instance
has its own transform and color.

| Param        | Type | Description       |
|--------------|------|-------------------|
| `meshHandle` | int  | Mesh to instance  |

**Returns:** entity handle (int), or `-1` on failure.

### `CreatePivot()`
Creates an invisible transform node to parent entities to. Pivots render nothing but carry the
full entity transform set, so turning a pivot orbits everything linked to it.

**Returns:** entity handle (int), or `-1` on failure.

### `CreatePlane(textureHandle = -1, keyColor = -1, lit = 1, verts = 48)`
Creates an infinite ground or cloud layer as an **entity**: `MoveEntity`, `PositionEntity`,
`RotateEntity`, `TurnEntity`, `ScaleEntity`, `EntityColor`, `EntityEmissive`, `HideEntity`,
`EntityOrder` and `FreeEntity` all work on it. The plane has no mesh and no size — it always covers
the whole view, and the engine re-tessellates it every frame around the camera: a hub at the
camera's position on the plane, then rings of quads out to the far plane, so the detail is dense
under the camera and coarse at the horizon. `verts` is the vertex budget that bounds those rings
(16 wedges each, 1..8 rings, so 33..145 vertices).

`ScaleEntity`'s `sx` and `sz` are the **texture tile size in world units**: the texture repeats once
every `sx` units along the plane's local x and every `sz` units along its local z, anchored to the
entity, so moving or rotating the plane moves the texture with it. `sy` does nothing: the plane has
no thickness.

The plane is **one-sided**: its front is the local `+y` side, and the camera sees nothing when it is
on the other side. A cloud layer seen from below is therefore a plane placed above the camera and
rotated `180` degrees of pitch. `keyColor` is an RGB565 texel value rendered fully transparent
(`-1` for no keying) and, like billboards, a keyed plane paints after the opaque geometry at its
depth, so what is behind it shows through the keyed texels. `lit = 0` shades from ambient and
emissive only; `lit = 1` also takes the scene lights, whose falloff is evaluated per vertex.

| Param            | Type  | Default | Description                                  |
|------------------|-------|---------|----------------------------------------------|
| `textureHandle`  | int   | `-1`    | Texture to tile; `-1` = flat entity color     |
| `keyColor`       | int   | `-1`    | RGB565 texel made transparent; `-1` = none    |
| `lit`            | int   | `1`     | `0` = ambient + emissive only                 |
| `verts`          | int   | `48`    | Vertex budget that bounds the LOD rings       |

**Returns:** entity handle (int), or `-1` on failure.

### `FreeEntity(entityHandle)`
Removes an instance.

| Param          | Type | Description        |
|----------------|------|--------------------|
| `entityHandle` | int  | Handle to free     |

**Returns:** `true` if the handle was in use.

### `MoveEntity(entityHandle, x, y, z)`
Translates the entity along its **own rotated axes** (Blitz3D semantics): with `yaw = 90` the
entity's `+z` points at world `+x`, so `MoveEntity(e, 0, 0, 10)` steps 10 units to the right.
With a parent, the move happens in the parent's frame.

| Param          | Type  | Description              |
|----------------|-------|--------------------------|
| `entityHandle` | int   | Entity handle            |
| `x`, `y`, `z`  | float | Local-axis offsets       |

**Returns:** nothing.

### `PositionEntity(entityHandle, x, y, z)`
Sets the absolute position. With a parent (see `LinkEntity`), the position is relative to the
parent.

| Param          | Type  | Description            |
|----------------|-------|------------------------|
| `entityHandle` | int   | Entity handle          |
| `x`, `y`, `z`  | float | World coordinates      |

**Returns:** nothing.

### `RotateEntity(entityHandle, pitch, yaw, roll)`
Sets absolute rotation angles in degrees.

| Param          | Type  | Description                 |
|----------------|-------|-----------------------------|
| `entityHandle` | int   | Entity handle               |
| `pitch`        | float | Rotation about x (up +)     |
| `yaw`          | float | Rotation about y (right +)  |
| `roll`         | float | Rotation about z            |

**Returns:** nothing.

### `TurnEntity(entityHandle, pitch, yaw, roll)`
Adds relative rotation, e.g. `TurnEntity(wheel, 10, 0, 0)` rolls 10 degrees per call.

| Param          | Type  | Description                |
|----------------|-------|----------------------------|
| `entityHandle` | int   | Entity handle              |
| `pitch`        | float | Added rotation about x     |
| `yaw`          | float | Added rotation about y     |
| `roll`         | float | Added rotation about z     |

**Returns:** nothing.

### `ScaleEntity(entityHandle, sx, sy, sz)`
Non-uniform scale of the instance.

| Param          | Type  | Description          |
|----------------|-------|----------------------|
| `entityHandle` | int   | Entity handle        |
| `sx`, `sy`, `sz` | float | Scale per axis     |

**Returns:** nothing.

### `EntityColor(entityHandle, r, g, b)`
Per-instance tint, multiplied with the mesh color.

| Param          | Type  | Description                     |
|----------------|-------|---------------------------------|
| `entityHandle` | int   | Entity handle                   |
| `r`, `g`, `b`  | int   | 0..255 per channel              |

**Returns:** nothing.

### `EntityEmissive(entityHandle, r, g, b)`
Per-instance emission; overrides the mesh's emissive. `-1` per channel falls back to the mesh's value.

| Param          | Type  | Description                          |
|----------------|-------|--------------------------------------|
| `entityHandle` | int   | Entity handle                        |
| `r`, `g`, `b`  | int   | 0..255 per channel, or `-1` to inherit the mesh |

**Returns:** nothing.

### `SetEntityTexture(entityHandle, textureHandle)`
Per-instance texture override: entities sharing a mesh can render with different textures.
`-1` clears the override and falls back to the mesh's own texture.

| Param           | Type | Description                        |
|-----------------|------|------------------------------------|
| `entityHandle`  | int  | Entity handle                      |
| `textureHandle` | int  | Texture handle, or `-1` to clear   |

**Returns:** nothing.

### `HideEntity(entityHandle, hidden)`
Excludes the instance from the scene while hidden.

| Param          | Type | Description                   |
|----------------|------|-------------------------------|
| `entityHandle` | int  | Entity handle                 |
| `hidden`       | int  | Non-zero hides the entity     |

**Returns:** nothing.

### `LinkEntity(childHandle, parentHandle)`
Makes the child follow the parent's transform (position and rotation are composed, nested links
are followed up to 8 levels). A child is unlinked automatically when its parent is freed.

| Param         | Type | Description                        |
|---------------|------|------------------------------------|
| `childHandle` | int  | Child entity                       |
| `parentHandle`| int  | Parent entity handle               |

**Returns:** `true` on success.

### `EntityOrder(entityHandle, order)`
Bias applied to the painter's depth sort: the entity's faces are shifted `order × 32` buckets
out of 4096 depth buckets. Positive values draw earlier (behind other geometry), negative
values draw later (in front). Used for coplanar surfaces to stop z-fighting flicker.

| Param          | Type | Description                    |
|----------------|------|--------------------------------|
| `entityHandle` | int  | Entity handle                  |
| `order`        | int  | Signed bias, clamped to int8   |

**Returns:** nothing.

### `CreateCamera()`
Creates a camera entity. The camera with the **lowest handle** is the active one. Defaults:
zoom 1, near plane 1, far plane 5000.

**Returns:** entity handle (int), or `-1` on failure. Cameras are entities: the handle works with
every entity transform (`PositionCamera` equals `PositionEntity` etc.), a camera linked to a
pivot orbits with it, and `HideEntity(cam, true)` disables it.

### `FreeCamera(cameraHandle)`
Frees a camera.

| Param          | Type | Description        |
|----------------|------|--------------------|
| `cameraHandle` | int  | Handle to free     |

**Returns:** `true` if the handle was in use.

### `PositionCamera(cameraHandle, x, y, z)`
Sets the absolute camera position.

| Param          | Type  | Description            |
|----------------|-------|------------------------|
| `cameraHandle` | int   | Camera handle          |
| `x`, `y`, `z`  | float | World coordinates      |

**Returns:** nothing.

### `RotateCamera(cameraHandle, pitch, yaw, roll)`
Sets absolute camera rotation angles in degrees.

| Param          | Type  | Description   |
|----------------|-------|---------------|
| `cameraHandle` | int   | Camera handle |
| `pitch`        | float | Degrees       |
| `yaw`          | float | Degrees       |
| `roll`         | float | Degrees       |

**Returns:** nothing.

### `TurnCamera(cameraHandle, pitch, yaw, roll)`
Adds relative camera rotation.

| Param          | Type  | Description   |
|----------------|-------|---------------|
| `cameraHandle` | int   | Camera handle |
| `pitch`        | float | Degrees       |
| `yaw`          | float | Degrees       |
| `roll`         | float | Degrees       |

**Returns:** nothing.

### `CameraZoom(cameraHandle, zoom)`
Sets the zoom factor; `zoom = 1` is a 60° vertical field of view.

| Param          | Type  | Description   |
|----------------|-------|---------------|
| `cameraHandle` | int   | Camera handle |
| `zoom`         | float | Zoom factor   |

**Returns:** nothing.

### `CameraRange(cameraHandle, near, far)`
Sets the near and far clipping planes. Faces outside this range are culled.

| Param          | Type  | Description   |
|----------------|-------|---------------|
| `cameraHandle` | int   | Camera handle |
| `near`         | float | Near plane    |
| `far`          | float | Far plane     |

**Returns:** nothing.

### `AmbientLight(r, g, b)`
Sets the global ambient light level. Device default: `127, 127, 127`.

| Param       | Type | Description        |
|-------------|------|--------------------|
| `r`, `g`, `b` | int | 0..255 per channel |

**Returns:** nothing.

### `CreateLight(r, g, b, x, y, z, range = 0, lightType = POINTLIGHT)`
Creates a light entity (per-vertex Gouraud lighting with distance attenuation for point/spot
lights).

| Param       | Type  | Default          | Description                                        |
|-------------|-------|------------------|----------------------------------------------------|
| `r`, `g`, `b` | int | —                | 0..255 per channel                                 |
| `x`, `y`, `z` | float | —              | Light position                                     |
| `range`     | float | `0`              | Attenuation range; `0` = infinite                  |
| `lightType` | int   | `POINTLIGHT`     | `POINTLIGHT`, `SPOTLIGHT` or `DIRECTIONALLIGHT`    |

**Returns:** entity handle (int), or `-1` on failure. Lights are entities: spot and directional
beams run along the entity's local `+z` axis, so `RotateEntity`/`TurnEntity` aim them and
`LinkEntity` moves them with a rig; an unrotated light beams into the scene (`+z`).

### `FreeLight(lightHandle)`
Removes a light.

| Param         | Type | Description        |
|---------------|------|--------------------|
| `lightHandle` | int  | Handle to free     |

**Returns:** `true` if the handle was in use.

### `SetLightDirection(lightHandle, dx, dy, dz)`
Beam direction for spot and directional lights (the vector is normalized; it rotates the light
entity so its `+z` axis points along the vector).

| Param         | Type  | Description        |
|---------------|-------|--------------------|
| `lightHandle` | int   | Light handle       |
| `dx`, `dy`, `dz` | float | Direction vector |

**Returns:** nothing.

### `SetLightCone(lightHandle, innerAngle, outerAngle)`
Spot light cone angles in degrees: full brightness inside `innerAngle`, falling off to zero at
`outerAngle`.

| Param         | Type  | Description             |
|---------------|-------|-------------------------|
| `lightHandle` | int   | Light handle            |
| `innerAngle`  | float | Full-brightness cone °  |
| `outerAngle`  | float | Edge of falloff cone °  |

**Returns:** nothing.

### `SetLightColor(lightHandle, r, g, b)`
Change a light's colour after creation (0-255 per channel). Takes effect on the next
`UpdateWorld()`.

| Param         | Type | Description    |
|---------------|------|----------------|
| `lightHandle` | int  | Light handle   |
| `r`, `g`, `b` | int  | Color channels |

**Returns:** nothing.

### `SetLightRange(lightHandle, range)`
Change a light's distance attenuation radius. `range` 0 means infinite range.

| Param         | Type  | Description        |
|---------------|-------|--------------------|
| `lightHandle` | int   | Light handle       |
| `range`       | float | Attenuation radius |

**Returns:** nothing.

### `CreateBillboard(textureHandle, keyColor = -1, lit = 1)`
Creates one of the 64 camera-facing quads, the particle/effect layer of the engine: a billboard is
always normal-to the camera, so `RotateBillboard`/`TurnBillboard` spin the image in the screen
plane only. `keyColor` is the texel color that becomes fully transparent — passed as a 24-bit color
and reduced to the RGB565 value the texture can hold; `-1` (the default) keys nothing. `lit` decides
whether dynamic lights shade the texture: an unlit billboard is shaded by ambient plus its own
emission only. Billboards draw after the opaque geometry at their depth, so their keyed pixels let
whatever is behind them show through.

| Param           | Type  | Default | Description                                  |
|-----------------|-------|---------|----------------------------------------------|
| `textureHandle` | int   | —       | Texture to draw, from `LoadTexture`          |
| `keyColor`      | int   | `-1`    | Texel color made transparent; `-1` = no key   |
| `lit`           | int   | `1`     | `0` = ambient + emissive only                |

**Returns:** billboard handle (int), or `-1` on failure. Billboards have their own handle range.

### `FreeBillboard(billboardHandle)`
Removes a billboard.

| Param             | Type | Description        |
|-------------------|------|--------------------|
| `billboardHandle` | int  | Handle to free     |

**Returns:** `true` if the handle was in use.

### `PositionBillboard(billboardHandle, x, y, z)`
World position of the quad's centre.

| Param             | Type  | Description        |
|-------------------|-------|--------------------|
| `billboardHandle` | int   | Billboard handle   |
| `x`, `y`, `z`     | float | World coordinates  |

**Returns:** nothing.

### `ScaleBillboard(billboardHandle, sx, sy)`
Size in world units. A billboard is a flat quad, so there is no depth scale.

| Param             | Type  | Description                       |
|-------------------|-------|-----------------------------------|
| `billboardHandle` | int   | Billboard handle                  |
| `sx`, `sy`        | float | Full width and height, default 1x1 |

**Returns:** nothing.

### `RotateBillboard(billboardHandle, angle)`
Absolute spin in degrees, measured in the screen plane (positive is counter-clockwise on screen).

| Param             | Type  | Description              |
|-------------------|-------|--------------------------|
| `billboardHandle` | int   | Billboard handle         |
| `angle`           | float | Screen-plane angle       |

**Returns:** nothing.

### `TurnBillboard(billboardHandle, deltaAngle)`
Relative spin in degrees, in the screen plane.

| Param             | Type  | Description              |
|-------------------|-------|--------------------------|
| `billboardHandle` | int   | Billboard handle         |
| `deltaAngle`      | float | Screen-plane angle       |

**Returns:** nothing.

### `SetBillboardEmissive(billboardHandle, r, g, b)`
Additive self-light, 0-255 per channel, default `0,0,0`. The quad is shaded by `ambient + emissive +
lights` (clamped to full brightness); with `lit = 0` the lights term never appears.

| Param             | Type | Description                     |
|-------------------|------|---------------------------------|
| `billboardHandle` | int  | Billboard handle                |
| `r`, `g`, `b`     | int  | 0..255 per channel, default 0   |

**Returns:** nothing.

### `HideBillboard(billboardHandle, hidden)`
Keeps the billboard out of the draw list without freeing it.

| Param             | Type | Description                     |
|-------------------|------|---------------------------------|
| `billboardHandle` | int  | Billboard handle                |
| `hidden`          | int  | Non-zero to hide                |

**Returns:** nothing.

### `UpdateWorld()`
Transforms, culls, lights and depth-sorts every entity's faces and billboards into the draw list.
Must run before `DrawWorld()` each frame.

**Returns:** nothing.

### `DrawWorld()`
Rasterizes the sorted draw list into the game display (fills the current 3D viewport; pixels not
covered by geometry are blanked, so each frame starts clean).

**Returns:** nothing.

### `SetViewport(x, y, w, h)`
Sets the screen region the 3D engine renders into. At game start the viewport defaults to
`SetViewport(0, 21, 320, 168)` — the area not covered by the system UI bars.

| Param | Type | Description                |
|-------|------|----------------------------|
| `x`   | int  | Viewport left edge         |
| `y`   | int  | Viewport top edge          |
| `w`   | int  | Viewport width in pixels   |
| `h`   | int  | Viewport height in pixels  |

**Returns:** nothing.

### `FreeAll3D()`
Frees every mesh, texture, entity, camera, light and billboard. Safe to call at any point; used by
the host when a game ends.

**Returns:** nothing.

### 3D light type constants

These globals are predefined in the Lua state:

| Constant            | Value | Meaning                     |
|---------------------|-------|-----------------------------|
| `POINTLIGHT`        | `1`   | Omnidirectional, attenuated |
| `SPOTLIGHT`         | `2`   | Coned beam                  |
| `DIRECTIONALLIGHT`  | `4`   | Parallel rays (sun-like)    |

> A fuller conceptual overview with a complete example is in [LuaAPI.md](../LuaAPI.md);
> The in-development 3ddemo game (`WIPSDCardContent/games/3ddemo`, not committed) exercises these commands end to end.

---

## Graphics Primitives

All primitives draw to the current draw target. Colors are 16-bit RGB565 values (e.g. `TFT_RED`).

### `DrawString(str, x, y)`
Draws text at `(x, y)`. Multi-line strings (containing `\n`) are supported; each line advances by
one font height.

| Param | Type   | Description        |
|-------|--------|--------------------|
| `str` | string | Text to draw       |
| `x`   | number | X coordinate       |
| `y`   | number | Y coordinate       |

**Returns:** nothing.

### `DrawRect(x, y, w, h, color)`
Draws an unfilled rectangle outline.

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | Top-left X               |
| `y`     | number | Top-left Y               |
| `w`     | number | Width                    |
| `h`     | number | Height                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `FillRect(x, y, w, h, color)`
Draws a filled rectangle.

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | Top-left X               |
| `y`     | number | Top-left Y               |
| `w`     | number | Width                    |
| `h`     | number | Height                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `DrawCircle(x, y, r, color)`
Draws an unfilled circle outline.

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | Center X                 |
| `y`     | number | Center Y                 |
| `r`     | number | Radius                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `FillCircle(x, y, r, color)`
Draws a filled circle.

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | Center X                 |
| `y`     | number | Center Y                 |
| `r`     | number | Radius                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `DrawLine(x0, y0, x1, y1, color)`
Draws a line between two points.

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x0`    | number | Start X                  |
| `y0`    | number | Start Y                  |
| `x1`    | number | End X                    |
| `y1`    | number | End Y                    |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `DrawFastHLine(x, y, w, color)`
Draws a horizontal line (fast path).

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | Start X                  |
| `y`     | number | Y                        |
| `w`     | number | Length                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `DrawFastVLine(x, y, h, color)`
Draws a vertical line (fast path).

| Param   | Type   | Description              |
|---------|--------|--------------------------|
| `x`     | number | X                        |
| `y`     | number | Start Y                  |
| `h`     | number | Length                   |
| `color` | int    | RGB565 color             |

**Returns:** nothing.

### `FillSprite(color)`
Fills the entire current draw target with a solid color.

| Param   | Type | Description        |
|---------|------|--------------------|
| `color` | int  | RGB565 color       |

**Returns:** nothing.

---

## Text

### `SetTextColor(color)`
Sets the color used for subsequent text drawing.

| Param   | Type | Description        |
|---------|------|--------------------|
| `color` | int  | RGB565 color       |

**Returns:** nothing.

### `SetTextSize(size)`
Sets the text size multiplier (1 = normal).

| Param  | Type | Description            |
|--------|------|------------------------|
| `size` | int  | Size multiplier        |

**Returns:** nothing.

### `SetTextDatum(datum)`
Sets the text datum (anchor point) used for positioning.

| Param   | Type | Description                          |
|---------|------|--------------------------------------|
| `datum` | int  | Datum flag (see TFT_eSPI text datum) |

**Returns:** nothing.

### `SetCursor(x, y)`
Sets the current text cursor position (used by `Print` / `Println`).

| Param | Type   | Description    |
|-------|--------|----------------|
| `x`   | number | Cursor X       |
| `y`   | number | Cursor Y       |

**Returns:** nothing.

### `Print(s)`
Prints text at the current cursor position (no newline).

| Param | Type   | Description    |
|-------|--------|----------------|
| `s`   | string | Text to print  |

**Returns:** nothing.

### `Println(s)`
Prints text at the current cursor position followed by a newline.

| Param | Type   | Description    |
|-------|--------|----------------|
| `s`   | string | Text to print  |

**Returns:** nothing.

---

## Preferences (Persistent Storage)

These functions read/write the device's persistent preferences (NVS), namespaced per game. Values
survive reboots and are useful for saving high scores or user settings.

### `PrefsSetString(key, value)`
Stores a string preference.

| Param   | Type   | Description        |
|---------|--------|--------------------|
| `key`   | string | Preference key     |
| `value` | string | Value to store     |

**Returns:** nothing.

### `PrefsGetString(key, default = "")`
Reads a string preference.

| Param     | Type   | Default | Description                          |
|-----------|--------|---------|--------------------------------------|
| `key`     | string | —       | Preference key                       |
| `default` | string | `""`    | Value returned if the key is absent  |

**Returns:** the stored string.

### `PrefsSetInt(key, value)`
Stores an integer preference.

| Param   | Type | Description        |
|---------|------|--------------------|
| `key`   | string | Preference key    |
| `value` | int  | Value to store     |

**Returns:** nothing.

### `PrefsGetInt(key, default = 0)`
Reads an integer preference.

| Param     | Type | Default | Description                          |
|-----------|------|---------|--------------------------------------|
| `key`     | string | —      | Preference key                       |
| `default` | int  | `0`     | Value returned if the key is absent  |

**Returns:** the stored integer.

### `PrefsSetNumber(key, value)`
Stores a floating-point preference.

| Param   | Type   | Description        |
|---------|--------|--------------------|
| `key`   | string | Preference key     |
| `value` | float  | Value to store     |

**Returns:** nothing.

### `PrefsGetNumber(key, default = 0.0)`
Reads a floating-point preference.

| Param     | Type   | Default | Description                          |
|-----------|--------|---------|--------------------------------------|
| `key`     | string | —       | Preference key                       |
| `default` | float  | `0.0`   | Value returned if the key is absent  |

**Returns:** the stored number.

---

## Game Flow

### `CloseProgressionMenu()`
Signals that the progression menu is finished, allowing the host application to advance.

**Returns:** nothing.

### `CloseWinScreen()`
Signals that the win screen is finished, allowing the host application to advance.

**Returns:** nothing.

---

## Math Utilities

### `Constrain(val, min, max)`
Clamps `val` to the range `[min, max]`.

| Param | Type   | Description              |
|-------|--------|--------------------------|
| `val` | float  | Value to clamp           |
| `min` | float  | Lower bound              |
| `max` | float  | Upper bound              |

**Returns:** the clamped value.

---

## Touch Input

### `IsTouchInZone(x, y, w, h)`
Checks whether the current touch position lies inside a rectangular zone.

| Param | Type   | Description              |
|-------|--------|--------------------------|
| `x`   | number | Zone top-left X          |
| `y`   | number | Zone top-left Y          |
| `w`   | number | Zone width               |
| `h`   | number | Zone height              |

**Returns:** boolean — `true` if the touch is inside the zone.

### `GetTouchX()`
**Returns:** current touch X position (int).

### `GetTouchY()`
**Returns:** current touch Y position (int).

### `GetTouchPressure()`
**Returns:** raw touch pressure value (int, higher = firmer press).

---

## Joystick Input

### `IsJoystickPresent()`
**Returns:** number — non-zero if a joystick is connected, zero otherwise.

### `GetJoystickXY()`
**Returns:** two values — `x` (float), `y` (float). Values are normalized axis positions.

```lua
x, y = GetJoystickXY()
```

### `GetJoystickX()`
**Returns:** joystick X axis value (float).

### `GetJoystickY()`
**Returns:** joystick Y axis value (float).

### `GetJoystickButton()`
**Returns:** boolean — `true` if the joystick button is pressed.

---

## Road (OutRun-style)

These functions support a classic pseudo-3d "road" renderer. Global road state (colors, draw
flags, dimensions) is configured once with the `Set*` functions and then used by the `Draw*`
functions each frame.

### `SetRoadColors(pavementA, pavementB, embankmentA, embankmentB, grassA, grassB, wallA, wallB, railA, railB, lamp, mountain, centerLine)`
Sets the road color palette. The `A`/`B` variants alternate per segment to create a striped
effect. All values are RGB565 colors.

| Param          | Type | Description                     |
|----------------|------|---------------------------------|
| `pavementA`    | int  | Road surface color (segment A)  |
| `pavementB`    | int  | Road surface color (segment B)  |
| `embankmentA`  | int  | Embankment color (segment A)    |
| `embankmentB`  | int  | Embankment color (segment B)    |
| `grassA`       | int  | Grass color (segment A)         |
| `grassB`       | int  | Grass color (segment B)         |
| `wallA`        | int  | Wall color (segment A)          |
| `wallB`        | int  | Wall color (segment B)          |
| `railA`        | int  | Railing color (segment A)       |
| `railB`        | int  | Railing color (segment B)       |
| `lamp`         | int  | Lamp post color                 |
| `mountain`     | int  | Mountain / background color     |
| `centerLine`   | int  | Center line color               |

**Returns:** nothing.

### `SetRoadDrawFlags(drawMountain, drawLamps, drawRails, drawCenterLine, drawTunnelDoors)`
Enables or disables optional road features.

| Param              | Type  | Description                          |
|--------------------|-------|--------------------------------------|
| `drawMountain`     | bool  | Draw the mountain background         |
| `drawLamps`        | bool  | Draw lamp posts                      |
| `drawRails`        | bool  | Draw railings                        |
| `drawCenterLine`   | bool  | Draw the center line                 |
| `drawTunnelDoors`  | bool  | Draw tunnel doors                    |

**Returns:** nothing.

### `SetRoadDimensions(roadWidth, embankmentWidth, centerlineWidth, railingDistance, railingHeight, railingThickness, lampHeight)`
Sets the geometric dimensions of the road.

| Param                | Type   | Description                          |
|----------------------|--------|--------------------------------------|
| `roadWidth`          | number | Width of the road surface            |
| `embankmentWidth`    | number | Width of the embankment              |
| `centerlineWidth`    | number | Width of the center line             |
| `railingDistance`    | number | Distance of railings from road edge  |
| `railingHeight`      | number | Height of the railings               |
| `railingThickness`   | number | Thickness of the railings            |
| `lampHeight`         | number | Height of the lamp posts             |

**Returns:** nothing.

### `DrawRaceOutdoor(x, y, w, roadYOffset, lastX, lastW)`
Draws one segment of the outdoor (daylight) road.

| Param         | Type   | Description                                    |
|---------------|--------|------------------------------------------------|
| `x`           | int    | Segment X position                             |
| `y`           | int    | Segment Y position (screen row)                |
| `w`           | number | Segment width at this row                      |
| `roadYOffset` | int    | Vertical offset of the road at this row        |
| `lastX`       | number | X position of the previous (nearer) segment    |
| `lastW`       | number | Width of the previous (nearer) segment         |

**Returns:** nothing.

### `DrawRaceTunnel(x, y, w, roadYOffset, lastX, lastW)`
Draws one segment of the tunnel (indoor) road. Parameters are identical to `DrawRaceOutdoor`.

| Param         | Type   | Description                                    |
|---------------|--------|------------------------------------------------|
| `x`           | int    | Segment X position                             |
| `y`           | int    | Segment Y position (screen row)                |
| `w`           | number | Segment width at this row                      |
| `roadYOffset` | int    | Vertical offset of the road at this row        |
| `lastX`       | number | X position of the previous (nearer) segment    |
| `lastW`       | number | Width of the previous (nearer) segment         |

**Returns:** nothing.

### `CalculateRoadProperties(y, distance, horizonY, baselineY, roadXOffset)`
Computes the screen position and width of a road segment at a given depth.

| Param         | Type   | Description                                  |
|---------------|--------|----------------------------------------------|
| `y`           | number | Screen row of the segment                    |
| `distance`    | number | Distance/depth of the segment                |
| `horizonY`    | number | Y position of the horizon line               |
| `baselineY`   | number | Y position of the baseline (bottom)          |
| `roadXOffset` | number | Horizontal offset of the road center         |

**Returns:** three values — `x` (number), `w` (number, width), `roadYOffset` (int).

```lua
x, w, roadYOffset = CalculateRoadProperties(y, distance, horizonY, baselineY, roadXOffset)
```

### `ProjectRoadPointToScreen(roadX, roadZ, horizonY, baselineY, roadXOffset)`
Projects a point in road space (lateral `roadX`, depth `roadZ`) to screen coordinates.

| Param         | Type   | Description                                  |
|---------------|--------|----------------------------------------------|
| `roadX`       | number | Lateral position in road space               |
| `roadZ`       | number | Depth in road space                          |
| `horizonY`    | number | Y position of the horizon line               |
| `baselineY`   | number | Y position of the baseline (bottom)          |
| `roadXOffset` | number | Horizontal offset of the road center         |

**Returns:** three values — `x` (number), `y` (number), `scale` (number).

```lua
x, y, scale = ProjectRoadPointToScreen(roadX, roadZ, horizonY, baselineY, roadXOffset)
```

---

## Flag Constants

The `flags` parameter of the scaled / transformed sprite drawing functions is a bitfield. The
available bits (from [`gfxHandler.hpp`](../../T-HMI-PEPmonitor/src/hardware/gfxHandler.hpp)) are:

### Flags for sprite loading functions

| Constant              | Value | Meaning                                          |
|-----------------------|-------|--------------------------------------------------|
| `FLIPPED_H`           | `0x01`| Flip horizontally                                |
| `FLIPPED_V`           | `0x02`| Flip vertically                                  |
| `DITHER_TRANSPARENCY` | `0x04`| Dither the transparency                          |

### Flags for sprite drawing functions

| Constant              | Value | Meaning                                          |
|-----------------------|-------|--------------------------------------------------|
| `ALIGN_H_LEFT`        | `0x00`| Align to the left (default)                      |
| `ALIGN_H_CENTER`      | `0x01`| Align horizontally centered                      |
| `ALIGN_H_RIGHT`       | `0x02`| Align to the right                               |
| `ALIGN_V_TOP`         | `0x00`| Align to the top (default)                       |
| `ALIGN_V_CENTER`      | `0x04`| Align vertically centered                        |
| `ALIGN_V_BOTTOM`      | `0x08`| Align to the bottom                              |
| `TRANSP_OFF`          | `0x00`| Masking-color transparency disabled              |
| `TRANSP_MASK`         | `0x10`| Use the sprite's masking color for transparency  |

> `DrawSprite` always ORs in `TRANSP_MASK` internally, so
> masking-color transparency is on by default for those functions.
