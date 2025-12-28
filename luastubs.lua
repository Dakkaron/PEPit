function SerialPrintln(s) end
function SerialPrint(s) end
function RunScript(path, flipped, maskingColor) end
function LoadSprite(path, flipped, maskingColor) end
function LoadAnimSprite(path, frameW, frameH, flipped, maskingColor) end
function FreeSprite(handle) end
function DrawSprite(handle, x, y) end
function DrawSpriteRegion(handle, tx, ty, sx, sy, sw, sh) end
function DrawAnimSprite(handle, x, y, frame) end
function DrawSpriteToSprite(srcHandle, dstHandle, x, y) end
function DrawSpriteScaled(handle, x, y, scaleX, scaleY, flags) end
function DrawAnimSpriteScaled(handle, x, y, scaleX, scaleY, frame, flags) end
function DrawSpriteScaledRotated(handle, x, y, scaleX, scaleY, angle, flags) end
function DrawAnimSpriteScaledRotated(handle, x, y, scaleX, scaleY, angle, frame, flags) end
function DrawSpriteTransformed(handle, x, y, matrixA, matrixB, matrixC, matrixD, flags) end
function SpriteWidth(handle) return 0; end
function SpriteHeight(handle) return 0; end
function Log(s) end
function DrawString(s, x, y) end
function DrawRect(x, y, w, h, color) end
function FillRect(x, y, w, h, color) end
function DrawCircle(x, y, r, color) end
function FillCircle(x, y, r, color) end
function DrawLine(x, y, x2, y2, color) end
function DrawFastHLine(x, y, w, color) end
function DrawFastVLine(x, y, h, color) end
function FillScreen(color) end
function SetTextColor(color) end
function SetTextSize(size) end
function SetTextDatum(d) end
function SetCursor(x, y) end
function Print(s) end
function Println(s) end
function Cls() end
function PrefsSetString(name, value) end
function PrefsGetString(name, default) return ""; end
function PrefsSetInt(name, value) end
function PrefsGetInt(name, default) return 0; end
function PrefsSetNumber(name, value) end
function PrefsGetNumber(name, default) return 0.0; end
function CloseProgressionMenu() end
function Constrain(val, min, max) return 0.0; end
function IsTouchInZone(x, y, w, h) return false; end
function GetTouchX() return 0; end
function GetTouchY() return 0; end
function GetTouchPressure() return 0; end
function GetFreeRAM() return 0; end
function DisableCaching() end
function Mode7WorldToScreen(worldX, worldY, camX, camY, camH, yawAngle, zoom, horizonHeight, startY, endY) return 0,0,0.0; end
function DrawMode7(groundTextureHandle, camX,  camY, camH, yawAngle, zoom, horizonHeight, startY, endY) end
function SetRoadColors(pavementA, pavementB, embankmentA, embankmentB, grassA, grassB, wallA, wallB, railA, railB, lamp, mountain, centerline) end
function SetRoadDrawFlags(drawMountain, drawLamps, drawRails, drawCenterline, drawTunnelDoors) end
function SetRoadDimensions(roadWidth, embankmentWidth, centerlineWidth, railingDistance, railingHeight, railingThickness, lampHeight) end
function DrawRaceOutdoor(x, y, w, roadXOffset, lastX, lastW) end
function DrawRaceTunnel(x, y, w, roadXOffset, lastX, lastW) end
function CalculateRoadProperties(y, distance, horizonY, baselineY, roadXOffset) return 0.0,0.0,0; end
function ProjectRoadPointToScreen(roadX, roadY, horizonY, baselineY, roadXOffset) return 0.0,0.0,0.0; end

CurrentlyBlowing = false
Ms = 0
MsDelta = 0
BlowStartMs = 0
BlowEndMs = 0
TargetDurationMs = 0
CycleNumber = 0
TotalCycleNumber = 0
CurrentRepetition = 0
NewRepetition = 0
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
CumulatedTaskNumber = 0
TaskNumber = 0
TotalTaskNumber = 0
IsNewTask = 0
BreathingScore = 0

CurrentlyJumping = false
MsLeft = 0
LastJumpMs = 0
