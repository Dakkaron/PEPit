SBackground = LoadSprite("gfx/background.bmp")
SPlane = LoadSprite("gfx/plane.bmp", 0, 0xf81f)
SPlaneBankL = LoadSprite("gfx/planeBankL.bmp", 0, 0xf81f)
SPlaneBankR = LoadSprite("gfx/planeBankR.bmp", 0, 0xf81f)
SSailShip = LoadSprite("gfx/sailship.bmp", 0, 0xf81f)
SDownArrow = LoadSprite("gfx/downArrow.bmp", 0, 0xf81f)
CameraAngle = 0
CameraX = 0
CameraY = 0
CameraHeight = 1
Speed = 0

TargetShip = 1

HorizonHeight = 50

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
    DrawSprite(SDownArrow, x-SpriteWidth(SDownArrow), 25 + math.sin(Ms/100))
  end
end