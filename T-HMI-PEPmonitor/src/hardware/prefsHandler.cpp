#include "prefsHandler.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "base64.hpp"
#include "sdHandler.h"

#define PREFS_BACKUP_FILE_PATH "/prefsBackup.bin"

#define PREFS_BUFFER_LENGTH 10240

Preferences prefs;
static String gamePrefsNamespace;

void printNamespaces() {
  Serial.println("Namespaces in NVS:\n");
  nvs_iterator_t it;
  nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
  if (it == NULL) {
    Serial.println("No entries found in NVS.");
    return;
  }

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    Serial.print(info.namespace_name);
    Serial.print(" - ");
    Serial.println(info.key);
    nvs_entry_next(&it);
  }

  prefs.begin("touch");
  Serial.print("Free before entities: ");
  Serial.println(prefs.freeEntries());
  prefs.remove("levels");
  prefs.remove("nitro");
  Serial.print("Free after entities:  ");
  Serial.println(prefs.freeEntries());
  prefs.end();
}

void setGamePrefsNamespace(String name) {
  gamePrefsNamespace = name;
  prefs.begin(name.c_str());
}

void applyGamePrefsNamespace() {
  if (gamePrefsNamespace.isEmpty()) {
    return;
  }
  prefs.begin(gamePrefsNamespace.c_str());
}

void clearPreferencesExceptSystem() {
  Serial.println("Clearing preferences except screen calibration.");
  uint8_t calibBytes[16];
  prefs.begin("system");
  prefs.getBytes("touch", calibBytes, sizeof(calibBytes));
  prefs.clear();
  prefs.end();

  nvs_flash_erase();
  nvs_flash_init();
  
  // Reinitialize the preferences to keep the touch calibration
  prefs.begin("system");
  prefs.putBytes("touch", calibBytes, sizeof(calibBytes));
  prefs.end();
}

void dumpNamespaceContents() {
  Serial.println("Dumping namespaces in Base64:");
  nvs_iterator_t it;
  nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
  if (it == NULL) {
    Serial.println("No entries found in NVS.");
    return;
  }

  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (!strcmp(info.namespace_name, "dhcp_state") ||
        !strcmp(info.namespace_name, "phy") ||
        !strcmp(info.namespace_name, "nvs.net80211")) {
      nvs_entry_next(&it);
      continue;
    }
    Serial.print(info.namespace_name);
    prefs.begin(info.namespace_name, true);
    Serial.print(" - ");
    Serial.print(info.key);
    Serial.print(" - ");
    Serial.print(info.type);
    Serial.print(" - ");
    switch (info.type) {
      case NVS_TYPE_U8:
        Serial.print(prefs.getUChar(info.key));
        break;
      case NVS_TYPE_I8:
        Serial.print(prefs.getChar(info.key));
        break;
      case NVS_TYPE_U16:
        Serial.print(prefs.getUShort(info.key));
        break;
      case NVS_TYPE_I16:
        Serial.print(prefs.getShort(info.key));
        break;
      case NVS_TYPE_U32:
        Serial.print(prefs.getUInt(info.key));
        break;
      case NVS_TYPE_I32:
        Serial.print(prefs.getInt(info.key));
        break;
      case NVS_TYPE_U64:
        Serial.print(prefs.getULong64(info.key));
        break;
      case NVS_TYPE_I64:
        Serial.print(prefs.getLong64(info.key));
        break;
      case NVS_TYPE_STR: {
        char strBuffer[256];
        size_t len = prefs.getString(info.key, strBuffer, sizeof(strBuffer));
        if (len > 0) {
          strBuffer[len] = '\0'; // Null-terminate the string
          Serial.print(strBuffer);
        } else {
          Serial.print("(empty)");
        }
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t blobLen = prefs.getBytesLength(info.key);
        if (blobLen > 0) {
          uint8_t *blobBuffer = (uint8_t *)heap_caps_malloc(blobLen, MALLOC_CAP_SPIRAM);
          if (blobBuffer) {
            prefs.getBytes(info.key, blobBuffer, blobLen);
            unsigned char* encodedBlob = (unsigned char*)heap_caps_malloc(blobLen * 2, MALLOC_CAP_SPIRAM);
            encode_base64((const unsigned char*)blobBuffer, blobLen, encodedBlob);
            Serial.print((char*)encodedBlob);
            free(blobBuffer);
            free(encodedBlob);
          } else {
            Serial.print("(blob allocation failed)");
          }
        } else {
          Serial.print("(empty blob)");
        }
        break;
      }
      default:
        Serial.print("(unknown type)");
        break;
    }
    Serial.println();
    prefs.end();
    nvs_entry_next(&it);
  }
  Serial.println("End of prefs dump.");
}

