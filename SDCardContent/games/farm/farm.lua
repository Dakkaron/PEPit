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

    local joystickX, joystickY = GetJoystickXY()
    local overviewJoyBtn = GetJoystickButton()
    if not Touched then
        if (IsTouchInZone(0, 96, 111, 38) or (JoystickSelection==0 and overviewJoyBtn and not FieldJoyWas)) and Energy >= 0.2 then --Berrypicking
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
        elseif (IsTouchInZone(175, 145, 146, 35) or (JoystickSelection==1 and overviewJoyBtn and not FieldJoyWas)) and Energy >= 0.2 then --Fishing
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
        elseif IsTouchInZone(0, 79, 170, 17) or IsTouchInZone(110, 79, 60, 34) or (JoystickSelection==2 and overviewJoyBtn and not FieldJoyWas) then --Field
            GameMode = GAME_MODE_FIELD
            fieldItemSelection = 0
        end

        if not FieldNavMoved then
            local dir = 0
            if math.abs(joystickX) > 0.2 then
                dir = joystickX > 0 and 1 or -1
            elseif math.abs(joystickY) > 0.2 then
                dir = joystickY > 0 and 1 or -1
            end
            if dir ~= 0 then
                JoystickSelection = (JoystickSelection + dir) % JOYSTICK_SELECTION_OVERVIEW_LIMIT
                FieldNavMoved = true
            end
        elseif math.abs(joystickX) <= 0.2 and math.abs(joystickY) <= 0.2 then
            FieldNavMoved = false
        end
    end

    FieldJoyWas = overviewJoyBtn

    Touched = IsTouchInZone(0,0,320,240)
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
        DrawSprite(SIconWateringCan, 320, 86, {angle=math.sin(Ms*0.005)*.3, flags=ALIGN_H_RIGHT|ALIGN_V_BOTTOM, alpha=fieldItemAlpha})
    else
        DrawSprite(SIconWateringCan, 283, 60, {alpha=fieldItemAlpha})
    end
    if fieldItemSelection == FIELD_ITEM_SELECTION_SEEDBAG then
        DrawSprite(SIconSeedbag, 304, 115, {angle=math.sin(Ms*0.005)*.3, flags=ALIGN_H_CENTER|ALIGN_V_CENTER, alpha=fieldItemSeedBagAlpha})
    else
        DrawSprite(SIconSeedbag, 287, 100, {alpha=fieldItemSeedBagAlpha})
    end
    DrawString("-€10", 295, 125)
    DrawSprite(SIconBackArrow, 278, 190, {scaleX=2, scaleY=2})
    DrawString("Zurück", 288, 217)

    if FieldSel < 0 or FieldSel > 58 then
        FieldSel = 55
    end
    while not FieldSlotActionable(FieldSel) do
        FieldSel = (FieldSel + 1) % 59
    end

    local fieldPressed = IsTouchInZone(0, 0, 320, 240)
    if fieldPressed and not FieldTapWas then
        local tapSlot = -1
        if IsTouchInZone(287, 20, 35, 29) then
            tapSlot = 55
        elseif IsTouchInZone(287, 60, 35, 26) then
            tapSlot = 56
        elseif IsTouchInZone(287, 100, 35, 26) then
            tapSlot = 57
        elseif IsTouchInZone(278, 190, 40, 225) then
            tapSlot = 58
        elseif fieldItemSelection >= 1 then
            local tapCol = math.floor((GetTouchX() - FIELD_GRID_X_OFFSET) / FIELD_GRID_SPACING + 0.5)
            local tapRow = math.floor((GetTouchY() - FIELD_GRID_Y_OFFSET) / FIELD_GRID_SPACING + 0.5)
            if tapCol >= 1 and tapCol <= FIELD_GRID_COLS and tapRow >= 1 and tapRow <= FIELD_GRID_ROWS then
                tapSlot = (tapRow-1)*FIELD_GRID_COLS + (tapCol-1)
            end
        end
        if tapSlot >= 0 then
            FieldSel = tapSlot
        end
    end
    FieldTapWas = fieldPressed

    local fieldJoyX, fieldJoyY = GetJoystickXY()
    if not FieldNavMoved then
        local dir = 0
        local axis = 0
        if math.abs(fieldJoyX) > 0.2 then
            dir = fieldJoyX > 0 and 1 or -1
            axis = 1
        elseif math.abs(fieldJoyY) > 0.2 then
            dir = fieldJoyY > 0 and 1 or -1
            axis = 2
        end
        if dir ~= 0 then
            local target = FieldSel
            if FieldSel <= 54 then
                local navCol = FieldSel % FIELD_GRID_COLS
                local navRow = FieldSel // FIELD_GRID_COLS
                if axis == 1 then
                    local newCol = navCol + dir
                    if newCol < 0 or newCol >= FIELD_GRID_COLS then
                        target = FIELD_TOOL_FOR_ROW[navRow + 1]
                    else
                        target = navRow * FIELD_GRID_COLS + newCol
                    end
                else
                    target = ((navRow + dir) % FIELD_GRID_ROWS) * FIELD_GRID_COLS + navCol
                end
            else
                if axis == 1 then
                    target = (dir > 0 and 0 or FIELD_GRID_COLS - 1) + FIELD_TOOL_ROW[FieldSel - 54] * FIELD_GRID_COLS
                else
                    target = 55 + ((FieldSel - 55 + dir) % 4)
                end
            end
            if FieldSlotActionable(target) then
                FieldSel = target
            end
            FieldNavMoved = true
        end
    elseif math.abs(fieldJoyX) <= 0.2 and math.abs(fieldJoyY) <= 0.2 then
        FieldNavMoved = false
    end

    local fieldJoyBtn = GetJoystickButton()
    if fieldJoyBtn and not FieldJoyWas then
        FieldApplySlot(FieldSel)
    end
    FieldJoyWas = fieldJoyBtn

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
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_PLOW, FIELD_BRUSH_RADIUS)
        elseif fieldItemSelection == FIELD_ITEM_SELECTION_WATERING_CAN then
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_WATER, FIELD_BRUSH_RADIUS)
        elseif fieldItemSelection == FIELD_ITEM_SELECTION_SEEDBAG then
            UpdateFieldGrid(touchX, touchY, FIELD_GRID_ACTION_SEED_WHEAT, FIELD_BRUSH_RADIUS)
        end
    end
    for col=1,#FieldGrid do
        for row=1,#(FieldGrid[1]) do
            fg = FieldGrid[col][row]
            if fg.plowed == false then
                
            elseif fg.watered == false then
                DrawSprite(SField_dirt, 4+col*23, 5+row*23, {frame=0, flags=ALIGN_H_CENTER|ALIGN_V_CENTER})
            elseif fg.plant == FIELD_GRID_PLANT_NONE then
                DrawSprite(SField_dirt, 4+col*23, 5+row*23, {frame=1, flags=ALIGN_H_CENTER|ALIGN_V_CENTER})
            end
            if fg.plant == FIELD_GRID_PLANT_WHEAT then
                DrawSprite(SField_plantWheat, 4+col*23, 5+row*23, {frame=fg.growStage, flags=ALIGN_H_CENTER|ALIGN_V_CENTER})
            end
        end
    end

    local cursorX, cursorY, cursorW, cursorH = FieldSlotCursorRect(FieldSel)
    local glowSprite = SGlowCell
    if FieldSel > 54 then
        glowSprite = SGlowTool
    end
    local glowW = SpriteWidth(glowSprite)
    local glowH = SpriteHeight(glowSprite)
    local glowX = cursorX + cursorW/2 - glowW/2
    local glowY = cursorY + cursorH/2 - glowH/2
    if glowX < 0 then
        glowX = 0
    elseif glowX > 320-glowW then
        glowX = 320-glowW
    end
    if glowY < 0 then
        glowY = 0
    elseif glowY > 240-glowH then
        glowY = 240-glowH
    end
    DrawSprite(glowSprite, glowX, glowY, {alpha=math.sin(Ms/100)*0.5+0.5})
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
