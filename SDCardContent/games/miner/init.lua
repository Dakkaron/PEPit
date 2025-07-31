function IsNextToTunnel(x, y)
  return (x>0 and WorldMapArray[x-1][y]==0) or (x<7 and WorldMapArray[x+1][y]==0) or (y>0 and WorldMapArray[x][y-1]==0) or (y<5 and WorldMapArray[x][y+1]==0) or (x>0 and y>0 and WorldMapArray[x-1][y-1]==0) or (x<7 and y>0 and WorldMapArray[x+1][y-1]==0) or (x>0 and y<5 and WorldMapArray[x-1][y+1]==0) or (x<7 and y<5 and WorldMapArray[x+1][y+1]==0)
end

function DrawFOWDirt(x, y, yOffset, showHidden)
  if (showHidden) then
    DrawSprite(Tiles.dirt, x*40, y*40 - yOffset)
  elseif ((x==0 or not IsNextToTunnel(x-1, y)) and (y==0 or not IsNextToTunnel(x, y-1))) then
    DrawSprite(Tiles.shadowTopLeft, x*40, y*40 - yOffset)
  elseif ((x==7 or not IsNextToTunnel(x+1, y)) and (y==0 or not IsNextToTunnel(x, y-1))) then
    DrawSprite(Tiles.shadowTopRight, x*40, y*40 - yOffset)
  elseif ((x==0 or not IsNextToTunnel(x-1, y)) and (y==5 or not IsNextToTunnel(x, y+1))) then
    DrawSprite(Tiles.shadowBottomLeft, x*40, y*40 - yOffset)
  elseif ((x==7 or not IsNextToTunnel(x+1, y)) and (y==5 or not IsNextToTunnel(x, y+1))) then
    DrawSprite(Tiles.shadowBottomRight, x*40, y*40 - yOffset)
  elseif (x==0 or not IsNextToTunnel(x-1, y)) then
    DrawSprite(Tiles.shadowLeft, x*40, y*40 - yOffset)
  elseif (x==7 or not IsNextToTunnel(x+1, y)) then
    DrawSprite(Tiles.shadowRight, x*40, y*40 - yOffset)
  elseif (y==0 or not IsNextToTunnel(x, y-1)) then
    DrawSprite(Tiles.shadowTop, x*40, y*40 - yOffset)
  elseif (y==5 or not IsNextToTunnel(x, y+1)) then
    DrawSprite(Tiles.shadowBottom, x*40, y*40 - yOffset)
  else
    DrawSprite(Tiles.dirt, x*40, y*40 - yOffset)
  end
end

function DrawTile(x, y, yOffset, showHidden)
  if (WorldMapArray[x][y] == 0) then
    if (WorldMapArray[x][y-1] == 0 or y == 0) then -- up
      if (x > 0 and WorldMapArray[x-1][y] == 0) then -- left
        if (x < 7 and WorldMapArray[x+1][y] == 0) then -- right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelX, x*40, y*40 - yOffset)
          else -- up, left, right, not down
            DrawSprite(Tiles.tunnelT, x*40, y*40 - yOffset)
          end
        else -- up, left, not right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelTLeft, x*40, y*40 - yOffset)
          else -- not down
            DrawSprite(Tiles.tunnelLLeft, x*40, y*40 - yOffset)
          end
        end
      else -- up, not left
        if (x < 7 and WorldMapArray[x+1][y] == 0) then -- right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelTRight, x*40, y*40 - yOffset)
          else -- not down
            DrawSprite(Tiles.tunnelLRight, x*40, y*40 - yOffset)
          end
        else -- up, not left, not right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelI, x*40, y*40 - yOffset)
          else -- not down
            DrawSprite(Tiles.tunnel, x*40, y*40 - yOffset)
          end
        end
      end
    else -- not up
      if (x > 0 and WorldMapArray[x-1][y] == 0) then -- left
        if (x < 7 and WorldMapArray[x+1][y] == 0) then -- right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelTDown, x*40, y*40 - yOffset)
          else -- not up, left, right, not down
            DrawSprite(Tiles.tunnelFlat, x*40, y*40 - yOffset)
          end
        else -- not up, left, not right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelFLeft, x*40, y*40 - yOffset)
          else -- not down
            DrawSprite(Tiles.tunnelLeft, x*40, y*40 - yOffset)
          end
        end
      else -- not up, not left
        if (x < 7 and WorldMapArray[x+1][y] == 0) then -- right
          if (WorldMapArray[x][y+1] == 0) then -- down
            DrawSprite(Tiles.tunnelFRight, x*40, y*40 - yOffset)
          else -- not down
            DrawSprite(Tiles.tunnelRight, x*40, y*40 - yOffset)
          end
        end
      end
    end
  elseif (showHidden or IsNextToTunnel(x, y)) then
    DrawFOWDirt(x, y, yOffset, showHidden)
    if (WorldMapArray[x][y] == 2) then
      DrawSprite(STileOreIron, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 3) then
      DrawSprite(STileOreCopper, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 4) then
      DrawSprite(STileOreEmerald, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 5) then
      DrawSprite(STileOreGold, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 6) then
      DrawSprite(STileOreDiamond, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 7) then
      DrawSprite(STileRock, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 8) then
      DrawSprite(STileLava, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 101) then
      DrawSprite(STileTreasureDino, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 102) then
      DrawSprite(STileTreasureBook, x*40, y*40 - yOffset)
    elseif (WorldMapArray[x][y] == 103) then
      DrawSprite(STileTreasureAlien, x*40, y*40 - yOffset)
    end
  end
