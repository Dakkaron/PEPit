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
        DrawSprite(SFieldSelection, 0, 78, 0.1)--math.sin(Ms/100)*0.5+0.5)
    else
        DrawSprite(SFieldSelection, 0, 78, 0.1)
    end

    if JoystickSelection == 0 then
        DrawSprite(SBerrySelection, 0, 87, math.sin(Ms/100)*0.5+0.5)
    elseif Energy>=0.2 then
        DrawSprite(SBerrySelection, 0, 87, 0.5)
    end

    if JoystickSelection == 1 then
        DrawSprite(SFishSelection, 174, 127, math.sin(Ms/100)*0.5+0.5)
    elseif Energy>=0.2 then
        DrawSprite(SFishSelection, 174, 127, 0.5)
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
            ShowEarningsText = "+10"
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
                ShowEarningsText = "+0"
            elseif fishResult < 0.5 then
                ShowEarningSpriteFrame = 1
                ShowEarningsText = "+5"
                Money = Money + 5
            elseif fishResult < 0.75 then
                ShowEarningSpriteFrame = 2
                ShowEarningsText = "+12"
                Money = Money + 12
            elseif fishResult < 0.95 then
                ShowEarningSpriteFrame = 3
                ShowEarningsText = "+20"
                Money = Money + 20
            else
                ShowEarningSpriteFrame = 4
                ShowEarningsText = "+30"
                Money = Money + 30
            end
        elseif (IsTouchInZone(0, 79, 170, 17) or IsTouchInZone(110, 79, 60, 34) or (JoystickSelection==1 and GetJoystickButton())) and Energy >= 0.2 then --Field
            GameMode = GAME_MODE_FIELD
        end

        if joystickX < -0.5 then
            JoystickSelection = JoystickSelection - 1
        elseif joystickX > 0.5 then
            JoystickSelection = JoystickSelection + 1
        end
    end

    Touched = IsTouchInZone(0,0,320,240) or GetJoystickButton() or math.abs(joystickX) > 0.5
elseif GameMode == GAME_MODE_FIELD then
    DrawSprite(SField_fallow, 0, 0)
    DrawSprite(SField_tilled, 0, 0)
    DrawSprite(SField_watered, 0, 0)
end

if Energy < 0.95 then
    DrawAnimSprite(SEnergyBar, 0, 145, 1)
    DrawAnimSpriteScaled(SEnergyBar, 0+26, 145, 5, 1, 2)
    DrawAnimSprite(SEnergyBar, 0+26*6, 145, 4)
    DrawAnimSpriteScaled(SEnergyBar, 0+26, 145, Energy*6, 1, 3)
    DrawAnimSpriteScaled(SEnergyBar, 0+26 + Energy*26*6, 145, 1, 1, 6)
else
    DrawAnimSprite(SEnergyBar, 0, 145, 1)
    DrawAnimSpriteScaled(SEnergyBar, 0+26, 145, 5, 1, 3)
    DrawAnimSprite(SEnergyBar, 0+26*6, 145, 5)
end

if Ms < ShowEarningSpriteUntil then
    local showSpriteYShift = (2000-(ShowEarningSpriteUntil-Ms))/10
    if ShowEarningSpriteFrame < 0 then
        DrawSpriteScaled(ShowEarningSprite, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift, ShowEarningSpriteScaleX, ShowEarningSpriteScaleY)
    else
        DrawAnimSpriteScaled(ShowEarningSprite, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift, ShowEarningSpriteScaleX, ShowEarningSpriteScaleY, ShowEarningSpriteFrame)
    end
    SetTextSize(2)
    DrawString(ShowEarningsText, ShowEarningSpriteX, ShowEarningSpriteY - showSpriteYShift - 10)
end

SetTextSize(2)
SetTextColor(0xFFFF)
DrawString("$" .. Money, 190, 220)
DrawString("Ms " .. math.sin(Ms/100)*0.5+0.5, 10, 20)