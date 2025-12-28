function SerializeByteList(l)
  local out = ""
  for i=1,#l do
    out = out .. string.format("%02x", l[i] % 256)
  end
  return out
end

function DeserializeByteList(s)
  local out = {}
  for i=1,#s/2 do
    local val = string.sub(s, i*2-1, i*2)
    out[i] = tonumber("0x"..val)
  end
  return out
end

COAT_R = 1
COAT_B = 2
COAT_Y = 3
HELMET_R = 1
HELMET_G = 2
HELMET_B = 3
HORSE_BAY = 1
HORSE_BLACK = 2
HORSE_SORREL = 3
NUM_HORSE_COLORS = 2

function CreateHorseSprite(colorHorse, colorCoat, colorHelmet)
  local sprite, addSprite
  if colorHorse == HORSE_BAY then
	sprite = LoadAnimSprite("gfx/horse/bay.bmp", 23, 81, 0, 0xf81f)
  elseif colorHorse == HORSE_BLACK then
    sprite = LoadAnimSprite("gfx/horse/black.bmp", 23, 81, 0, 0xf81f)
  end
  if colorCoat == COAT_R then
    addSprite = LoadAnimSprite("gfx/coat/red.bmp", 23, 81, 0, 0xf81f)
  elseif colorCoat == COAT_B then
    addSprite = LoadAnimSprite("gfx/coat/blue.bmp", 23, 81, 0, 0xf81f)
  elseif colorCoat == COAT_Y then
    addSprite = LoadAnimSprite("gfx/coat/yellow.bmp", 23, 81, 0, 0xf81f)
  end
  DrawSpriteToSprite(addSprite, sprite, 0, 0)
  FreeSprite(addSprite)
  if colorHelmet == HELMET_R then
    addSprite = LoadAnimSprite("gfx/helmet/red.bmp", 23, 81, 0, 0xf81f)
  elseif colorHelmet == HELMET_G then
    addSprite = LoadAnimSprite("gfx/helmet/green.bmp", 23, 81, 0, 0xf81f)
  elseif colorHelmet == HELMET_B then
    addSprite = LoadAnimSprite("gfx/helmet/blue.bmp", 23, 81, 0, 0xf81f)
  end
  DrawSpriteToSprite(addSprite, sprite, 0, 0)
  FreeSprite(addSprite)
  addSprite = LoadAnimSprite("gfx/horse_extra.bmp", 23, 81, 0, 0xf81f)
  DrawSpriteToSprite(addSprite, sprite, 0, 0)
  FreeSprite(addSprite)
  return sprite
end

ROAD_W = 140
BANK_W = 50
DRAW_HORIZON = 60
HORIZON_Y = 52
BASELINE_Y = 190
Distance = 0
RoadXOffset = 0
RoadXOffsetDirection = 1

PlayerX = 0

Speed = 0.01

SSky = LoadSprite("gfx/sky.bmp")
STree = LoadSprite("gfx/tree.bmp", 0, 0xf81f)
SHorseSide = {LoadSprite("gfx/horse_side/bay.bmp", 0, 0xf81f), LoadSprite("gfx/horse_side/black.bmp", 0, 0xf81f), LoadSprite("gfx/horse_side/sorrel.bmp", 0, 0xf81f)}
SHorse = {CreateHorseSprite(math.random(1,2), math.random(1,3), math.random(1,3)), CreateHorseSprite(math.random(1,2), math.random(1,3), math.random(1,3)), CreateHorseSprite(math.random(1,2), math.random(1,3), math.random(1,3)), CreateHorseSprite(math.random(1,2), math.random(1,3), math.random(1,3)), CreateHorseSprite(math.random(1,2), math.random(1,3), math.random(1,3))}
SHorseAngledLeft = LoadAnimSprite("gfx/horse_angled_right.bmp", 42, 80, 1, 0xf81f)
SHorseAngledRight = LoadAnimSprite("gfx/horse_angled_right.bmp", 42, 80, 0, 0xf81f)

CurrentLeague = PrefsGetInt("league", 5)
OwnPoints = PrefsGetInt("points", 0)

Money = PrefsGetInt("money", 0)
PrizeMoney = {100, 72, 60, 48, 40, 32}

