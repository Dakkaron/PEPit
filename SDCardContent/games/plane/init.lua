SBackground = LoadSprite("gfx/background.bmp")
SSky = LoadSprite("gfx/sky.bmp")
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
SBird = LoadSprite("gfx/bird.bmp", 0, 0xf81f)
CameraAngle = 0
CameraX = 0
CameraY = 0
CameraHeight = 1
Speed = 100
TopFlightHeight = 2

DrowningStartMs = 0
TargetShip = 1
Money = PrefsGetInt("money", 0)

HorizonHeight = 49

BIRD_SPEED = 20

OBJECT_TYPE_SHIP = 1
OBJECT_TYPE_STORM = 2
OBJECT_TYPE_BIRD = 3

Objects = {
  {type = OBJECT_TYPE_SHIP, x = 0,   y = 100, d = 0, id = 1},
  {type = OBJECT_TYPE_SHIP, x = 400, y = 400, d = 0, id = 2},
  {type = OBJECT_TYPE_SHIP, x = 0,   y = 380, d = 0, id = 3},
  {type = OBJECT_TYPE_SHIP, x = 400, y = 100, d = 0, id = 4},
  {type = OBJECT_TYPE_SHIP, x = 200, y = 200, d = 0, id = 5},
  {type = OBJECT_TYPE_STORM, x = 200, y = 300, d = 0},
  {type = OBJECT_TYPE_BIRD, x = 0, y = 80, d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  --{ObjectTypeStorm, 50, 300, 0, 2}
}


function calcDistance2D(x1, y1, x2, y2)
  local dx = x1-x2
  local dy = y1-y2
  return math.sqrt(dx*dx + dy*dy)
end

function drawBillboard(sprite, worldX, worldY, worldHeight, baseScale, hflip)
  local x,y,z = Mode7WorldToScreen(worldX,worldY, CameraX,CameraY,CameraHeight,CameraAngle, 1, HorizonHeight, 20,180)
  local dx = worldX - CameraX
  local dy = worldY - CameraY
  local dz = -worldHeight * 35--(worldHeight - CameraHeight) * 5
  local distance = math.sqrt(dx*dx + dy*dy)
  scale = 15/distance
  local sy = y + dz * scale + dz * scale
  if (distance < 50) then
    DrawString("y  " .. y, 250, 20)
    DrawString("dz " .. dz, 250, 30)
    DrawString("sy " .. sy, 250, 40)
  end
  DrawSpriteScaled(sprite, x, sy, hflip and -scale*baseScale or scale*baseScale, scale*baseScale, 0x05)
end

function drawShip(sprite, worldX, worldY)
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
end

function drawTarget(sprite, worldX, worldY)
  --local x,y,z = Mode7WorldToScreen(worldX,worldY, CameraX,CameraY,CameraHeight,CameraAngle, 1, HorizonHeight, 20,180)
  --if (x==-1000 and y==-1000) then
  --  return
  --end
  local dx = CameraX - worldX
  local dy = CameraY - worldY
  local distance = math.sqrt(dx*dx + dy*dy)
  --if (distance<8 and (TopFlightHeight - CameraHeight) < (TopFlightHeight/10.0)) then
  --  DrawSpriteScaled(SRing, x, 120, scale, scale, 0x05)
  --elseif (distance<50) then
    --DrawSpriteScaled(SRing, x, y-100*scale, scale, scale, 0x09)
  --else
    --DrawSprite(SDownArrow, x-SpriteWidth(SDownArrow)/2, 25 + math.sin(Ms/100))
  --end
  if (distance<50) then
    drawBillboard(SRing, worldX, worldY, 2, 1, false)
  else
    local x,y,z = Mode7WorldToScreen(worldX,worldY, CameraX,CameraY,CameraHeight,CameraAngle, 1, HorizonHeight, 20,180)
    if (x==-1000 and y==-1000) then
      return
    end
    DrawSprite(SDownArrow, x-SpriteWidth(SDownArrow)/2, 25 + math.sin(Ms/100))
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

function getObjectRelativeDirection(x, y)
  local targetAngle = math.atan(y, x)
  local angleDiff = CameraAngle - targetAngle
  if (angleDiff<-math.pi) then
    angleDiff = angleDiff + 2*math.pi
  end
  return angleDiff
end

function getObjectMotionVector(object)
  dx = object.targetX - object.x
  dy = object.targetY - object.y
  m = math.sqrt(dx*dx + dy*dy)
  return dx/m, dy/m
end

-- Progression Menu
PART_TYPE_PLANE = 1
PART_TYPE_PROPELLER = 2
PART_TYPE_ENGINE = 3
PART_TYPE_WINGS = 4

function DisplayBuyPart(price, partType)
  if (Money < price) then
    FillRect(10, 60, 220, 35, 0xF800)
  else
    FillRect(10, 60, 220, 35, 0x001F)
    SetTextSize(1)
    DrawString("Nicht genug Geld", 15, 84)
  end

  SetTextSize(2)
  DrawImage()
  if (Money >= price and IsTouchInZone(10, 60, 220, 35)) then
    Money = Money - price
    Stage = Stage + 1
    PrefsSetInt("stage", Stage)
    PrefsSetInt("money", Money)
  end
end