end

function GetEarnings(type)
  if (type == 2) then
    return 1
  elseif (type == 3) then
    return 2
  elseif (type == 4) then
    return 3
  elseif (type == 5) then
    return 4
  elseif (type == 6) then
    return 5
  elseif (type == 101) then
    return 15
  elseif (type == 102) then
    return 20
  elseif (type == 103) then
    return 25
  end
  return 0
end

function MineTile(x,y)
  local r = GetEarnings(WorldMapArray[x][y])
  if (r>0) then
    EarnValue = EarnValue + r
    EarnTime = Ms + 2000
    Money = Money + r
  end
  WorldMapArray[x][y] = 0
  return r
end

StrX = -1
StrY = -1
StrW = 0
StrH = 0
StrArray = {}

function GenStructure(x, y, typ)
  if typ == 0 then
    WorldMapArray[x][y] = StrArray[y-StrY+1][x-StrX+1]
  else
    StrArray = {}
    if typ == 1 then
      StrArray[1] = {1,7,1}
      StrArray[2] = {7,101,7}
    elseif typ == 2 then
      StrArray[1] = {7,7,1}
      StrArray[2] = {7,102,7}
    elseif typ == 3 then
      StrArray[1] = {7,7,7}
      StrArray[2] = {7,103,7}
    end
    StrW = 3
    StrH = 2
    StrX = x
    StrY = y
    GenStructure(x, y, 0)
  end
end

