-- Progression menu: upgrade shop. Coins are earned by flying distance
-- (mainGame.lua) and persisted in Prefs; buying updates the physics
-- globals immediately so upgrades take effect in the running session.
if not MenuTouch then MenuTouch = {} end
if not MenuLastActionMs then MenuLastActionMs = 0 end

COL_GOLD = rgb(232, 200, 90)
COL_GRAY = rgb(110, 110, 110)
SKIN_NAME = { "Rot", "Blau", "Grün", "Gold" }

function spend(price)
  PrefsSetInt("coins", PrefsGetInt("coins") - price)
end

function buySpeed()
  if SPEED_LEVEL < 3 and PrefsGetInt("coins") >= SPEED_PRICE[SPEED_LEVEL + 1] then
    spend(SPEED_PRICE[SPEED_LEVEL + 1])
    SPEED_LEVEL = SPEED_LEVEL + 1
    MAX_SPEED = MAX_SPEED + SPEED_STEP
    PrefsSetInt("speedLevel", SPEED_LEVEL)
  end
end

function buyBurner()
  if BURNER_LEVEL < 2 and PrefsGetInt("coins") >= BURNER_PRICE[BURNER_LEVEL + 1] then
    spend(BURNER_PRICE[BURNER_LEVEL + 1])
    BURNER_LEVEL = BURNER_LEVEL + 1
    FUEL_PER_BLOW = FUEL_PER_BLOW + BURNER_STEP
    PrefsSetInt("burnerLevel", BURNER_LEVEL)
  end
end

function buyInsulation()
  if INSULATION_LEVEL < 2 and PrefsGetInt("coins") >= INSUL_PRICE[INSULATION_LEVEL + 1] then
    spend(INSUL_PRICE[INSULATION_LEVEL + 1])
    INSULATION_LEVEL = INSULATION_LEVEL + 1
    IDLE_DRAIN = IDLE_DRAIN - INSUL_IDLE_STEP
    STEER_DRAIN = STEER_DRAIN - INSUL_STEER_STEP
    PrefsSetInt("insulationLevel", INSULATION_LEVEL)
  end
end

function selectOrBuySkin(i)
  if ((SKINS_UNLOCKED >> i) & 1) == 1 then
    SKIN = i
    PrefsSetInt("skin", SKIN)
  elseif PrefsGetInt("coins") >= SKIN_PRICE[i + 1] then
    spend(SKIN_PRICE[i + 1])
    SKINS_UNLOCKED = SKINS_UNLOCKED | (1 << i)
    PrefsSetInt("skinsUnlocked", SKINS_UNLOCKED)
    SKIN = i
    PrefsSetInt("skin", SKIN)
  end
end

-- One press = one action: the handler only fires on the touch-down edge,
-- and the cooldown absorbs jittery touch signals between frames.
function Button(id, x, y, w, h, label, enabled, action)
  FillRect(x, y, w, h, enabled and COL_GOLD or COL_GRAY)
  SetTextColor(COL_BLACK)
  SetTextSize(1)
  DrawString(label, x + 5, y + 4)
  local touched = IsTouchInZone(x, y, w, h)
  if touched and not MenuTouch[id] then
    MenuTouch[id] = true
    if enabled and Ms - MenuLastActionMs > 400 then
      MenuLastActionMs = Ms
      action()
    end
  elseif not touched then
    MenuTouch[id] = false
  end
end

local coins = PrefsGetInt("coins")

FillRect(0, 0, 320, 240, COL_BLACK)
SetTextSize(3)
DrawString("Heißluftballon", 10, 8)
SetTextSize(2)
DrawString("Münzen: " .. coins, 20, 32)
DrawString("Distanz: " .. (Distance // PX_PER_M) .. " m", 170, 32)

local upgrades = {
  { name = "Düse (+Speed)",     level = SPEED_LEVEL,      max = 3,
    price = SPEED_PRICE[SPEED_LEVEL + 1], action = buySpeed },
  { name = "Brenner (+Tank)",   level = BURNER_LEVEL,     max = 2,
    price = BURNER_PRICE[BURNER_LEVEL + 1], action = buyBurner },
  { name = "Isolierung",        level = INSULATION_LEVEL, max = 2,
    price = INSUL_PRICE[INSULATION_LEVEL + 1], action = buyInsulation },
}
for i, u in ipairs(upgrades) do
  local y = 52 + (i - 1) * 22
  SetTextColor(COL_WHITE)
  SetTextSize(1)
  DrawString(u.name .. "  " .. u.level .. "/" .. u.max, 20, y)
  Button("upg" .. i, 240, y - 4, 66, 14,
         u.level >= u.max and "Max" or ("Kaufen " .. u.price),
         u.level < u.max and coins >= u.price, u.action)
end

SetTextColor(COL_WHITE)
SetTextSize(1)
DrawString("Farben:", 20, 120)
for i = 0, 3 do
  local unlocked = ((SKINS_UNLOCKED >> i) & 1) == 1
  Button("skin" .. i, 80 + i * 40, 116, 26, 14,
         unlocked and SKIN_NAME[i + 1] or "?",
         unlocked or coins >= SKIN_PRICE[i + 1],
         function() selectOrBuySkin(i) end)
end

DrawSprite(BalloonSkin[SKIN + 1], 10, 146)
SetTextColor(COL_WHITE)
SetTextSize(1)
DrawString("Speed " .. MAX_SPEED, 64, 146)
DrawString("Tank +", 64, 158)
DrawString(FUEL_PER_BLOW, 100, 158)

FillRect(240, 200, 90, 40, 0x001F)
SetTextSize(2)
DrawString("Fertig", 255, 215)
if IsTouchInZone(240, 200, 80, 40) then
  CloseProgressionMenu()
end