void clearPreferences() {
  nvs_flash_erase();
  nvs_flash_init();
  Serial.println("Preferences cleared.");
}

boolean backupAllPrefs() {
  Serial.println("Backing up prefs.");
  if (SD_MMC.exists(PREFS_BACKUP_FILE_PATH)) {
    SD_MMC.remove(PREFS_BACKUP_FILE_PATH);
  }
  nvs_iterator_t it;
  nvs_entry_find(NVS_DEFAULT_PART_NAME, NULL, NVS_TYPE_ANY, &it);
  if (it == NULL) {
    Serial.println("No entries found in NVS.");
    return false;
  }

  File file = SD_MMC.open(PREFS_BACKUP_FILE_PATH, FILE_WRITE, true);
  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    if (!strcmp(info.namespace_name, "dhcp_state") ||
        !strcmp(info.namespace_name, "phy") ||
        !strcmp(info.namespace_name, "nvs.net80211") ||
        !(strcmp(info.namespace_name, "system") && strcmp(info.key, "touch"))) {
      nvs_entry_next(&it);
      continue;
    }
    file.write((const uint8_t*)info.namespace_name, strlen(info.namespace_name));
    prefs.begin(info.namespace_name, true);
    file.write((const uint8_t*)"~", 1);
    file.write((const uint8_t*)info.key, strlen(info.key));
    file.write((const uint8_t*)"~", 1);
    file.write((const uint8_t*)String((uint32_t)info.type).c_str(), String((uint32_t)info.type).length());
    file.write((const uint8_t*)"~", 1);
    String value = "";
    switch (info.type) {
      case NVS_TYPE_U8:
        value = prefs.getUChar(info.key);
        break;
      case NVS_TYPE_I8:
        value = prefs.getChar(info.key);
        break;
      case NVS_TYPE_U16:
        value = prefs.getUShort(info.key);
        break;
      case NVS_TYPE_I16:
        value = prefs.getShort(info.key);
        break;
      case NVS_TYPE_U32:
        value = prefs.getUInt(info.key);
        break;
      case NVS_TYPE_I32:
        value = prefs.getInt(info.key);
        break;
      case NVS_TYPE_U64:
        value = prefs.getULong64(info.key);
        break;
      case NVS_TYPE_I64:
        value = prefs.getLong64(info.key);
        break;
      case NVS_TYPE_STR: {
        char* strBuffer = (char*)heap_caps_malloc(PREFS_BUFFER_LENGTH, MALLOC_CAP_SPIRAM);
        unsigned char* base64Buffer = (unsigned char*)heap_caps_malloc(PREFS_BUFFER_LENGTH * 2, MALLOC_CAP_SPIRAM);
        size_t len = prefs.getString(info.key, strBuffer, PREFS_BUFFER_LENGTH);
        strBuffer[len] = '\0'; // Null-terminate the string
        encode_base64((const unsigned char*)strBuffer, strlen(strBuffer), base64Buffer);
        value = (char*)base64Buffer;
        free(strBuffer);
        free(base64Buffer);
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t blobLen = prefs.getBytesLength(info.key);
        if (blobLen > 0) {
          uint8_t* blobBuffer = (uint8_t*)heap_caps_malloc(blobLen, MALLOC_CAP_SPIRAM);
          if (blobBuffer) {
            prefs.getBytes(info.key, blobBuffer, blobLen);
            unsigned char* base64Buffer = (unsigned char*)heap_caps_malloc(blobLen * 2, MALLOC_CAP_SPIRAM);
            encode_base64((const unsigned char*)blobBuffer, blobLen, base64Buffer);
            value = (char*)base64Buffer;
            free(blobBuffer);
            free(base64Buffer);
          } else {
            Serial.print("(blob allocation failed)");
          }
        } else {
          Serial.print("(empty blob)");
        }
        break;
      }
      default:
        Serial.print("(unknown type)");
        break;
    }
    file.write((uint8_t*)value.c_str(), value.length());
    file.write('\n');
    prefs.end();
    nvs_entry_next(&it);
  }
  file.close();
  Serial.println("End of prefs dump.");
  return true;
}

