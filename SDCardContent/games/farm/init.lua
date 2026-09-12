SBackground = LoadSprite("gfx/background.bmp")
SEnergyBar = LoadAnimSprite("gfx/energybar.bmp", 26, 33, 0, 0xf81f)
SEnergy = LoadSprite("gfx/energy.bmp", 0, 0xf81f)
SBerry = LoadSprite("gfx/blackberry.bmp", 0, 0xf81f)
SFish = LoadAnimSprite("gfx/fish.bmp", 162, 42, 0, 0xf81f)

SIconPlow = LoadSprite("gfx/plow.bmp", 0, 0xf81f)
SIconWateringCan = LoadSprite("gfx/wateringcan.bmp", 0, 0xf81f)
SIconSeedbag = LoadSprite("gfx/seedbag.bmp", 0, 0xf81f)
SIconBackArrow = LoadSprite("gfx/backarrow.bmp", 0, 0xf81f)

SBerrySelection = LoadSprite("gfx/berrySelection.bmp", 0, 0xf81f)
SFishSelection = LoadSprite("gfx/fishSelection.bmp", 0, 0xf81f)
SFieldSelection = LoadSprite("gfx/fieldSelection.bmp", 0, 0xf81f)

SField_fallow = LoadSprite("gfx/field/field_fallow.bmp", 0, 0xf81f)
SField_tilled = LoadSprite("gfx/field/field_tilled.bmp", 0, 0xf81f)
SField_watered = LoadSprite("gfx/field/field_watered.bmp", 0, 0xf81f)
SField_plantWheat = LoadAnimSprite("gfx/field/wheat.bmp", 18, 13, 0, 0xf81f)
SField_dirt = LoadAnimSprite("gfx/field/dirt.bmp", 15, 6, 0, 0xf81f)

Money = PrefsGetInt("money", 0)
Energy = 0
Touched = false

FIELD_GRID_PLANT_NONE = 0
FIELD_GRID_PLANT_WHEAT = 1

FIELD_GRID_COLS = 11
FIELD_GRID_ROWS = 5

FieldGrid = {}
for col=1,FIELD_GRID_COLS do
  FieldGrid[col] = {}
  for row=1,FIELD_GRID_ROWS do
    FieldGrid[col][row] = {
      plowed = false,
      watered = false,
      plant = FIELD_GRID_PLANT_NONE,
      growStage = 0
    }
  end
end

FIELD_GRID_SPACING = 23
FIELD_GRID_X_OFFSET = 4
FIELD_GRID_Y_OFFSET = 5

FIELD_GRID_ACTION_PLOW = 1
FIELD_GRID_ACTION_WATER = 2
FIELD_GRID_ACTION_SEED_WHEAT = 3

FIELD_ACTION_RADIUS = 10
FIELD_ACTION_ENERGY = 0.05

FIELD_ACTION_SEEDBAG_WHEAT_PRICE = 10

LastFieldGridUpdateX = -1
LastFieldGridUpdateY = -1

function SaveFieldGrid()
  local serialized = ""
  for row=1,FIELD_GRID_ROWS do
    for col=1,FIELD_GRID_COLS do
      local intval = 0
      local fg = FieldGrid[col][row]
      if fg.watered then
        intval = 0x03
      elseif fg.plowed then
        intval = 0x02
      else
        intval = 0x01
      end
      intval = intval | (fg.plant << 2) | (fg.growStage << 6)
      serialized = serialized .. string.char(intval)
    end
  end
  SerialPrintln("|"..serialized.."|")
  SerialPrintln("LEN: "..string.len(serialized))
  PrefsSetString("fieldGrid", serialized)
end

function LoadFieldGrid()
  local serialized = PrefsGetString("fieldGrid", "\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1\1")
  SerialPrintln("|"..serialized.."|")
  SerialPrintln("LEN: "..string.len(serialized))
  for row=1,FIELD_GRID_ROWS do
    for col=1,FIELD_GRID_COLS do
      i = FIELD_GRID_COLS*(row-1) + (col)
      SerialPrint(i)
      local intval = string.byte(serialized, i, i+1) or 0
      SerialPrint(" -> "..intval)
      local wasWatered = (intval & 0x03) == 0x03 -- bit 0 and 1 => 0b11
      SerialPrint(", "..(wasWatered and 1 or 0))
      FieldGrid[col][row] = {
        plowed = (intval & 0x02) == 0x02, -- bit 1 => 0b1x
        watered = false,
        plant = (intval & 0x3C) >> 2, -- bit 2, 3, 4, 5
        growStage = (intval & 0xC0) >> 6 -- bit 6 and 7
      }
      if wasWatered and FieldGrid[col][row].plant~=FIELD_GRID_PLANT_NONE and FieldGrid[col][row].growStage < 3 then
        SerialPrint(" GROW")
        FieldGrid[col][row].growStage = FieldGrid[col][row].growStage + 1
      end
      if FieldGrid[col][row].plowed and FieldGrid[col][row].plant==FIELD_GRID_PLANT_NONE and math.random(0,1)==1 then
        SerialPrint(" UNPLOW")
        FieldGrid[col][row].plowed = false
      end
      SerialPrintln("")
      if FieldGrid[col][row].plowed then
        SetDrawTargetSprite(SField_fallow)
        local xPos = FIELD_GRID_X_OFFSET + col*FIELD_GRID_SPACING - 8 - 0.5*FIELD_GRID_SPACING
        local yPos = FIELD_GRID_X_OFFSET + row*FIELD_GRID_SPACING - 16 - 0.5*FIELD_GRID_SPACING
        FillRect(xPos, yPos, FIELD_GRID_SPACING, FIELD_GRID_SPACING, 0xf81f)
      end
      SetDrawTargetFramebuffer()
    end
  end
