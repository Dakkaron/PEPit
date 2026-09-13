DrawSprite(SFarm[3], 0, 0)
DrawSprite(SFarmer[1 + (Ms//5000) % 2], 275, 135, {frame=(Ms//1000) % 2})
DrawSprite(SFarmer[3 + (Ms//5000) % 2], 170, 145, {scaleX=-1.25, scaleY=1.25, frame=(Ms//1000) % 2})
DrawFastVLine(170, 175, 20, 0x0000)

DrawSprite(SFarmer[1 + (Ms//5000) % 2], 107, 97, {scaleX=-0.7, scaleY=0.7, frame=(Ms//1000) % 2})
DrawSprite(SFarmer[1 + (Ms//5000) % 2], 120, 115, {scaleX=-0.75, scaleY=0.75, frame=(Ms//1000) % 2})