#include "storage.h"
#include <Preferences.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

static Preferences prefs;

bool initStorage() {
  bool ok = LittleFS.begin(true);
  if (!ok) Serial.println("LittleFS mount failed");
  return ok;
}

void loadSettingsFromStorage() {
  prefs.begin("kiln", true);
  settings.kp = prefs.getFloat("kp", settings.kp);
  settings.ki = prefs.getFloat("ki", settings.ki);
  settings.kd = prefs.getFloat("kd", settings.kd);
  settings.tcOffset = prefs.getFloat("tco", settings.tcOffset);
  settings.maxTemp = prefs.getFloat("maxt", settings.maxTemp);
  settings.maxOutput = prefs.getFloat("maxo", settings.maxOutput);
  settings.maxOvershoot = prefs.getFloat("ovr", settings.maxOvershoot);
  settings.manualPowerMax = prefs.getFloat("mpmax", settings.manualPowerMax);
  settings.manualPowerTempLimit = prefs.getFloat("mptemp", settings.manualPowerTempLimit);
  settings.manualPowerTimeoutMin = prefs.getUShort("mptime", settings.manualPowerTimeoutMin);
  settings.hostname = prefs.getString("host", settings.hostname);
  wifiSsid = prefs.getString("ssid", "");
  wifiPassword = prefs.getString("pass", "");
  prefs.end();
}

void saveSettingsToStorage() {
  prefs.begin("kiln", false);
  prefs.putFloat("kp", settings.kp);
  prefs.putFloat("ki", settings.ki);
  prefs.putFloat("kd", settings.kd);
  prefs.putFloat("tco", settings.tcOffset);
  prefs.putFloat("maxt", settings.maxTemp);
  prefs.putFloat("maxo", settings.maxOutput);
  prefs.putFloat("ovr", settings.maxOvershoot);
  prefs.putFloat("mpmax", settings.manualPowerMax);
  prefs.putFloat("mptemp", settings.manualPowerTempLimit);
  prefs.putUShort("mptime", settings.manualPowerTimeoutMin);
  prefs.putString("host", settings.hostname);
  prefs.end();
}

void saveWifiCredentials(const String &ssid, const String &password, bool keepPasswordIfBlank) {
  prefs.begin("kiln", false);
  prefs.putString("ssid", ssid);
  if (!password.isEmpty() || !keepPasswordIfBlank) prefs.putString("pass", password);
  prefs.end();
  wifiSsid = ssid;
  if (!password.isEmpty() || !keepPasswordIfBlank) wifiPassword = password;
}

