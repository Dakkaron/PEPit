if NewRepetition then
    Energy = Energy + 0.1
    if Energy > 1.0 then
        Energy = 1.0
    end
end

if GameMode == GAME_MODE_OVERVIEW then
    DrawSprite(SBackground, 0, 0)

    JoystickSelection = JoystickSelection % JOYSTICK_SELECTION_OVERVIEW_LIMIT

    if JoystickSelection == 2 then
        DrawSprite(SFieldSelection, 0, 78, {alpha=math.sin(Ms/100)*0.5+0.5})
    else
        DrawSprite(SFieldSelection, 0, 78, {alpha=0.7})
    end

    if JoystickSelection == 0 then
        DrawSprite(SBerrySelection, 0, 87, {alpha=math.sin(Ms/100)*0.5+0.5})
    elseif Energy>=0.2 then
        DrawSprite(SBerrySelection, 0, 87, {alpha=0.7})
    end

    if JoystickSelection == 1 then
        DrawSprite(SFishSelection, 174, 127, {alpha=math.sin(Ms/100)*0.5+0.5})
    elseif Energy>=0.2 then
        DrawSprite(SFishSelection, 174, 127, {alpha=0.7})
    end

    local joystickX = GetJoystickX()
    if not Touched then
        if (IsTouchInZone(0, 96, 111, 38) or (JoystickSelection==0 and GetJoystickButton())) and Energy >= 0.2 then --Berrypicking
            Energy = Energy - 0.2
            ShowEarningSpriteUntil = Ms + 2000
            ShowEarningSprite = SBerry
            ShowEarningSpriteX = 50
            ShowEarningSpriteY = 80
            ShowEarningSpriteScaleX = 0.5
            ShowEarningSpriteScaleY = 0.5
            ShowEarningSpriteFrame = -1
            ShowEarningsText = "+€10"
            Money = Money + 10
        elseif (IsTouchInZone(175, 145, 146, 35) or (JoystickSelection==1 and GetJoystickButton())) and Energy >= 0.2 then --Fishing
            Energy = Energy - 0.2
            ShowEarningSpriteUntil = Ms + 2000
            ShowEarningSpriteX = 175
            ShowEarningSpriteY = 145
            ShowEarningSpriteScaleX = 1
            ShowEarningSpriteScaleY = 1
            ShowEarningSprite = SFish
            local fishResult = math.random()
            if fishResult < 0.2 then
                ShowEarningSpriteFrame = 0
                ShowEarningsText = "+€0"
            elseif fishResult < 0.5 then
                ShowEarningSpriteFrame = 1
                ShowEarningsText = "+€5"
                Money = Money + 5
            elseif fishResult < 0.75 then
                ShowEarningSpriteFrame = 2
                ShowEarningsText = "+€12"
                Money = Money + 12
            elseif fishResult < 0.95 then
                ShowEarningSpriteFrame = 3
                ShowEarningsText = "+€20"
                Money = Money + 20
            else
                ShowEarningSpriteFrame = 4
                ShowEarningsText = "+€30"
                Money = Money + 30
            end
        elseif IsTouchInZone(0, 79, 170, 17) or IsTouchInZone(110, 79, 60, 34) or (JoystickSelection==2 and GetJoystickButton()) then --Field
            GameMode = GAME_MODE_FIELD
            fieldItemSelection = 0
        end

        if joystickX < -0.5 then
            JoystickSelection = JoystickSelection - 1
        elseif joystickX > 0.5 then
            JoystickSelection = JoystickSelection + 1
        end
    end

    Touched = IsTouchInZone(0,0,320,240) or GetJoystickButton() or math.abs(joystickX) > 0.5