PlayerHorse = {
  name = PrefsGetString("hname", "Free Spirit"),
  speed = PrefsGetInt("hspeed", 1),
  color = PrefsGetInt("hcolor", 2),
  sprite = CreateHorseSprite(PrefsGetInt("hcolor", 2), PrefsGetInt("hcoat", 1), PrefsGetInt("hhelmet", 2))
}

if CurrentLeague == 5 then
  CompetitorNames = {
    "Victor Espinosa", "Anthony Darman", "Gustavo E Calvante", "Harry Coffan", "Maximiliano Aserin", "Jose Lezcan", "Callum Rodrigez", "P J MacDonen", "Joaquin Herran", "Craig Gryllan", "Billy Eganes", "Brad Parnum", "Erik Asmundsen", "Jason Harte", "Ryan Malone", "Antonio Oranis", "Patrick Carbin", "Ben Allain", "D S Machada", "Christian A Torrez", "Genki Maruami", "Masami Matsuoro", "Lukas Delozier"
  }
end

CompPoints = DeserializeByteList(PrefsGetString("lp", "0000000000000000000000000000000000000000000000")) -- 23 competitor scores in hex
LeagueCompetitors = {}
for i=1,#CompetitorNames do
	LeagueCompetitors[i] = {n = i, p = CompPoints[i] or 0}
end
table.sort(LeagueCompetitors, function (e1, e2) return e1.p < e2.p end)

OwnPos = 1
for i=1,#CompetitorNames do
	if (OwnPoints < LeagueCompetitors[i].p) then
      OwnPos = #CompetitorNames+2-i
      break
    end
end

Competitors = {}

for i=1, 5 do
  CompPos = (OwnPos-1)-((OwnPos-1)%6)+i
  Competitors[i] = {}
  Competitors[i].nr = LeagueCompetitors[i].n
  Competitors[i].speed = 0.010-(CompPos)*0.00045
  Competitors[i].distance = 0
  Competitors[i].xpos = (i-1)*26-65
end

function ShuffleKeys(array, key)
    for i = #array, 2, -1 do
        local j = math.random(i)
        array[i][key], array[j][key] = array[j][key], array[i][key]
    end
end