function GenTile(x, y, baseOdds)
  baseOdds = baseOdds or 1000
  local v = math.random(0, baseOdds)
  if (StrX~=-1 and x >= StrX and x < StrX+StrW and y >= StrY and y < StrY + StrH) then
    GenStructure(x, y, 0)
    return
  end
  if (y > StrY + StrH + 1) then
    StrX = -1
    StrY = -1
    StrW = 0
    StrH = 0
  end
  if (Stage == 1) then
    if (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 70) then
      WorldMapArray[x][y] = 3
    elseif (v < 170) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 2) then
    if (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 50) then
      WorldMapArray[x][y] = 4
    elseif (v < 100) then
      WorldMapArray[x][y] = 3
    elseif (v < 200) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 3) then
    if (v < 3) then
      WorldMapArray[x][y] = 7 -- 8
    elseif (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 23 and StrX == -1 and x<=5) then
      GenStructure(x, y, 1)
    elseif (v < 40) then
      WorldMapArray[x][y] = 5
    elseif (v < 70) then
      WorldMapArray[x][y] = 4
    elseif (v < 120) then
      WorldMapArray[x][y] = 3
    elseif (v < 220) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 4) then
    if (v < 4) then
      WorldMapArray[x][y] = 7 -- 8
    elseif (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 21 and StrX == -1 and x<=5) then
      GenStructure(x, y, 2)
    elseif (v < 23 and StrX == -1 and x<=5) then
      GenStructure(x, y, 1)
    elseif (v < 25) then
      WorldMapArray[x][y] = 6
    elseif (v < 40) then
      WorldMapArray[x][y] = 5
    elseif (v < 70) then
      WorldMapArray[x][y] = 4
    elseif (v < 120) then
      WorldMapArray[x][y] = 3
    elseif (v < 220) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 5) then
    if (v < 4) then
      WorldMapArray[x][y] = 7 -- 8
    elseif (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 21 and StrX == -1 and x<=5) then
      GenStructure(x, y, 3)
    elseif (v < 22 and StrX == -1 and x<=5) then
      GenStructure(x, y, 2)
    elseif (v < 24 and StrX == -1 and x<=5) then
      GenStructure(x, y, 1)
    elseif (v < 27) then
      WorldMapArray[x][y] = 6
    elseif (v < 40) then
      WorldMapArray[x][y] = 5
    elseif (v < 70) then
      WorldMapArray[x][y] = 4
    elseif (v < 120) then
      WorldMapArray[x][y] = 3
    elseif (v < 220) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 6) then
    if (v < 5) then
      WorldMapArray[x][y] = 7 -- 8
    elseif (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 22 and StrX == -1 and x<=5) then
      GenStructure(x, y, 3)
    elseif (v < 23 and StrX == -1 and x<=5) then
      GenStructure(x, y, 2)
    elseif (v < 25 and StrX == -1 and x<=5) then
      GenStructure(x, y, 1)
    elseif (v < 29) then
      WorldMapArray[x][y] = 6
    elseif (v < 40) then
      WorldMapArray[x][y] = 5
    elseif (v < 70) then
      WorldMapArray[x][y] = 4
    elseif (v < 120) then
      WorldMapArray[x][y] = 3
    elseif (v < 220) then
      WorldMapArray[x][y] = 2
    else
      WorldMapArray[x][y] = 1
    end
  elseif (Stage == 7) then
    if (v < 6) then
      WorldMapArray[x][y] = 7 -- 8
    elseif (v < 20) then
      WorldMapArray[x][y] = 7
    elseif (v < 23 and StrX == -1 and x<=5) then
      GenStructure(x, y, 3)
    elseif (v < 25 and StrX == -1 and x<=5) then
      GenStructure(x, y, 2)
    elseif (v < 27 and StrX == -1 and x<=5) then
      GenStructure(x, y, 1)
    elseif (v < 33) then
      WorldMapArray[x][y] = 6
    elseif (v < 45) then
      WorldMapArray[x][y] = 5
    elseif (v < 70) then
      WorldMapArray[x][y] = 4
    elseif (v < 120) then
      WorldMapArray[x][y] = 3
    elseif (v < 220) then
      WorldMapArray[x][y] = 1
    else
      WorldMapArray[x][y] = 1
    end
  end
  WorldMapArray[x][y] = WorldMapArray[x][y]
end

function InitTileFragment(dirtPath, overlayPath, flip)
  local tile = LoadSprite(dirtPath)
  local tmp = LoadSprite(overlayPath, flip, 0x0000)
  local overlay = LoadSprite(overlayPath, flip, 0x0000)
  DrawSpriteToSprite(overlay, tmp, 0, 0) -- TODO: Fix DrawSpriteToSprite so that it doesn't invert colors
  DrawSpriteToSprite(tmp, tile, 0, 0)
  FreeSprite(tmp)
  FreeSprite(overlay)
  return tile
end

