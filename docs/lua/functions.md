# Lua Functions

This document lists every C function that is exposed to Lua games via `lua_register()` in
[`src/games/gameLua.cpp`](../../src/games/gameLua.cpp). All functions are registered as **global**
functions on the Lua state, so they can be called directly by name from any game script.

The standard Lua libraries `table`, `string` and `math` are also available (see
[`gameLua.cpp:1053-1059`](../../src/games/gameLua.cpp#L1053)).

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

### `Log(s)`
Alias for printing a debug message to the serial console (with newline).

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

### `DrawSpriteRegion(handle, tx, ty, sx, sy, sw, sh, alpha = 1)`
Draws a rectangular sub-region of the sprite.

| Param    | Type   | Default | Description                              |
|----------|--------|---------|------------------------------------------|
| `handle` | int    | —       | Sprite handle                            |
| `tx`     | number | —       | Destination X coordinate                 |
| `ty`     | number | —       | Destination Y coordinate                 |
| `sx`     | number | —       | Source X (offset within the sprite)      |
| `sy`     | number | —       | Source Y (offset within the sprite)      |
| `sw`     | number | —       | Source region width                      |
| `sh`     | number | —       | Source region height                     |
| `alpha`  | float  | `1`     | Opacity, `0`–`1`                         |

**Returns:** nothing.

### `DrawSpriteTransformed(handle, x, y, a, b, c, d, flags = 0)`
Draws a sprite using an arbitrary 2×2 transformation matrix:

```
[ a  b ]       [ x' ]   [ a*x + b*y ]
[ c  d ]  ·  ( y' ) = ( c*x + d*y )
```

The result is then translated to `(x, y)`. Transparency via masking color is always enabled.
Use this for full control (shear, non-uniform transforms) that `DrawSprite`'s scale+angle
cannot express.

| Param    | Type   | Default | Description                                        |
|----------|--------|---------|----------------------------------------------------|
| `handle` | int    | —       | Sprite handle                                      |
| `x`      | number | —       | Translation X                                      |
| `y`      | number | —       | Translation Y                                      |
| `a`      | number | —       | Matrix element (1,1)                               |
| `b`      | number | —       | Matrix element (1,2)                               |
| `c`      | number | —       | Matrix element (2,1)                               |
| `d`      | number | —       | Matrix element (2,2)                               |
| `flags`  | int    | `0`     | Draw flags (see [Flag Constants](#flag-constants)) |

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

## Graphics Primitives

All primitives draw to the current draw target. Colors are 16-bit RGB565 values (e.g. `TFT_RED`).

### `DrawString(str, x, y)`
Draws text at `(x, y)`. Multi-line strings (containing `\n`) are supported; each line advances by
one font height. The `€` character is substituted with `¶`.

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

### `FillScreen(color)`
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
Prints text at the current cursor position (no newline). The `€` character is substituted with
`¶`.

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

### `Cls()`
Clears the current draw target by filling it with black (`TFT_BLACK`).

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
available bits (from [`gfxHandler.hpp`](../../src/hardware/gfxHandler.hpp)) are:

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

> The scaled/transformed wrappers always OR in `TRANSP_MASK` internally, so masking-color
> transparency is on by default for those functions.
