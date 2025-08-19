#ifndef __PREFSHANDLER_H__
#define __PREFSHANDLER_H__
#include <Arduino.h>
#include <Preferences.h>

extern Preferences prefs;

void printNamespaces();
void dumpNamespaceContents();
void setGamePrefsNamespace(String name);
void applyGamePrefsNamespace();
void clearPreferences();
void clearPreferencesExceptSystem();

#endif /* __PREFSHANDLER_H__ */