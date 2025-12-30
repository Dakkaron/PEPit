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
COAT_B = 1 << 1
COAT_Y = 1 << 2
HELMET_R = 1 << 8
HELMET_G = 1 << 9
HELMET_B = 1 << 10
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

function CreateRandomHorseSprite()
  return CreateHorseSprite(math.random(1, NUM_HORSE_COLORS), 1 << math.random(0,2), 1 << math.random(8,10))
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
SHorse = {CreateRandomHorseSprite(), CreateRandomHorseSprite(), CreateRandomHorseSprite(), CreateRandomHorseSprite(), CreateRandomHorseSprite()}
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
  sprite = CreateHorseSprite(PrefsGetInt("hcolor", 2), PrefsGetInt("hcoat", COAT_R), PrefsGetInt("hhelmet", HELMET_B)),
  helmet = PrefsGetInt("hhelmet", HELMET_B),
  coat = PrefsGetInt("hcoat", COAT_R)
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

ShopItems = nil
OwnedItems = PrefsGetInt("ownedItems", COAT_R + HELMET_B)

ITEM_TYPE_COAT = 1
ITEM_TYPE_HELMET = 2


ShopTab = 0
ConfirmDialogOpen = false
ConfirmDialogText = ""
ConfirmDialogItemNr = 0