elseif GameMode == GAME_MODE_FIELD then
    DrawSprite(SField_watered, 0, 0)
    DrawSprite(SField_tilled, 11, 20)
    DrawSprite(SField_fallow, 8, 16)
    
    local fieldItemAlpha = 1
    if Energy < FIELD_ACTION_ENERGY then
        fieldItemAlpha = 0.5
    end
    local fieldItemSeedBagAlpha = 1
    if Energy < FIELD_ACTION_ENERGY or Money < FIELD_ACTION_SEEDBAG_WHEAT_PRICE then
        fieldItemSeedBagAlpha = 0.5
    end
    if fieldItemSelection == FIELD_ITEM_SELECTION_PLOW then
        DrawSprite(SIconPlow, 290, 20, {angle=math.sin(Ms*0.005)*.3, flags=0, alpha=fieldItemAlpha})
    else
        DrawSprite(SIconPlow, 290, 20, {alpha=fieldItemAlpha})
    end
    if fieldItemSelection == FIELD_ITEM_SELECTION_WATERING_CAN then
        DrawSprite(SIconWateringCan, 320, 86, {angle=math.sin(Ms*0.005)*.3, flags=0xA, alpha=fieldItemAlpha})
    else
        DrawSprite(SIconWateringCan, 283, 60, {alpha=fieldItemAlpha})
    end
    if fieldItemSelection == FIELD_ITEM_SELECTION_SEEDBAG then
        DrawSprite(SIconSeedbag, 304, 115, {angle=math.sin(Ms*0.005)*.3, flags=0x5, alpha=fieldItemSeedBagAlpha})
    else
        DrawSprite(SIconSeedbag, 287, 100, {alpha=fieldItemSeedBagAlpha})
    end
    DrawString("-€10", 295, 125)
    DrawSprite(SIconBackArrow, 278, 190, {scaleX=2, scaleY=2})
    DrawString("Zurück", 288, 217)

    if not IsTouchInZone(0,0,320,240) then
        Touched = false
        LastFieldGridUpdateX = -1
        LastFieldGridUpdateY = -1
    end
    if IsTouchInZone(287, 20, 35, 29) and Touched == false then
        Touched = true
        fieldItemSelection = FIELD_ITEM_SELECTION_PLOW
    elseif IsTouchInZone(287, 60, 35, 26) and Touched == false then
        Touched = true
        fieldItemSelection = FIELD_ITEM_SELECTION_WATERING_CAN
    elseif IsTouchInZone(287, 100, 35, 26) and Touched == false then
        Touched = true
        fieldItemSelection = FIELD_ITEM_SELECTION_SEEDBAG
    elseif IsTouchInZone(278, 190, 40, 225) and Touched == false then
        Touched = true
        GameMode = GAME_MODE_OVERVIEW
    elseif IsTouchInZone(22, 10, 274, 130) and Energy >= FIELD_ACTION_ENERGY then
        Touched = true
        local touchX = GetTouchX()
        local touchY = GetTouchY()
        if fieldItemSelection == FIELD_ITEM_SELECTION_PLOW then
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_PLOW)
        elseif fieldItemSelection == FIELD_ITEM_SELECTION_WATERING_CAN then
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_WATER)
        elseif fieldItemSelection == FIELD_ITEM_SELECTION_SEEDBAG then
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_SEED_WHEAT)
        end
    end
    for col=1,#FieldGrid do
        for row=1,#(FieldGrid[1]) do
            fg = FieldGrid[col][row]
            if fg.plowed == false then
                
            elseif fg.watered == false then
                DrawSprite(SField_dirt, 4+col*23, 5+row*23, {frame=0, flags=0x5})
            elseif fg.plant == FIELD_GRID_PLANT_NONE then
                DrawSprite(SField_dirt, 4+col*23, 5+row*23, {frame=1, flags=0x5})
            end
            if fg.plant == FIELD_GRID_PLANT_WHEAT then
                DrawSprite(SField_plantWheat, 4+col*23, 5+row*23, {frame=fg.growStage, flags=0x5})
            end
        end
    end
end

if Energy < 0.95 then
    DrawSprite(SEnergyBar, 0, 145, {frame=1})
    DrawSprite(SEnergyBar, 0+26, 145, {scaleX=5, scaleY=1, frame=2})
    DrawSprite(SEnergyBar, 0+26*6, 145, {frame=4})
    DrawSprite(SEnergyBar, 0+26, 145, {scaleX=Energy*6, scaleY=1, frame=3})
    DrawSprite(SEnergyBar, 0+26 + Energy*26*6, 145, {scaleX=1, scaleY=1, frame=6})
else
    DrawSprite(SEnergyBar, 0, 145, {frame=1})
    DrawSprite(SEnergyBar, 0+26, 145, {scaleX=5, scaleY=1, frame=3})
    DrawSprite(SEnergyBar, 0+26*6, 145, {frame=5})
end

if Ms < ShowEarningSpriteUntil then
    local showSpriteYShift = (2000-(ShowEarningSpriteUntil-Ms))/10
    if ShowEarningSpriteFrame < 0 then
        DrawSprite(ShowEarningSprite, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift, {scaleX=ShowEarningSpriteScaleX, scaleY=ShowEarningSpriteScaleY})
    else
        DrawSprite(ShowEarningSprite, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift, {scaleX=ShowEarningSpriteScaleX, scaleY=ShowEarningSpriteScaleY, frame=ShowEarningSpriteFrame})
    end
    SetTextSize(2)
    DrawString(ShowEarningsText, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift - 10)
end

SetTextSize(2)
SetTextColor(0xFFFF)
DrawString("€" .. Money, 190, 220)
