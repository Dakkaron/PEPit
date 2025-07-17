SBackground = LoadSprite("gfx/background.bmp")
SPlane = LoadSprite("gfx/plane.bmp", 0, 0xf81f)
SPlaneBankL = LoadSprite("gfx/planeBankL.bmp", 0, 0xf81f)
SPlaneBankR = LoadSprite("gfx/planeBankR.bmp", 0, 0xf81f)
SSailShip = LoadSprite("gfx/sailship.bmp", 0, 0xf81f)
SDownArrow = LoadSprite("gfx/downArrow.bmp", 0, 0xf81f)
SRing = LoadSprite("gfx/ring.bmp", 0, 0xf81f)
CameraAngle = 0
CameraX = 0
CameraY = 0
CameraHeight = 1
Speed = 0

TargetShip = 1
Money = PrefsGetInt("money", 0)

HorizonHeight = 50

ShipPositions = {
  {0, 100},
  {400, 400},
  {0, 380},
  {400, 100},
  {200, 200}
}

function calcDistance2D(x1, y1, x2, y2)
  local dx = x1-x2
  local dy = y1-y2
  return math.sqrt(dx*dx + dy*dy)
end

function drawShip(sprite, worldX, worldY, drawArrow)
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
  elseif (drawArrow) then
    DrawSprite(SDownArrow, x-SpriteWidth(SDownArrow)/2, 25 + math.sin(Ms/100))
  end
  if (distance<8) then
    DrawSpriteScaled(SRing, x, 120, scale, scale, 0x05)
  elseif (distance<50) then
    DrawSpriteScaled(SRing, x, y-100*scale, scale, scale, 0x09)
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