void clearWifiCredentials() {
  prefs.begin("kiln", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  wifiSsid = "";
  wifiPassword = "";
}

void makeDefaultPrograms() {
  programCount = 2;

  programs[0].name = "Demo 120C";
  programs[0].stepCount = 2;
  programs[0].steps[0] = Step(60.0f, 120.0f, 1, 100.0f);
  programs[0].steps[1] = Step(120.0f, 180.0f, 2, 100.0f);

  programs[1].name = "Test 200C";
  programs[1].stepCount = 2;
  programs[1].steps[0] = Step(100.0f, 100.0f, 0, 100.0f);
  programs[1].steps[1] = Step(200.0f, 150.0f, 5, 100.0f);
}

bool savePrograms() {
  DynamicJsonDocument doc(24576);
  doc["schema"] = 2;
  JsonArray arr = doc.createNestedArray("programs");

  for (size_t i = 0; i < programCount; i++) {
    JsonObject p = arr.createNestedObject();
    p["name"] = programs[i].name;
    JsonArray sa = p.createNestedArray("steps");

    for (int j = 0; j < programs[i].stepCount; j++) {
      JsonObject s = sa.createNestedObject();
      s["target"] = programs[i].steps[j].target;
      s["rate"] = programs[i].steps[j].rate;
      s["hold"] = programs[i].steps[j].holdMin;
      s["maxPower"] = programs[i].steps[j].maxPower;
    }
  }

  File f = LittleFS.open("/programs.tmp", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove("/programs.json");
  return LittleFS.rename("/programs.tmp", "/programs.json");
}

void loadPrograms() {
  if (!LittleFS.exists("/programs.json")) {
    makeDefaultPrograms();
    savePrograms();
    return;
  }

  File f = LittleFS.open("/programs.json", "r");
  if (!f) {
    makeDefaultPrograms();
    return;
  }

  DynamicJsonDocument doc(24576);
  if (deserializeJson(doc, f)) {
    f.close();
    makeDefaultPrograms();
    return;
  }
  f.close();

  programCount = 0;
  for (JsonObject p : doc["programs"].as<JsonArray>()) {
    if (programCount >= MAX_PROGRAMS) break;
    Program &dst = programs[programCount++];
    dst.name = p["name"] | "Program";
    dst.stepCount = 0;

    for (JsonObject s : p["steps"].as<JsonArray>()) {
      if (dst.stepCount >= MAX_STEPS) break;
      dst.steps[dst.stepCount++] = Step(
        (float)(s["target"] | 100.0f),
        (float)(s["rate"] | 100.0f),
        (uint16_t)(s["hold"] | 0),
        constrain((float)(s["maxPower"] | 100.0f), 0.0f, 100.0f)
      );
    }
  }

  if (programCount == 0) {
    makeDefaultPrograms();
    savePrograms();
  }
}

static bool saveRunLogFile() {
  DynamicJsonDocument doc(4096);
  JsonArray arr = doc.createNestedArray("runs");
  for (size_t i = 0; i < runLogCount; i++) {
    JsonObject o = arr.createNestedObject();
    o["program"] = runLog[i].programName;
    o["result"] = runLog[i].result;
    o["elapsed"] = runLog[i].elapsedSec;
  }
  File f = LittleFS.open("/runs.tmp", "w");
  if (!f) return false;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove("/runs.json");
  return LittleFS.rename("/runs.tmp", "/runs.json");
}

void loadRunLog() {
  runLogCount = 0;
  if (!LittleFS.exists("/runs.json")) return;
  File f = LittleFS.open("/runs.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, f)) {
    f.close();
    return;
  }
  f.close();
  for (JsonObject o : doc["runs"].as<JsonArray>()) {
    if (runLogCount >= RUN_LOG_SIZE) break;
    runLog[runLogCount].programName = String((const char *)(o["program"] | "Program"));
    runLog[runLogCount].result = String((const char *)(o["result"] | "Unknown"));
    runLog[runLogCount].elapsedSec = o["elapsed"] | 0UL;
    runLogCount++;
  }
}

void appendRunRecord(const String &programName, RunResult result, uint32_t elapsedSec) {
  if (programName.isEmpty()) return;
  if (runLogCount < RUN_LOG_SIZE) {
    for (size_t i = runLogCount; i > 0; i--) runLog[i] = runLog[i - 1];
    runLogCount++;
  } else {
    for (size_t i = RUN_LOG_SIZE - 1; i > 0; i--) runLog[i] = runLog[i - 1];
  }
  runLog[0].programName = programName;
  runLog[0].result = resultName(result);
  runLog[0].elapsedSec = elapsedSec;
  saveRunLogFile();
}

void loadRecoverySnapshot() {
  recovery = RecoverySnapshot();
  if (!LittleFS.exists("/recovery.json")) return;
  File f = LittleFS.open("/recovery.json", "r");
  if (!f) return;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, f)) {
    f.close();
    LittleFS.remove("/recovery.json");
    return;
  }
  f.close();

  if (!(doc["active"] | false)) return;
  recovery.available = true;
  recovery.programIndex = doc["programIndex"] | -1;
  recovery.programName = String((const char *)(doc["programName"] | ""));
  recovery.activeStep = doc["step"] | -1;
  recovery.phase = (doc["phase"] | 0) == 1 ? ProgramPhase::HOLD : ProgramPhase::RAMP;
  recovery.resumePhase = (doc["resumePhase"] | 0) == 1 ? ProgramPhase::HOLD : ProgramPhase::RAMP;
  recovery.paused = doc["paused"] | false;
  recovery.phaseStartTarget = doc["phaseStartTarget"] | 20.0f;
  recovery.pausedTarget = doc["pausedTarget"] | 20.0f;
  recovery.targetTemp = doc["target"] | 20.0f;
  recovery.phaseElapsedSec = doc["phaseElapsed"] | 0UL;
  recovery.runElapsedSec = doc["runElapsed"] | 0UL;
}

void saveRecoverySnapshot(bool force) {
  if (runMode != RunMode::PROGRAM || activeProgram < 0 || activeStep < 0) return;
  uint32_t now = millis();
  if (!force && now - lastRecoverySaveMs < RECOVERY_SAVE_INTERVAL_MS) return;
  lastRecoverySaveMs = now;

  uint32_t effectiveNow = programPaused ? pauseStartMs : now;
  uint32_t phaseElapsed = phaseStartMs ? (effectiveNow - phaseStartMs) / 1000UL : 0;

  DynamicJsonDocument doc(2048);
  doc["active"] = true;
  doc["programIndex"] = activeProgram;
  doc["programName"] = programs[activeProgram].name;
  doc["step"] = activeStep;
  doc["phase"] = programPhase == ProgramPhase::HOLD ? 1 : 0;
  doc["resumePhase"] = resumePhase == ProgramPhase::HOLD ? 1 : 0;
  doc["paused"] = programPaused;
  doc["phaseStartTarget"] = phaseStartTarget;
  doc["pausedTarget"] = pausedTarget;
  doc["target"] = targetTemp;
  doc["phaseElapsed"] = phaseElapsed;
  doc["runElapsed"] = elapsedRunSeconds();

  File f = LittleFS.open("/recovery.tmp", "w");
  if (!f) return;
  serializeJson(doc, f);
  f.close();
  LittleFS.remove("/recovery.json");
  LittleFS.rename("/recovery.tmp", "/recovery.json");
}

void clearRecoverySnapshot() {
  recovery = RecoverySnapshot();
  LittleFS.remove("/recovery.json");
  lastRecoverySaveMs = 0;
}

void discardRecoveryToHistory() {
  if (!recovery.available) return;
  appendRunRecord(recovery.programName, RunResult::INTERRUPTED, recovery.runElapsedSec);
  emitEvent("interrupted", recovery.programName + " was discarded after power loss");
  clearRecoverySnapshot();
}

void factoryResetStorage() {
  prefs.begin("kiln", false);
  prefs.clear();
  prefs.end();
  LittleFS.remove("/programs.json");
  LittleFS.remove("/runs.json");
  LittleFS.remove("/recovery.json");
}
