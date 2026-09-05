SBackground = LoadSprite("gfx/background.bmp")
SEnergyBar = LoadAnimSprite("gfx/energybar.bmp", 26, 33, 0, 0xf81f)
SEnergy = LoadSprite("gfx/energy.bmp", 0, 0xf81f)
SBerry = LoadSprite("gfx/blackberry.bmp", 0, 0xf81f)
SFish = LoadAnimSprite("gfx/fish.bmp", 162, 42, 0, 0xf81f)

SBerrySelection = LoadSprite("gfx/berrySelection.bmp", 0, 0xf81f)
SFishSelection = LoadSprite("gfx/fishSelection.bmp", 0, 0xf81f)
SFieldSelection = LoadSprite("gfx/fieldSelection.bmp", 0, 0xf81f)

SField_fallow = LoadSprite("gfx/field/field_fallow.bmp", 0, 0xf81f)
SField_tilled = LoadSprite("gfx/field/field_tilled.bmp", 0, 0xf81f)
SField_watered = LoadSprite("gfx/field/field_watered.bmp", 0, 0xf81f)
SField_Wheat = LoadAnimSprite("gfx/field/wheat.bmp", 26, 33, 0, 0xf81f)

Money = PrefsGetInt("money", 0)
Energy = 0
Touched = false

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