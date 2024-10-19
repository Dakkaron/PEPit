#include "monsterDetails.h"

String getRandomTokenCSV(String input) {
  int tokenCount = 1;
  for (int i = 0; i < input.length(); i++) {
    if (input[i] == ',') {
      tokenCount++;
    }
  }

  int randomIndex = random(0, tokenCount);
  int tokenIndex = 0;
  String token = "";
  
  for (int i = 0; i <= input.length(); i++) {
    if (input[i] == ',' || i == input.length()) {
      if (tokenIndex == randomIndex) {
        return token;
      }
      token = "";
      tokenIndex++;
    } else {
      token += input[i];
    }
  }

  return "";
}

uint8_t getMonsterSafariRarity(String gameIniPath, uint16_t monsterId, String* errorMessage) {
  char resBuffer[1024];
  getIniSection(gameIniPath, String("[monster_") + String(monsterId) + String("]"), (char*)resBuffer, 1024, errorMessage);
  if (!errorMessage->isEmpty()) {
    return 0;
  }
  String ignoreErrors;
  return atoi(getIniValueFromSection(resBuffer, "safariRarity", &ignoreErrors).c_str());
}

uint16_t getSafariMonster(String gameIniPath, uint8_t targetRarity, String* errorMessage) {
  uint16_t id = 0;
  targetRarity = _min(3, _max(1, targetRarity));
  while (id == 0) {
    id = random(1,TOTAL_MONSTER_NUMBER+1);
    uint8_t monsterRarity = getMonsterSafariRarity(gameIniPath, id, errorMessage);
    if (monsterRarity == 0 || monsterRarity>targetRarity) {
      id = 0;
    }
  }
  return id;
}

uint16_t getEvolutionTarget(char* sectionData, String* errorMessage) {
  String value = getIniValueFromSection(sectionData, "evolvesTo", errorMessage);
  String token = getRandomTokenCSV(value);
  token.trim();
  return atoi(token.c_str());
}

AttackFunctionType getAttackFunctionFromIdentifier(String attackFunctionIdentifier) {
  Serial.print("Getting attack for ");
  Serial.println(attackFunctionIdentifier);
  if (attackFunctionIdentifier == "confusion") {
    return attackFunction_confusion;
  } else if (attackFunctionIdentifier = "ember") {
    return attackFunction_ember;
  } else if (attackFunctionIdentifier = "throwFastAnim") {
    return attackFunction_throwFastAnim;
  } else if (attackFunctionIdentifier = "scratch") {
    return attackFunction_scratch;
  } else if (attackFunctionIdentifier = "lightning") {
    return attackFunction_lightning;
  } else if (attackFunctionIdentifier = "catch") {
    return attackFunction_catch;
  } else if (attackFunctionIdentifier = "rareCandy") {
    return attackFunction_rareCandy;
  }
  return NULL;
}

void loadAttackData(String gameIniPath, AttackData* attackData, String attackIdentifier, String* errorMessage) {
  char resBuffer[1024];
  if (attackIdentifier == "") {
    Serial.println("Monster has no attack identifier!");
    return;
  }
  getIniSection(gameIniPath, String("[attack_") + attackIdentifier + String("]"), (char*)resBuffer, 1024, errorMessage);
  if (!errorMessage->isEmpty()) {
    return;
  }
  String ignoreErrors;
  attackData->identifier = attackIdentifier;
  attackData->animFrames = atoi(getIniValueFromSection(resBuffer, "animFrames", &ignoreErrors).c_str());
  attackData->imagePath = getIniValueFromSection(resBuffer, "imagePath", &ignoreErrors);
  attackData->attackFunction = getAttackFunctionFromIdentifier(getIniValueFromSection(resBuffer, "attackFunction", errorMessage));
  if (!errorMessage->isEmpty()) {
    return;
  }
  Serial.println("Attack data");
  Serial.println(attackData->identifier);
  Serial.println(attackData->animFrames);
  Serial.println(attackData->imagePath);
}

void loadMonsterData(String gameIniPath, MonsterData* monsterData, uint16_t monsterId, String* errorMessage) {
  Serial.println("LOAD MONSTER DATA");
  String attackIdentifier;
  {
    char resBuffer[1024];
    String ignoreErrors;
    getIniSection(gameIniPath, String("[monster_") + String(monsterId) + String("]"), (char*)resBuffer, 1024, errorMessage);
    if (!errorMessage->isEmpty()) {
      return;
    }
    monsterData->id = monsterId;
    monsterData->name = getIniValueFromSection(resBuffer, "name", errorMessage);
    monsterData->imagePath = getIniValueFromSection(resBuffer, "imagePath", errorMessage);
    String isBasicMonster = getIniValueFromSection(resBuffer, "basicMonster", errorMessage);
    monsterData->isBasicMonster = (isBasicMonster == "True") || (isBasicMonster == "true") || (isBasicMonster == "1");
    monsterData->evolvesTo = getEvolutionTarget(resBuffer, &ignoreErrors);
    monsterData->safariRarity = atoi(getIniValueFromSection(resBuffer, "safariRarity", &ignoreErrors).c_str());
    attackIdentifier = getIniValueFromSection(resBuffer, "attackIdentifier", &ignoreErrors);
    if (!errorMessage->isEmpty()) {
      return;
    }
  }
  loadAttackData(gameIniPath, &(monsterData->attack), attackIdentifier, errorMessage);
}