SBackground = LoadSprite("gfx/background.bmp")
SPlane = LoadSprite("gfx/plane.bmp", 0, 0xf81f)
SPlaneBankL = LoadSprite("gfx/planeBankL.bmp", 0, 0xf81f)
SPlaneBankR = LoadSprite("gfx/planeBankR.bmp", 0, 0xf81f)
SSailShip = LoadSprite("gfx/sailship.bmp", 0, 0xf81f)
SDownArrow = LoadSprite("gfx/downArrow.bmp", 0, 0xf81f)
SRing = LoadSprite("gfx/ring.bmp", 0, 0xf81f)
SSplash = {
  LoadSprite("gfx/splash1.bmp", 0, 0xf81f),
  LoadSprite("gfx/splash2.bmp", 0, 0xf81f),
}
SStorm = LoadSprite("gfx/storm.bmp", 0, 0xf81f)
CameraAngle = 0
CameraX = 0
CameraY = 0
CameraHeight = 1
Speed = 100

TargetShip = 1
Money = PrefsGetInt("money", 0)

HorizonHeight = 50

ObjectTypeShip = 1
ObjectTypeStorm = 2

Objects = {
  {ObjectTypeShip, 0, 100, 0, 1},
  {ObjectTypeShip, 400, 400, 0, 2},
  {ObjectTypeShip, 0, 380, 0, 3},
  {ObjectTypeShip, 400, 100, 0, 4},
  {ObjectTypeShip, 200, 200, 0, 5}
  --{ObjectTypeStorm, 0, 200, 0, 1},
  --{ObjectTypeStorm, 50, 300, 0, 2}
}


function calcDistance2D(x1, y1, x2, y2)
  local dx = x1-x2
  local dy = y1-y2
  return math.sqrt(dx*dx + dy*dy)
end

function drawBillboard(sprite, worldX, worldY, worldHeight, baseScale)
  local x,y,z = Mode7WorldToScreen(worldX,worldY, CameraX,CameraY,CameraHeight,CameraAngle, 1, HorizonHeight, 20,180)
  local dx = worldX - CameraX
  local dy = worldY - CameraY
  local dz = (worldHeight - CameraHeight) * 5
  local distance = math.sqrt(dx*dx + dy*dy)
  scale = 15/distance
  local sy = y + dz * scale
  DrawSpriteScaled(sprite, x, sy, scale*baseScale, scale*baseScale, 0x09)
  DrawString("x ".. x, 30, 70)
  DrawString("y ".. sy, 30, 80)
  DrawString("s ".. scale, 30, 90)
end

function drawShip(sprite, worldX, worldY, isTarget)
  local x,y,z = Mode7WorldToScreen(worldX,worldY, CameraX,CameraY,CameraHeight,CameraAngle, 1, HorizonHeight, 20,180)
  if (x==-1000 and y==-1000) then
    return
  end
  local dx = CameraX - worldX
  local dy = CameraY - worldY
  local distance = math.sqrt(dx*dx + dy*dy)
  local distance3d = math.sqrt(dx*dx + dy*dy + CameraHeight*CameraHeight*5)
  scale = 15/distance3d
  y = math.max(y, 70)
  if (distance<110) then
    DrawSpriteScaled(sprite, x, y, scale, scale, 0x09)
    if (y+SpriteHeight(sprite) > 180) then
      FillRect(x-SpriteWidth(sprite)*scale, 181, SpriteWidth(sprite)*scale*2, y+SpriteWidth(sprite)*scale-181, 0x0000)
    end
  end
  if (isTarget) then
    if (distance<8) then
      DrawSpriteScaled(SRing, x, 120, scale, scale, 0x05)
    elseif (distance<50) then
      DrawSpriteScaled(SRing, x, y-100*scale, scale, scale, 0x09)
    else
      DrawSprite(SDownArrow, x-SpriteWidth(SDownArrow)/2, 25 + math.sin(Ms/100))
    end
  end
end

EarnValue = 0
EarnTime = 0

function AddEarnings(value)
  EarnValue = EarnValue + value
  EarnTime = Ms + 2000
  Money = Money + value
end

function DisplayEarnings(x, y)
  if (EarnTime > Ms) then
    SetTextSize(2)
    if (EarnValue > 0) then
      DrawString("+" .. EarnValue, x, y + (EarnTime - Ms) * 0.01)
    else
      DrawString("" .. EarnValue, x, y + (EarnTime - Ms) * 0.01)
    end
  else
    EarnValue = 0
  end
end
