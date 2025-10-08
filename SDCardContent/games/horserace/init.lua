function serializeByteList(l)
  out = ""
  for i=1,#l do
    out = out .. string.format("%02x", l[i] % 256)
  end
  return out
end

function deserializeByteList(s)
  out = {}
  for i=1,#s/2 do
    val = string.sub(s, i*2-1, i*2)
    out[i] = tonumber("0x"..val)
  end
  return out
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
SHorse = LoadAnimSprite("gfx/horse.bmp", 23, 81, 0, 0xf81f)
SHorseAngledLeft = LoadAnimSprite("gfx/horse_angled_right.bmp", 42, 80, 1, 0xf81f)
SHorseAngledRight = LoadAnimSprite("gfx/horse_angled_right.bmp", 42, 80, 0, 0xf81f)

currentLeague = PrefsGetInt("league", 5)
ownPoints = PrefsGetInt("points", 0)

if currentLeague == 5 then
  leagueCompetitors = {
    name = {"Victor Espinosa", "Anthony Darman", "Gustavo E Calvante", "Harry Coffan", "Maximiliano Aserin", "Jose Lezcan", "Callum Rodrigez", "P J MacDonen", "Joaquin Herran", "Craig Gryllan", "Billy Eganes", "Brad Parnum", "Erik Asmundsen", "Jason Harte", "Ryan Malone", "Antonio Oranis", "Patrick Carbin", "Ben Allain", "D S Machada"},
  }
end

leagueCompetitors["points"] = deserializeByteList(PrefsGetString("lp", "00000000000000000000000000000000000000")) -- 19 competitor scores


Competitors = {
  name = {"Victor Espinosa", "Anthony Darman", "Gustavo E Calvante", "Harry Coffan", "Maximiliano Aserin", "Jose Lezcan"},
  speed = {0.006, 0.007, 0.008, 0.009, 0.010, 0.011},
  distance = {0, 0, 0, 0, 0, 0},
  xpos = {-65, -39, -13, 13, 39, 65},
}

function shuffle(array)
    for i = #array, 2, -1 do
        local j = math.random(i)
        array[i], array[j] = array[j], array[i]
    end
end

shuffle(Competitors.xpos)

function drawSpriteToRoad(handle, roadX, roadY, scaleX, scaleY)
  if (scaleY == nil) then
    scaleY = scaleX
  end
  x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
  if (y~=-1000 and y>=DRAW_HORIZON) then
    scaleX = scaleX*scaleFactor
    scaleY = scaleY*scaleFactor
    DrawSpriteScaled(handle, x-SpriteWidth(handle)*scaleX*0.5, y-SpriteHeight(handle)*scaleY, scaleX, scaleY)
  end
end

function drawAnimSpriteToRoad(handle, roadX, roadY, scaleX, scaleY, frame)
  x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
  if (y~=-1000 and y>=DRAW_HORIZON) then
    scaleX = scaleX*scaleFactor
    scaleY = scaleY*scaleFactor
    DrawAnimSpriteScaled(handle, x-SpriteWidth(handle)*scaleX*0.5, y-SpriteHeight(handle)*scaleY, scaleX, scaleY, frame)
  end
end

function drawHorse(handle, roadX, roadY, height, scaleX, scaleY, angle, frame, name)
  x, y, scaleFactor = ProjectRoadPointToScreen(roadX, roadY, HORIZON_Y, BASELINE_Y, RoadXOffset)
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