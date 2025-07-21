#include "prefsHandler.h"
#include "nvs_flash.h"
#include "nvs.h"


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

void clearPreferences() {
  nvs_flash_erase();
  nvs_flash_init();
  Serial.println("Preferences cleared.");
}