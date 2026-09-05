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
  return getIntIniValueFromSection(resBuffer, "safariRarity", &ignoreErrors);
}

uint16_t getMaxMonsterCount(String gameIniPath, String* errorMessage) {
  char resBuffer[1024];
  getIniSection(gameIniPath, "[game]", (char*)resBuffer, 1024, errorMessage);
  if (!errorMessage->isEmpty()) {
    return 0;
  }
  String ignoreErrors;
  return getIntIniValueFromSection(resBuffer, "maxMonsterCount", &ignoreErrors);
}

uint16_t getMonsterCount(String gameIniPath, String* errorMessage) {
  char resBuffer[1024];
  getIniSection(gameIniPath, String("[game]"), (char*)resBuffer, 1024, errorMessage);
  if (!errorMessage->isEmpty()) {
    return 0;
  }
  String ignoreErrors;
  return getIntIniValueFromSection(resBuffer, "monsterCount", &ignoreErrors);
}

uint16_t getRandomMonsterId(String gameIniPath, String* errorMessage) {
  return random(1, getMonsterCount(gameIniPath, errorMessage)+1);
}

uint16_t getSafariMonster(String gameIniPath, uint8_t targetRarity, String* errorMessage) {
  uint16_t id = 0;
  targetRarity = _min(3, _max(1, targetRarity));
  uint16_t monsterCount = getMonsterCount(gameIniPath, errorMessage);
  while (id == 0) {
    id = random(1, monsterCount+1);
    uint8_t monsterRarity = getMonsterSafariRarity(gameIniPath, id, errorMessage);
    if (monsterRarity == 0 || monsterRarity>targetRarity) {
      id = 0;
    }
  }
  return id;
}

uint16_t getEvolutionTarget(char* sectionData, String* errorMessage) {
  String value;
  getIniValueFromSection(sectionData, "evolvesTo", &value, errorMessage);
  String token = getRandomTokenCSV(value);
  token.trim();
  return atoi(token.c_str());
}

AttackFunctionType getAttackFunctionFromIdentifier(String attackFunctionIdentifier) {
  Serial.print("Getting attack for ");
  Serial.println(attackFunctionIdentifier);
  if (attackFunctionIdentifier == "confusion") {
    return attackFunction_confusion;
  } else if (attackFunctionIdentifier.equals("ember")) {
    return attackFunction_ember;
  } else if (attackFunctionIdentifier.equals("throwFastAnim")) {
    return attackFunction_throwFastAnim;
  } else if (attackFunctionIdentifier.equals("scratch")) {
    return attackFunction_scratch;
  } else if (attackFunctionIdentifier.equals("lightning")) {
    return attackFunction_lightning;
  } else if (attackFunctionIdentifier.equals("catch")) {
    return attackFunction_catch;
  } else if (attackFunctionIdentifier.equals("rareCandy")) {
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
  attackData->animFrames = getIntIniValueFromSection(resBuffer, "animFrames", &ignoreErrors);
  getIniValueFromSection(resBuffer, "imagePath", &(attackData->imagePath), &ignoreErrors);
  String output;
  getIniValueFromSection(resBuffer, "attackFunction", &output, errorMessage);
  attackData->attackFunction = getAttackFunctionFromIdentifier(output);
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
    getIniValueFromSection(resBuffer, "name", &(monsterData->name), errorMessage);
    getIniValueFromSection(resBuffer, "imagePath", &(monsterData->imagePath), errorMessage);
    String isBasicMonster;
    getIniValueFromSection(resBuffer, "basicMonster", &isBasicMonster, errorMessage);
    monsterData->isBasicMonster = (isBasicMonster == "True") || (isBasicMonster == "true") || (isBasicMonster == "1");
    monsterData->evolvesTo = getEvolutionTarget(resBuffer, &ignoreErrors);
    monsterData->safariRarity = getIntIniValueFromSection(resBuffer, "safariRarity", &ignoreErrors);
    getIniValueFromSection(resBuffer, "attackIdentifier", &attackIdentifier, &ignoreErrors);
    if (!errorMessage->isEmpty()) {
      return;
    }
  }
  loadAttackData(gameIniPath, &(monsterData->attack), attackIdentifier, errorMessage);
}