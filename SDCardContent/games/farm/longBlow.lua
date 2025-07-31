DisableCaching()
DrawSprite(SFarm[3], 0, 0)
DrawAnimSprite(SFarmer[1 + (Ms//5000) % 2], 275, 135, (Ms//1000) % 2)
DrawAnimSpriteScaled(SFarmer[3 + (Ms//5000) % 2], 170, 145, -1.25, 1.25, (Ms//1000) % 2)
DrawFastVLine(170, 175, 20, 0x0000)

DrawAnimSpriteScaled(SFarmer[1 + (Ms//5000) % 2], 107, 97, -0.7, 0.7, (Ms//1000) % 2)
DrawAnimSpriteScaled(SFarmer[1 + (Ms//5000) % 2], 120, 115, -0.75, 0.75, (Ms//1000) % 2)