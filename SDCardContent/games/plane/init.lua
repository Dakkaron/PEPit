SBackground = LoadSprite("gfx/background.bmp")
SSky = LoadSprite("gfx/sky.bmp")
SPlane = {
  LoadSprite("gfx/plane.bmp", 0, 0xf81f),
  LoadSprite("gfx/plane2.bmp", 0, 0xf81f),
  LoadSprite("gfx/plane3.bmp", 0, 0xf81f),
}
SShip = {
  LoadSprite("gfx/ship1.bmp", 0, 0xf81f),
  LoadSprite("gfx/ship2.bmp", 0, 0xf81f)
}
SDownArrow = LoadSprite("gfx/downArrow.bmp", 0, 0xf81f)
SRing = LoadSprite("gfx/ring.bmp", 0, 0xf81f)
SSplash = {
  LoadSprite("gfx/splash1.bmp", 0, 0xf81f),
  LoadSprite("gfx/splash2.bmp", 0, 0xf81f),
}
SStorm = LoadSprite("gfx/storm.bmp", 0, 0xf81f)
SBird = LoadSprite("gfx/bird.bmp", 0, 0xf81f)

SSpeedDial = LoadSprite("gfx/speeddial.bmp", 0, 0xf81f)
SSpeedDialNeedle = LoadSprite("gfx/speeddial_needle.bmp", 0, 0xf81f)

SUpgradeProp = LoadSprite("gfx/propeller.bmp", 0, 0xf81f)
SUpgradeEngine = LoadSprite("gfx/engine.bmp", 0, 0xf81f)
SUpgradeControl = LoadSprite("gfx/control.bmp", 0, 0xf81f)
SButtonUp = LoadSprite("gfx/buttonUp.bmp", 0, 0xf81f)
SButtonDown = LoadSprite("gfx/buttonDown.bmp", 0, 0xf81f)

PlaneAngle = 0
CameraAngle = 0
CameraX = 0
CameraY = 0
CameraHeight = 1
Speed = 100
TopFlightHeight = 2

DrowningStartMs = 0
TargetShip = 1
Money = PrefsGetInt("money", 0)
PlaneType = PrefsGetInt("uplane", 0) + 1
ShipType = PrefsGetInt("uship", 0) + 1
UpgradeSpeed = PrefsGetInt("uprop", 0) + PrefsGetInt("uengine", 0) + PlaneType
UpgradeTurn = PrefsGetInt("usurf", 0) + PlaneType

HorizonHeight = 49

BIRD_SPEED = 5

OBJECT_TYPE_SHIP = 1
OBJECT_TYPE_STORM = 2
OBJECT_TYPE_BIRD = 3

