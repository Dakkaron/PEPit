-- System & Debug
function SerialPrintln(s) end
function SerialPrint(s) end
function Log(s) end
function GetFreeRAM() return 0; end
function GetFreePSRAM() return 0; end
function DisableCaching() end

-- Scripting
function RunScript(path) end

-- Sprites: Loading & Freeing
function LoadSprite(path, options, maskingColor) return -1; end
function LoadAnimSprite(path, frameW, frameH, options, maskingColor) return -1; end
function FreeSprite(handle) end
function GetFreeSpriteSlots() return 0; end

-- Sprites: Drawing
function DrawSprite(handle, x, y, opts) end
function DrawSpriteRegion(handle, tx, ty, sx, sy, sw, sh, alpha) end
function DrawSpriteTransformed(handle, x, y, a, b, c, d, flags) end

-- Sprites: Info & Draw Target
function SpriteWidth(handle) return 0; end
function SpriteHeight(handle) return 0; end
function SetDrawTargetSprite(handle) end
function SetDrawTargetFramebuffer() end

-- Mode 7 (3D Ground)
function DrawMode7(textureHandle, camX, camY, camHeight, yawAngle, zoom, horizonHeight, startY, endY) end
function Mode7WorldToScreen(worldX, worldY, camX, camY, camHeight, yawAngle, zoom, horizonHeight, startY, endY) return 0, 0, 0.0; end

-- Graphics Primitives
function DrawString(str, x, y) end
function DrawRect(x, y, w, h, color) end
function FillRect(x, y, w, h, color) end
function DrawCircle(x, y, r, color) end
function FillCircle(x, y, r, color) end
function DrawLine(x0, y0, x1, y1, color) end
function DrawFastHLine(x, y, w, color) end
function DrawFastVLine(x, y, h, color) end
function FillScreen(color) end

-- Text
function SetTextColor(color) end
function SetTextSize(size) end
function SetTextDatum(datum) end
function SetCursor(x, y) end
function Print(s) end
function Println(s) end
function Cls() end

-- Preferences (Persistent Storage)
function PrefsSetString(key, value) end
function PrefsGetString(key, default) return ""; end
function PrefsSetInt(key, value) end
function PrefsGetInt(key, default) return 0; end
function PrefsSetNumber(key, value) end
function PrefsGetNumber(key, default) return 0.0; end

-- Game Flow
function CloseProgressionMenu() end
function CloseWinScreen() end

-- Math Utilities
function Constrain(val, min, max) return 0.0; end

-- Touch Input
function IsTouchInZone(x, y, w, h) return false; end
function GetTouchX() return 0; end
function GetTouchY() return 0; end
function GetTouchPressure() return 0; end

-- Joystick Input
function IsJoystickPresent() return 0; end
function GetJoystickXY() return 0.0, 0.0; end
function GetJoystickX() return 0.0; end
function GetJoystickY() return 0.0; end
function GetJoystickButton() return false; end

-- Road (OutRun-style)
function SetRoadColors(pavementA, pavementB, embankmentA, embankmentB, grassA, grassB, wallA, wallB, railA, railB, lamp, mountain, centerLine) end
function SetRoadDrawFlags(drawMountain, drawLamps, drawRails, drawCenterLine, drawTunnelDoors) end
function SetRoadDimensions(roadWidth, embankmentWidth, centerlineWidth, railingDistance, railingHeight, railingThickness, lampHeight) end
function DrawRaceOutdoor(x, y, w, roadYOffset, lastX, lastW) end
function DrawRaceTunnel(x, y, w, roadYOffset, lastX, lastW) end
function CalculateRoadProperties(y, distance, horizonY, baselineY, roadXOffset) return 0.0, 0.0, 0; end
function ProjectRoadPointToScreen(roadX, roadZ, horizonY, baselineY, roadXOffset) return 0.0, 0.0, 0.0; end

-- Injected variables (globals set by the host each frame)
Ms = 0
LeftHandedMode = false
MsDelta = 0
CycleNumber = 0
TotalCycleNumber = 0
CurrentRepetition = 0
NewRepetition = false
CumulatedTaskNumber = 0
TaskNumber = 0
TotalTaskNumber = 0
CurrentlyBlowing = false
BlowStartMs = 0
BlowEndMs = 0
TargetDurationMs = 0
Repetitions = 0
Pressure = 0
PeakPressure = 0
MinPressure = 0
CumulativeError = 0
Fails = 0
TaskType = 0
LastBlowStatus = 0
TotalTimeSpentBreathing = 0
TaskStartMs = 0
IsNewTask = false
BreathingScore = 0
CurrentlyJumping = false
CurrentBonusRepetition = 0
NewBonusRepetition = false
MsLeft = 0
LastJumpMs = 0