function InitTileFragments(tileTable, dirtPath)
  tileTable["dirt"] = LoadSprite(dirtPath, 0, 0x0000)
  tileTable["tunnel"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel.bmp", 0)
  tileTable["tunnelI"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_I.bmp", 0)
  tileTable["tunnelFlat"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_flat.bmp", 0)
  tileTable["tunnelLeft"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_left.bmp", 0)
  tileTable["tunnelRight"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_left.bmp", 1)
  tileTable["tunnelLLeft"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_L_left.bmp", 0)
  tileTable["tunnelLRight"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_L_left.bmp", 1)
  tileTable["tunnelFLeft"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_L_left.bmp", 2)
  tileTable["tunnelFRight"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_L_left.bmp", 3)
  tileTable["tunnelT"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_T.bmp", 0)
  tileTable["tunnelTDown"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_T.bmp", 2)
  tileTable["tunnelTLeft"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_T_left.bmp", 0)
  tileTable["tunnelTRight"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_T_left.bmp", 1)
  tileTable["tunnelX"] = InitTileFragment(dirtPath, "gfx/tilefragments/tile_tunnel_X.bmp", 0)

  local sFOWBottom = LoadSprite("gfx/tilefragments/tile_fow_down.bmp", 0, 0x0001)
  local sFOWTop = LoadSprite("gfx/tilefragments/tile_fow_down.bmp", 2, 0x0001)
  local sFOWLeft = LoadSprite("gfx/tilefragments/tile_fow_left.bmp", 0, 0x0001)
  local sFOWRight = LoadSprite("gfx/tilefragments/tile_fow_left.bmp", 1, 0x0001)
  tileTable["shadowLeft"] = LoadSprite(dirtPath)
  tileTable["shadowRight"] = LoadSprite(dirtPath)
  tileTable["shadowTop"] = LoadSprite(dirtPath)
  tileTable["shadowBottom"] = LoadSprite(dirtPath)
  tileTable["shadowTopLeft"] = LoadSprite(dirtPath)
  tileTable["shadowTopRight"] = LoadSprite(dirtPath)
  tileTable["shadowBottomLeft"] = LoadSprite(dirtPath)
  tileTable["shadowBottomRight"] = LoadSprite(dirtPath)

  DrawSpriteToSprite(sFOWLeft, tileTable["shadowLeft"], 0, 0)
  DrawSpriteToSprite(sFOWRight, tileTable["shadowRight"], 31, 0)
  DrawSpriteToSprite(sFOWTop, tileTable["shadowTop"], 0, 0)
  DrawSpriteToSprite(sFOWBottom, tileTable["shadowBottom"], 0, 31)

  DrawSpriteToSprite(sFOWLeft, tileTable["shadowTopLeft"], 0, 0)
  DrawSpriteToSprite(sFOWTop, tileTable["shadowTopLeft"], 0, 0)

  DrawSpriteToSprite(sFOWRight, tileTable["shadowTopRight"], 31, 0)
  DrawSpriteToSprite(sFOWTop, tileTable["shadowTopRight"], 0, 0)

  DrawSpriteToSprite(sFOWLeft, tileTable["shadowBottomLeft"], 0, 0)
  DrawSpriteToSprite(sFOWBottom, tileTable["shadowBottomLeft"], 0, 31)

  DrawSpriteToSprite(sFOWRight, tileTable["shadowBottomRight"], 31, 0)
  DrawSpriteToSprite(sFOWBottom, tileTable["shadowBottomRight"], 0, 31)

  FreeSprite(sFOWBottom)
  FreeSprite(sFOWTop)
  FreeSprite(sFOWLeft)
  FreeSprite(sFOWRight)
end

function FreeTileFragments(tileTable)
  for _, v in pairs(tileTable) do
    FreeSprite(v)
  end
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

Tiles = {}
Stage = PrefsGetInt("stage", 1)
InitTileFragments(Tiles, "gfx/tilefragments/tile_dirt" .. Stage .. ".bmp")

SRobotIdle = LoadSprite("gfx/idle.bmp", 0, 0x0001)
SRobotDig = LoadSprite("gfx/dig.bmp", 0, 0x0001)
SRobotFunnel = LoadSprite("gfx/funnel.bmp", 0, 0x0001)

SBigDigger = LoadSprite("gfx/bigdigger.bmp", 0, 0xf81f)

STileOreIron = LoadSprite("gfx/tilefragments/tile_ore_iron.bmp", 0, 0x0001)
STileOreCopper = LoadSprite("gfx/tilefragments/tile_ore_copper.bmp", 0, 0x0001)
STileOreEmerald = LoadSprite("gfx/tilefragments/tile_ore_emerald.bmp", 0, 0x0001)
STileOreGold = LoadSprite("gfx/tilefragments/tile_ore_gold.bmp", 0, 0x0001)
STileOreDiamond = LoadSprite("gfx/tilefragments/tile_ore_diamond.bmp", 0, 0x0001)

STileTreasureDino = LoadSprite("gfx/tilefragments/tile_treasure_dino.bmp", 0, 0xd03e)
STileTreasureBook = LoadSprite("gfx/tilefragments/tile_treasure_book.bmp", 0, 0xd03e)
STileTreasureAlien = LoadSprite("gfx/tilefragments/tile_treasure_alien.bmp", 0, 0xd03e)

STileRock = LoadSprite("gfx/tilefragments/tile_rock.bmp", 0, 0x0001)
STileLava = LoadSprite("gfx/tilefragments/tile_lava.bmp", 0, 0xFFFF)

SItemLantern = LoadSprite("gfx/item_lantern.bmp", 0, 0x0001)

SExplosionAnim = LoadAnimSprite("gfx/explosion-0.bmp", 80, 48, 0, 0x0001)

SArrowRight = LoadSprite("gfx/arrow.bmp", 0, 0xf81f)
SArrowLeft = LoadSprite("gfx/arrow.bmp", 1, 0xf81f)

SEffectBurned = LoadAnimSprite("gfx/effect_burned.bmp", 80, 80, 4, 0xf800)
SEffectGasWarning = LoadSprite("gfx/effect_gaswarning.bmp", 4, 0xffff)

math.randomseed(Ms)

WorldMapArray = {}
for x = 0, 7 do
  WorldMapArray[x] = {}
  for y = 0, 5 do
    GenTile(x,y)
  end
end
WorldMapArray[2][0] = 1
WorldMapArray[2][1] = 1
MineTile(2, 0)
MineTile(2, 1)

YDigOffset = 0
PlayerX = 2
PlayerXOffset = 0
IsCurrentlyTouching = false
StepsLeft = 2
Money = PrefsGetInt("money", 0)
EarnValue = 0
EarnTime = 0
FullVisibilityTime = 0
LastBlownTimer = -1
BurnStart = 0


-- Inhalation game
function GetDistancePointLine(l1x, l1y, l2x, l2y, px, py)
  return math.abs(((l2y - l1y) * px - (l2x - l1x) * py + l2x * l1y - l2y * l1x) / math.sqrt((l2y - l1y) * (l2y - l1y) + (l2x - l1x) * (l2x - l1x)))
end

InhalationCrosshairX = 85
InhalationCrosshairY = 135
InhalationVectorX = 1
InhalationVectorY = 0
TargetPositions = {{210.0, 120.0}, {200.0, 50.0}, {150.0, 20.0}}
TargetEffectiveness = {0.0, 0.0, 0.0}
TargetType = {2, 2, 2}
TargetVector = {}
TypeSprites = {0, STileOreIron, STileOreCopper, STileOreEmerald, STileOreGold, STileOreDiamond}
WasBlowing = false

-- PEPShort
RockImageLoaded = -1

-- Progression Menu
function DisplayBuyStage(price)
  if (Money < price) then
    FillRect(10, 60, 220, 35, 0xF800)
  else
    FillRect(10, 60, 220, 35, 0x001F)
    SetTextSize(1)
    DrawString("Nicht genug Geld", 15, 84)
  end

  SetTextSize(2)
  DrawString("Tiefer graben: $" .. price, 15, 65)
  if (Money >= price and IsTouchInZone(10, 60, 220, 35)) then
    Money = Money - price
    Stage = Stage + 1
    PrefsSetInt("stage", Stage)
    PrefsSetInt("money", Money)
  end
end

--[[
--  0 = tunnel
--  1 = dirt
--  2 = iron
--  3 = copper
--  4 = emerald
--  5 = gold
--  6 = diamond
--  7 = rock
--101 = treasure: dino
--102 = treasure: book
--103 = treasure: alien
]]