end
LoadFieldGrid()

function UpdateFieldGrid(x, y, type)
  if LastFieldGridUpdateX == -1 and LastFieldGridUpdateY == -1 then
    DoUpdateFieldGrid(x, y, type)
  else
    local dX = x-LastFieldGridUpdateX
    local dY = y-LastFieldGridUpdateY
    if dX>=dY then
      local yFraction = dY/dX
      local yPos = LastFieldGridUpdateY
      for xPos=LastFieldGridUpdateX,x do
        DoUpdateFieldGrid(xPos, yPos, type)
        yPos = yPos + yFraction
      end
    else
      local xFraction = dX/dY
      local xPos = LastFieldGridUpdateX
      for yPos=LastFieldGridUpdateY,y do
        DoUpdateFieldGrid(xPos, yPos, type)
        xPos = xPos + xFraction
      end
    end
  end
  LastFieldGridUpdateX = x
  LastFieldGridUpdateY = y
end

function DoUpdateFieldGrid(x, y, type)
  local radiusSq = FIELD_ACTION_RADIUS*FIELD_ACTION_RADIUS
  for col=1,#FieldGrid do
    for row=1,#(FieldGrid[1]) do
      if Energy < FIELD_ACTION_ENERGY then
        return
      end
      local fg = FieldGrid[col][row]
      if type == FIELD_GRID_ACTION_PLOW then
        SetDrawTargetSprite(SField_fallow)
        FillCircle(x-8, y-16, FIELD_ACTION_RADIUS, 0xf81f)
      elseif type == FIELD_GRID_ACTION_WATER then
        SetDrawTargetSprite(SField_tilled)
        FillCircle(x-8, y-16, FIELD_ACTION_RADIUS, 0xf81f)
      end
      SetDrawTargetFramebuffer()
      local posX = col * FIELD_GRID_SPACING + FIELD_GRID_X_OFFSET
      local posY = row * FIELD_GRID_SPACING + FIELD_GRID_Y_OFFSET
      local dX = posX-x
      local dY = posY-y
      local d = dX*dX + dY*dY
      if d<=radiusSq then
        if fg.plant ~= FIELD_GRID_PLANT_NONE and fg.growStage == 3 then
          fg.plant = FIELD_GRID_PLANT_NONE
          fg.growStage = 0
          ShowEarningSpriteUntil = Ms + 2000
          ShowEarningSprite = SField_plantWheat
          ShowEarningSpriteX = x - SpriteWidth(SField_plantWheat) * 0.5
          ShowEarningSpriteY = y
          ShowEarningSpriteScaleX = 1
          ShowEarningSpriteScaleY = 1
          ShowEarningSpriteFrame = 3
          ShowEarningsText = "+€40"
          Money = Money + 40
          Energy = Energy - FIELD_ACTION_ENERGY
        elseif type == FIELD_GRID_ACTION_PLOW and fg.plowed == false then
          fg.plowed = true
          Energy = Energy - FIELD_ACTION_ENERGY
        elseif type == FIELD_GRID_ACTION_WATER and fg.watered == false then
          fg.watered = true
          Energy = Energy - FIELD_ACTION_ENERGY
        elseif type == FIELD_GRID_ACTION_SEED_WHEAT and fg.plowed and fg.watered and fg.plant == FIELD_GRID_PLANT_NONE and Money >= FIELD_ACTION_SEEDBAG_WHEAT_PRICE then
          fg.plant = FIELD_GRID_PLANT_WHEAT
          fg.growStage = 0
          Money = Money - FIELD_ACTION_SEEDBAG_WHEAT_PRICE
          Energy = Energy - FIELD_ACTION_ENERGY
        end
      end
    end
  end
end

GAME_MODE_OVERVIEW = 0
GAME_MODE_FIELD = 1
GameMode = GAME_MODE_OVERVIEW

ShowEarningSpriteUntil = 0
ShowEarningsText = ""
ShowEarningSprite = 0
ShowEarningSpriteFrame = 0
ShowEarningSpriteX = 0
ShowEarningSpriteY = 0
ShowEarningSpriteScaleX = 1
ShowEarningSpriteScaleY = 1

JoystickSelection = 0
JOYSTICK_SELECTION_OVERVIEW_LIMIT = 3
JOYSTICK_SELECTION_FIELD_LIMIT = 0


fieldItemSelection = 0
FIELD_ITEM_SELECTION_PLOW = 1
FIELD_ITEM_SELECTION_WATERING_CAN = 2
FIELD_ITEM_SELECTION_SEEDBAG = 3