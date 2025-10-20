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

CurrentLeague = PrefsGetInt("league", 5)
OwnPoints = PrefsGetInt("points", 0)

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
