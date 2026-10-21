local p = IsJoystickPresent()
local x, y = GetJoystickXY()
local b = GetJoystickButton()
if math.abs(x) > 0.1 or math.abs(y) > 0.1 or b then
  SerialPrintln((p and "P " or "A ") .. x .. "/" .. y .. (b and " D" or " U" ))
end
DrawCircle(140, 100, 40, 0xFFFF)
FillCircle(140 + x*60, 100 + y*60, 40, 0xFFFF)

if IsTouchInZone(0,0,320,240) then
  DrawRect(GetTouchX()-5, GetTouchY()-5, 10, 10, 0xFFFF)
end