function SwitchRandomPair(array)
  local pos = math.random(#array-1)
  array[pos], array[pos+1] = array[pos+1], array[pos]
end

ShuffleKeys(Competitors, "xpos")
for i = 1, 10 do
	SwitchRandomPair(Competitors)
end

function DrawSpriteToRoad(handle, roadX, roadY, scaleX, scaleY)
  if (scaleY == nil) then
    scaleY = scaleX
  end
  local x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
  if (y~=-1000 and y>=DRAW_HORIZON) then
    scaleX = scaleX*scaleFactor
    scaleY = scaleY*scaleFactor
    DrawSpriteScaled(handle, x-SpriteWidth(handle)*scaleX*0.5, y-SpriteHeight(handle)*scaleY, scaleX, scaleY)
  end
end

function DrawAnimSpriteToRoad(handle, roadX, roadY, scaleX, scaleY, frame)
  local x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
  if (y~=-1000 and y>=DRAW_HORIZON) then
    scaleX = scaleX*scaleFactor
    scaleY = scaleY*scaleFactor
    DrawAnimSpriteScaled(handle, x-SpriteWidth(handle)*scaleX*0.5, y-SpriteHeight(handle)*scaleY, scaleX, scaleY, frame)
  end
end

function DrawHorse(handle, roadX, roadY, height, scaleX, scaleY, angle, frame, name)
  local x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
  if (y~=-1000 and y>=DRAW_HORIZON) then
    scaleX = scaleX*scaleFactor
    scaleY = scaleY*scaleFactor
    if (name ~= nil and name ~= "" and roadY<30) then
      SetTextDatum(4)
      DrawString(name, x, y - SpriteHeight(handle)*scaleY - 10)
      SetTextDatum(0)
    end
    DrawAnimSpriteScaledRotated(handle, x, y + height, scaleX, scaleY, angle, frame, 1+8)
  end
end

-- Winscreen

WinScreenPage = 0


-- Progression Menu

ShopHorses = nil
SButtonUp = LoadSprite("gfx/buttonUp.bmp", 0, 0xf81f)
SButtonDown = LoadSprite("gfx/buttonDown.bmp", 0, 0xf81f)

function GenerateHorseName()
  local l1 = {'Sierra', 'Smooth', 'Free', 'Snow', 'Stern', 'Cool', 'Neat', 'Chasing', 'Pretty', 'Grand', 'Mystic' }
  local l2 = {'Leone', 'Serrano', 'Guard', 'Ride', 'Down', 'Time', 'Spring', 'Starr', 'Peace', 'Show', 'Candy', 'Pride', 'Sea', 'Run', 'Front', 'Looks'}
  return l1[math.random(#l1)] .. " " .. l2[math.random(#l2)]
end

function GetShopHorses()
  if ShopHorses == nil then
    ShopHorses = {}
    for i = 1, 3 do
      local speed = math.random(1,10)
	  ShopHorses[i] = {
	    name = GenerateHorseName(),
	    speed = speed,
	    color = math.random(1,NUM_HORSE_COLORS),
	    cost = 1000*speed + math.random(0,2000)
	  }
    end
  end
  return ShopHorses
end

function SavePlayerHorse()
  PrefsSetString("hname", PlayerHorse.name)
  PrefsSetInt("hspeed", PlayerHorse.speed)
  PrefsSetInt("hcolor", PlayerHorse.color)
end

ShopMenuOffset = 0
TouchBlocked = false
function DisplayHorseShop(offset)
  if TouchBlocked and not IsTouchInZone(0, 0, 320, 240) then
    TouchBlocked = false
  end
  local shopHorses = GetShopHorses()
  if #shopHorses >= 2 and ShopMenuOffset>#shopHorses-2 then
    ShopMenuOffset = #shopHorses-3
  end
  if ShopMenuOffset>0 then
    DrawSprite(SButtonUp, 275, 35)
    if IsTouchInZone(275, 35, 32, 32) and not TouchBlocked then
      ShopMenuOffset = ShopMenuOffset - 1
      TouchBlocked = true
    end
  end
  if ShopMenuOffset<#shopHorses-2 then
    DrawSprite(SButtonDown, 275, 150)
    if IsTouchInZone(275, 150, 32, 32) and not TouchBlocked then
      ShopMenuOffset = ShopMenuOffset + 1
      TouchBlocked = true
    end
  end
  local yPos = 35
  FillRect(10, yPos, 220, 60, 0xfea0)
  DrawSprite(SHorseSide[PlayerHorse.color], 12, yPos)
  SetTextSize(2)
  DrawString(PlayerHorse.name, 85, yPos+2)
  DrawString("Tempo: " .. PlayerHorse.speed, 85, yPos+20)
  SetTextDatum(2)
  DrawString("Im Besitz", 225, yPos+40)
  SetTextDatum(0)
  for i = 1, #shopHorses - ShopMenuOffset do
    local shopHorse = shopHorses[i + ShopMenuOffset]
    yPos = 35 + (i)*64
    if (Money < shopHorse.cost) then
      FillRect(10, yPos, 220, 60, 0xF800)
    else
      FillRect(10, yPos, 220, 60, 0x001F)
    end
    DrawSprite(SHorseSide[shopHorse.color], 12, yPos)
    DrawString(shopHorse.name, 85, yPos+2)
    DrawString("Tempo: " .. shopHorse.speed, 85, yPos+20)
    SetTextDatum(2)
    DrawString("$"..shopHorse.cost, 225, yPos+40)
    SetTextDatum(0)
  end
  for i = 1, #shopHorses - ShopMenuOffset do
    local shopHorse = shopHorses[i + ShopMenuOffset]
    yPos = 35 + (i)*64
    if Money > shopHorse.cost and IsTouchInZone(10, yPos, 220, 60) and not TouchBlocked then
      TouchBlocked = true
      Money = Money - shopHorse.cost
      PrefsSetInt("money", Money)
      PlayerHorse = shopHorse
      SavePlayerHorse()
      for j = i + ShopMenuOffset, #shopHorses do
        ShopHorses[j] = ShopHorses[j+1]
      end
      break
    end
  end
  SetTextSize(1)
end