bool restorePrefsBackup() {
  Serial.println("Restoring prefs");
  if (!SD_MMC.exists(PREFS_BACKUP_FILE_PATH)) {
    Serial.printf("Error: Could not restore prefs backup: backup file %s not found!\n", PREFS_BACKUP_FILE_PATH);
    return false;
  }
  File file = SD_MMC.open(PREFS_BACKUP_FILE_PATH, FILE_READ);

  char* buffer = (char*)heap_caps_malloc(PREFS_BUFFER_LENGTH, MALLOC_CAP_SPIRAM);
  
  while (true) {
    int32_t readBytes = file.readBytesUntil('~', buffer, PREFS_BUFFER_LENGTH-1);
    if (readBytes <= 0) {
      break;
    }
    buffer[readBytes] = 0;
    String prefsNamespace = buffer;
    
    readBytes = file.readBytesUntil('~', buffer, PREFS_BUFFER_LENGTH-1);
    buffer[readBytes] = 0;
    String keyStr = buffer;
    const char* key = keyStr.c_str();
    
    readBytes = file.readBytesUntil('~', buffer, PREFS_BUFFER_LENGTH-1);
    buffer[readBytes] = 0;
    nvs_type_t type = (nvs_type_t)atoi(buffer);
    
    memset(buffer, 0, PREFS_BUFFER_LENGTH);
    size_t bufferSize = file.readBytesUntil('\n', buffer, PREFS_BUFFER_LENGTH-1);
    
    prefs.begin(prefsNamespace.c_str());
    Serial.print("'");
    Serial.print(prefsNamespace);
    Serial.print("' ~ '");
    Serial.print(key);
    Serial.print("' ~ '");
    switch (type) {
      case NVS_TYPE_U8:
        prefs.putUChar(key, (uint8_t)atoi(buffer));
        Serial.print(prefs.getUChar(key));
        break;
      case NVS_TYPE_I8:
        prefs.putChar(key, (int8_t)atoi(buffer));
        Serial.print(prefs.getChar(key));
        break;
      case NVS_TYPE_U16:
        prefs.putUShort(key, (uint16_t)atoi(buffer));
        Serial.print(prefs.getUShort(key));
        break;
      case NVS_TYPE_I16:
        prefs.putShort(key, (int16_t)atoi(buffer));
        Serial.print(prefs.getShort(key));
        break;
      case NVS_TYPE_U32:
        prefs.putUInt(key, (uint32_t)atoi(buffer));
        Serial.print(prefs.getUInt(key));
        break;
      case NVS_TYPE_I32:
        prefs.putInt(key, atoi(buffer));
        Serial.print(prefs.getInt(key));
        break;
      case NVS_TYPE_U64:
        prefs.putULong64(key, (uint64_t)atol(buffer));
        Serial.print(prefs.getULong(key));
        break;
      case NVS_TYPE_I64:
        prefs.putLong64(key, (int64_t)atol(buffer));
        Serial.print(prefs.getLong(key));
        break;
      case NVS_TYPE_STR: {
        char* strBuffer = (char*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
        int32_t strLen = decode_base64((unsigned char*)buffer, (unsigned char*)strBuffer);
        strBuffer[strLen] = 0;
        prefs.putString(key, String(strBuffer));
        free(strBuffer);
        Serial.print(prefs.getString(key));
        break;
      }
      case NVS_TYPE_BLOB: {
        if (bufferSize > 0) {
          uint8_t* blobBuffer = (uint8_t*)heap_caps_malloc(bufferSize, MALLOC_CAP_SPIRAM);
          if (blobBuffer) {
            int32_t bytesSize = decode_base64((unsigned char*) buffer, (unsigned char*) blobBuffer);
            blobBuffer[bytesSize] = 0;
            prefs.putBytes(key, blobBuffer, bytesSize);
            free(blobBuffer);
          } else {
            Serial.print("(blob allocation failed)");
          }
        } else {
          Serial.print("(empty blob)");
        }
        break;
      }
      default:
        Serial.print("(unknown type)");
        break;
    }
    Serial.println("'");
    prefs.end();
  }
  return true;
}
