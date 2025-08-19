#include "prefsHandler.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "base64.hpp"


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
  prefs.begin("touch");
  prefs.getBytes("calib", calibBytes, sizeof(calibBytes));
  prefs.clear();
  prefs.end();

  nvs_flash_erase();
  nvs_flash_init();
  
  // Reinitialize the preferences to keep the touch calibration
  prefs.begin("touch");
  prefs.putBytes("calib", calibBytes, sizeof(calibBytes));
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