function GenerateHorseName()
  local l1 = {'Sierra', 'Smooth', 'Free', 'Snow', 'Stern', 'Cool', 'Neat', 'Chasing', 'Pretty', 'Grand', 'Mystic' }
  local l2 = {'Leone', 'Serrano', 'Guard', 'Ride', 'Down', 'Time', 'Spring', 'Starr', 'Peace', 'Show', 'Candy', 'Pride', 'Sea', 'Run', 'Front', 'Looks'}
  return l1[math.random(#l1)] .. " " .. l2[math.random(#l2)]
end

function PopulateShopHorses()
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
  PopulateShopHorses()
  if #ShopHorses >= 2 and ShopMenuOffset>#ShopHorses-2 then
    ShopMenuOffset = #ShopHorses-3
  end
  if ShopMenuOffset>0 then
    DrawSprite(SButtonUp, 275, 100)
    if IsTouchInZone(275, 100, 32, 32) and not TouchBlocked then
      ShopMenuOffset = ShopMenuOffset - 1
      TouchBlocked = true
    end
  end
  if ShopMenuOffset<#ShopHorses-2 then
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
  for i = 1, #ShopHorses - ShopMenuOffset do
    local shopHorse = ShopHorses[i + ShopMenuOffset]
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
  for i = 1, #ShopHorses - ShopMenuOffset do
    local shopHorse = ShopHorses[i + ShopMenuOffset]
    yPos = 35 + (i)*64
    if Money > shopHorse.cost and IsTouchInZone(10, yPos, 220, 60) and not TouchBlocked then
      TouchBlocked = true
      ConfirmDialogOpen = true
      ConfirmDialogText = "Willst du " .. shopHorse.name .. "\n\nfür $" .. shopHorse.cost .." kaufen?\n \nDein Kontostand beträgt\n$" .. Money  
      ConfirmDialogItemNr = i + ShopMenuOffset
      break
    end
  end
end


function PopulateShopItems()
  if ShopItems == nil then
    ShopItems = {
      {
        sprite = LoadSprite("gfx/items/shop_coat_red.bmp"),
        cost = 1200,
        name = "Rote Jacke",
        color = COAT_R,
        itemType = ITEM_TYPE_COAT
      },
      {
        sprite = LoadSprite("gfx/items/shop_coat_blue.bmp"),
        cost = 1200,
        name = "Blaue Jacke",
        color = COAT_B,
        itemType = ITEM_TYPE_COAT
      },
      {
        sprite = LoadSprite("gfx/items/shop_coat_yellow.bmp"),
        cost = 1200,
        name = "Gelbe Jacke",
        color = COAT_Y,
        itemType = ITEM_TYPE_COAT
      },
      {
        sprite = LoadSprite("gfx/items/shop_helmet_blue.bmp"),
        cost = 1200,
        name = "Blauer Helm",
        color = HELMET_B,
        itemType = ITEM_TYPE_HELMET
      },
      {
        sprite = LoadSprite("gfx/items/shop_helmet_red.bmp"),
        cost = 1200,
        name = "Roter Helm",
        color = HELMET_R,
        itemType = ITEM_TYPE_HELMET
      },
      {
        sprite = LoadSprite("gfx/items/shop_helmet_green.bmp"),
        cost = 1200,
        name = "Grüner Helm",
        color = HELMET_G,
        itemType = ITEM_TYPE_HELMET
      },
    }
  end
end

function DisplayItemShop()
  if TouchBlocked and not IsTouchInZone(0, 0, 320, 240) then
    TouchBlocked = false
  end
  PopulateShopItems()
  if #ShopItems >= 2 and ShopMenuOffset>#ShopItems-2 then
    ShopMenuOffset = #ShopItems-3
  end
  if ShopMenuOffset>0 then
    DrawSprite(SButtonUp, 275, 100)
    if IsTouchInZone(275, 100, 32, 32) and not TouchBlocked then
      ShopMenuOffset = ShopMenuOffset - 1
      TouchBlocked = true
    end
  end
  if ShopMenuOffset<#ShopItems-2 then
    DrawSprite(SButtonDown, 275, 150)
    if IsTouchInZone(275, 150, 32, 32) and not TouchBlocked then
      ShopMenuOffset = ShopMenuOffset + 1
      TouchBlocked = true
    end
  end
  for i = 1, #ShopItems - ShopMenuOffset do
    local shopItem = ShopItems[i + ShopMenuOffset]
    local yPos = 35 + (i-1)*64
    local isOwned = (OwnedItems & shopItem.color) ~= 0
    local isEquipped = PlayerHorse.coat == shopItem.color or PlayerHorse.helmet == shopItem.color
    if isEquipped then
      FillRect(10, yPos, 220, 60, 0xffe0)
    elseif isOwned then
      FillRect(10, yPos, 220, 60, 0x7bef)
    elseif Money < shopItem.cost then
      FillRect(10, yPos, 220, 60, 0xF800)
    else
      FillRect(10, yPos, 220, 60, 0x001F)
    end
    DrawSprite(shopItem.sprite, 12, yPos)
    DrawString(shopItem.name, 85, yPos+2)
    SetTextDatum(2)
    if isEquipped then
      DrawString("Aktiv", 225, yPos+40)
    elseif isOwned then
      DrawString("Im Besitz", 225, yPos+40)
    else
      DrawString("$"..shopItem.cost, 225, yPos+40)
    end
    SetTextDatum(0)
  end
  for i = 1, #ShopItems - ShopMenuOffset do
    local shopItem = ShopItems[i + ShopMenuOffset]
    yPos = 35 + (i-1)*64
    local isOwned = (OwnedItems & shopItem.color) ~= 0
    local isEquipped = PlayerHorse.coat == shopItem.color or PlayerHorse.helmet == shopItem.color
    if IsTouchInZone(10, yPos, 220, 60) and not TouchBlocked then
      if isOwned and not isEquipped then
        if shopItem.itemType == ITEM_TYPE_COAT then
          PlayerHorse.coat = shopItem.color
          PrefsSetInt("hcoat", shopItem.color)
        elseif shopItem.itemType == ITEM_TYPE_HELMET then
          PlayerHorse.helmet = shopItem.color
          PrefsSetInt("hhelmet", shopItem.color)
        end
        TouchBlocked = true
        break
      elseif not isOwned and Money > shopItem.cost then
        TouchBlocked = true
        ConfirmDialogOpen = true
        ConfirmDialogText = "Willst du " .. shopItem.name .. "\n\nfür $" .. shopItem.cost .." kaufen?\n \nDein Kontostand beträgt\n$" .. Money  
        ConfirmDialogItemNr = i + ShopMenuOffset
        break
      end
    end
  end
end

function DisplayConfirmDialog()
  if TouchBlocked and not IsTouchInZone(0, 0, 320, 240) then
    TouchBlocked = false
  end
  FillRect(30, 30, 260, 160, 0x94b2)
  DrawString(ConfirmDialogItemNr, 40, 200)
  SetTextSize(2)
  SetTextDatum(0)
  DrawString(ConfirmDialogText, 40, 40)
  FillRect(40, 145, 100, 40, 0x001F)
  DrawString("Ja", 80, 157)
  FillRect(180, 145, 100, 40, 0x001F)
  DrawString("Nein", 215, 157)
  if not TouchBlocked and IsTouchInZone(40, 145, 100, 40) then
    if ShopTab == 0 then -- Horse shop
      shopHorse = ShopHorses[ConfirmDialogItemNr]
      Money = Money - shopHorse.cost
      PrefsSetInt("money", Money)
      PlayerHorse = shopHorse
      SavePlayerHorse()
      for i = ConfirmDialogItemNr, #ShopHorses do
        ShopHorses[i] = ShopHorses[i+1]
      end
    elseif ShopTab == 1 then -- Item shop
      shopItem = ShopItems[ConfirmDialogItemNr]
      Money = Money - shopItem.cost
      PrefsSetInt("money", Money)
      if shopItem.itemType == ITEM_TYPE_COAT then
        PlayerHorse.coat = shopItem.color
        PrefsSetInt("hcoat", shopItem.color)
      elseif shopItem.itemType == ITEM_TYPE_HELMET then
        PlayerHorse.helmet = shopItem.color
        PrefsSetInt("hhelmet", shopItem.color)
      end
      OwnedItems = OwnedItems | shopItem.color
      PrefsSetInt("ownedItems", OwnedItems)
      for i = ConfirmDialogItemNr, #ShopHorses do
        ShopHorses[i] = ShopHorses[i+1]
      end
    end
    ConfirmDialogOpen = false
    TouchBlocked = true
  elseif not TouchBlocked and IsTouchInZone(180, 145, 100, 40) then
    ConfirmDialogOpen = false
    TouchBlocked = true
  end
end