Objects = {
  {type = OBJECT_TYPE_SHIP, x = 0,   y = 100, d = 0, id = 1, shipType = math.random(1, ShipType)},
  {type = OBJECT_TYPE_SHIP, x = 400, y = 450, d = 0, id = 2, shipType = math.random(1, ShipType)},
  {type = OBJECT_TYPE_SHIP, x = 0,   y = 380, d = 0, id = 3, shipType = math.random(1, ShipType)},
  {type = OBJECT_TYPE_SHIP, x = 400, y = 50, d = 0, id = 4, shipType = math.random(1, ShipType)},
  {type = OBJECT_TYPE_SHIP, x = 200, y = 200, d = 0, id = 5, shipType = math.random(1, ShipType)},
  {type = OBJECT_TYPE_STORM, x = 200, y = 300, d = 0},
  {type = OBJECT_TYPE_BIRD, x = 0, y = 80, d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
  {type = OBJECT_TYPE_BIRD, x = math.random(10, 450), y = math.random(10, 450), d = 0, targetX = math.random(10, 450), targetY = math.random(10, 450)},
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
  local dz = -worldHeight * 35
  local distance = math.sqrt(dx*dx + dy*dy)
  scale = 15/distance
  local sy = y + dz * scale + dz * scale
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

function drawTarget(worldX, worldY)
  local dx = CameraX - worldX
  local dy = CameraY - worldY
  local distance = math.sqrt(dx*dx + dy*dy)
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

AllUpgrades = {
  {
    id = 1,
    text = "Propeller",
    img = SUpgradeProp,
    cost = 400,
    prop = "uprop",
    upto = 1,
    req = {}
  },
  {
    id = 2,
    text = "Motor",
    img = SUpgradeEngine,
    cost = 500,
    prop = "uengine",
    upto = 2,
    req = {}
  },
  {
    id = 3,
    text = "Klappen",
    img = SUpgradeControl,
    cost = 550,
    prop = "usurf",
    upto = 2,
    req = {}
  },
  {
    id = 4,
    text = "Flugzeug",
    img = SPlane[PlaneType+1],
    cost = 1500,
    prop = "uplane",
    upto = 1,
    req = {
      {1,1}, {2,2}, {3,2}
    }
  },
  {
    id = 5,
    text = "Propeller",
    img = SUpgradeProp,
    cost = 800,
    prop = "uprop",
    upto = 2,
    req = {{4,1}}
  },
  {
    id = 6,
    text = "Motor",
    img = SUpgradeEngine,
    cost = 900,
    prop = "uengine",
    upto = 4,
    req = {{4,1}}
  },
  {
    id = 7,
    text = "Klappen",
    img = SUpgradeControl,
    cost = 950,
    prop = "usurf",
    upto = 4,
    req = {{4,1}}
  },
  {
    id = 8,
    text = "Schiffe",
    img = SShip[ShipType+1],
    cost = 800,
    prop = "uship",
    upto = 1,
    req = {}
  },
  {
    id = 9,
    text = "Flugzeug",
    img = SPlane[PlaneType+1],
    cost = 3500,
    prop = "uplane",
    upto = 2,
    req = {
      {5,2}, {6,4}, {7,4}
    }
  },
}

CachedValidUpdates = nil
function GetValidUpgrades()
  if (CachedValidUpdates ~= nil) then
    return CachedValidUpdates
  end
  local out = {}
  local nr = 1
  for i = 1, #AllUpgrades do
    local upgrade = AllUpgrades[i]
    local reqs = upgrade.req
    local reqOk = upgrade.upto > PrefsGetInt(upgrade.prop)
    SerialPrintln(upgrade.text .. " = " .. tostring(reqOk))
    for j = 1, #reqs do
      SerialPrintln(AllUpgrades[reqs[j][1]].prop .. "->" .. PrefsGetInt(AllUpgrades[reqs[j][1]].prop, 0) .. "<" .. reqs[j][2])
      if reqOk and PrefsGetInt(AllUpgrades[reqs[j][1]].prop, 0) < reqs[j][2] then
        reqOk = false
        SerialPrintln("reqOk = False")
      end
    end
    if (reqOk) then
      out[nr] = upgrade
      nr = nr + 1
    end
  end
  CachedValidUpdates = out
  return out
end

UpgradeMenuOffset = 0
TouchBlocked = false
function DisplayValidUpgrades()
  if TouchBlocked and not IsTouchInZone(0, 0, 320, 240) then
    TouchBlocked = false
  end
  local upgrades = GetValidUpgrades()
  if #upgrades >= 3 and UpgradeMenuOffset>#upgrades-3 then
    UpgradeMenuOffset = #upgrades-3
  end
  if UpgradeMenuOffset>0 then
    DrawSprite(SButtonUp, 275, 35)
    if IsTouchInZone(275, 35, 32, 32) and not TouchBlocked then
      UpgradeMenuOffset = UpgradeMenuOffset - 1
      TouchBlocked = true
    end
  end
  if UpgradeMenuOffset<#upgrades-3 then
    DrawSprite(SButtonDown, 275, 150)
    if IsTouchInZone(275, 150, 32, 32) and not TouchBlocked then
      UpgradeMenuOffset = UpgradeMenuOffset + 1
      TouchBlocked = true
    end
  end
  for i = 1, math.min(3,#upgrades) do
    local upgrade = upgrades[i + UpgradeMenuOffset]
    local yPos = 30 + (i-1)*64
    if (Money < upgrade.cost) then
      FillRect(10, yPos, 220, 60, 0xF800)
    else
      FillRect(10, yPos, 220, 60, 0x001F)
      if IsTouchInZone(10, yPos, 220, 60) and not TouchBlocked then
        TouchBlocked = true
        Money = Money - upgrade.cost
        PrefsSetInt("money", Money)
        PrefsSetInt(upgrade.prop, PrefsGetInt(upgrade.prop, 0) + 1)
        CachedValidUpdates = nil
      end
    end
    SetTextSize(1)
    local scale = 1
    local maxDim = math.max(SpriteWidth(upgrade.img),SpriteHeight(upgrade.img))
    scale = 60.0/maxDim
    DrawSpriteScaled(upgrade.img, 42, yPos+30, scale, scale, 0x05)
    SetTextSize(2)
    DrawString(upgrade.text, 78, yPos+2)
    DrawString("Stufe " .. (PrefsGetInt(upgrade.prop, 0)+1), 78, yPos+20)
    DrawString("$"..upgrade.cost, 230 - #("$"..upgrade.cost)*12, yPos+40)
    SetTextSize(1)
  end
end
