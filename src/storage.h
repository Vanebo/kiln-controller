#pragma once
#include <Arduino.h>
#include "app_state.h"

bool initStorage();
void loadSettingsFromStorage();
void saveSettingsToStorage();
void saveWifiCredentials(const String &ssid, const String &password, bool keepPasswordIfBlank = true);
void clearWifiCredentials();

void makeDefaultPrograms();
void loadPrograms();
bool savePrograms();

void loadRunLog();
void appendRunRecord(const String &programName, RunResult result, uint32_t elapsedSec);

void loadRecoverySnapshot();
void saveRecoverySnapshot(bool force = false);
void clearRecoverySnapshot();
void discardRecoveryToHistory();

void factoryResetStorage();
