#include "web_server.h"
#include "app_config.h"
#include "app_state.h"
#include "control.h"
#include "network.h"
#include "storage.h"
#include "web_ui.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <WiFi.h>

static WebServer server(80);
static bool otaAllowed = false;
static bool otaFailed = false;
static String otaError;

static void sendJson(DynamicJsonDocument &doc) {
  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
}

static bool parseBody(DynamicJsonDocument &doc) {
  DeserializationError e = deserializeJson(doc, server.arg("plain"));
  if (e) {
    server.send(400, "text/plain", "Invalid JSON");
    return false;
  }
  return true;
}

static String activeProgramName() {
  if (runMode == RunMode::PROGRAM && activeProgram >= 0) return programs[activeProgram].name;
  if (runMode == RunMode::DELAYED_START && pendingProgram >= 0) return programs[pendingProgram].name;
  return "";
}

void initWebServer() {
  server.on("/", HTTP_GET, [](){ server.send_P(200, "text/html", INDEX_HTML); });

  server.on("/api/status", HTTP_GET, [](){
    DynamicJsonDocument d(4096);
    d["temp"] = currentTemp;
    d["rawTemp"] = rawTemp;
    d["target"] = targetTemp;
    d["hasTarget"] = modeHasTarget();
    d["power"] = heaterPower;
    d["powerLimit"] = currentStepPowerLimit();
    d["state"] = stateName();
    d["programName"] = activeProgramName();
    d["stepInfo"] = stepInfo();
    d["fault"] = faultMessage;
    d["elapsed"] = (runMode == RunMode::IDLE || runMode == RunMode::FAULT || runMode == RunMode::DELAYED_START) ? -1 : (int64_t)elapsedRunSeconds();
    d["remaining"] = estimatedRemainingSeconds();
    d["actualRate"] = actualRateCPerHour;
    d["rateValid"] = rateValid;
    d["requestedRate"] = requestedRampRate();
    d["lagging"] = kilnLagging;
    d["etaConfidence"] = etaConfidence();
    d["paused"] = programPaused;
    d["simulation"] = SIMULATION_MODE;
    d["heaterEnabled"] = ENABLE_REAL_HEATER_OUTPUT;
    d["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    d["wifiStatus"] = wifiStatusText();
    d["ip"] = stationIp();
    d["apActive"] = setupApActive;
    d["eventCounter"] = eventCounter;
    d["eventType"] = lastEventType;
    d["eventMessage"] = lastEventMessage;
    d["recoveryAvailable"] = recovery.available;
    d["recoveryProgram"] = recovery.programName;
    d["recoveryElapsed"] = recovery.runElapsedSec;
    sendJson(d);
  });

  server.on("/api/history", HTTP_GET, [](){
    DynamicJsonDocument d(196608);
    JsonArray a = d.createNestedArray("samples");
    size_t start = (historyHead + HISTORY_SIZE - historyCount) % HISTORY_SIZE;
    for (size_t i = 0; i < historyCount; i++) {
      HistorySample &h = historyBuf[(start + i) % HISTORY_SIZE];
      JsonObject o = a.createNestedObject();
      o["t"] = h.t;
      o["temp"] = h.temp;
      if (h.hasTarget) o["target"] = h.target; else o["target"] = nullptr;
      o["power"] = h.power;
    }
    sendJson(d);
  });

  server.on("/api/runs", HTTP_GET, [](){
    DynamicJsonDocument d(4096);
    JsonArray a = d.createNestedArray("runs");
    for (size_t i = 0; i < runLogCount; i++) {
      JsonObject o = a.createNestedObject();
      o["program"] = runLog[i].programName;
      o["result"] = runLog[i].result;
      o["elapsed"] = runLog[i].elapsedSec;
    }
    sendJson(d);
  });

  server.on("/api/programs", HTTP_GET, [](){
    DynamicJsonDocument d(24576);
    JsonArray a = d.createNestedArray("programs");
    for (size_t i = 0; i < programCount; i++) {
      JsonObject p = a.createNestedObject();
      p["name"] = programs[i].name;
      p["durationSec"] = nominalProgramDurationSeconds((int)i, 20.0f);
      JsonArray sa = p.createNestedArray("steps");
      for (int j = 0; j < programs[i].stepCount; j++) {
        JsonObject s = sa.createNestedObject();
        s["target"] = programs[i].steps[j].target;
        s["rate"] = programs[i].steps[j].rate;
        s["hold"] = programs[i].steps[j].holdMin;
        s["maxPower"] = programs[i].steps[j].maxPower;
      }
    }
    sendJson(d);
  });

  server.on("/api/programs", HTTP_POST, [](){
    if (runMode != RunMode::IDLE || recovery.available) {
      server.send(409, "text/plain", "Resolve recovery and stop the kiln before editing programs");
      return;
    }
    DynamicJsonDocument d(24576);
    if (!parseBody(d)) return;
    JsonArray a = d["programs"].as<JsonArray>();
    size_t count = 0;
    for (JsonObject p : a) {
      if (count >= MAX_PROGRAMS) break;
      Program &dst = programs[count++];
      dst.name = String((const char *)(p["name"] | "Program"));
      dst.stepCount = 0;
      for (JsonObject s : p["steps"].as<JsonArray>()) {
        if (dst.stepCount >= MAX_STEPS) break;
        float target = s["target"] | 100.0f;
        float rate = s["rate"] | 100.0f;
        uint16_t hold = s["hold"] | 0;
        float maxPower = s["maxPower"] | 100.0f;
        if (target < 0 || target > settings.maxTemp || rate <= 0 || maxPower < 0 || maxPower > 100) {
          server.send(400, "text/plain", "Invalid step values");
          return;
        }
        dst.steps[dst.stepCount++] = Step(target, rate, hold, maxPower);
      }
      if (dst.stepCount == 0) {
        server.send(400, "text/plain", "Each program needs at least one step");
        return;
      }
    }
    if (count == 0) { server.send(400, "text/plain", "At least one program required"); return; }
    programCount = count;
    if (!savePrograms()) { server.send(500, "text/plain", "Failed to save programs"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/start", HTTP_POST, [](){
    DynamicJsonDocument d(512); if (!parseBody(d)) return;
    int index = d["index"] | -1;
    if (!startProgramNow(index)) { server.send(409, "text/plain", "Cannot start: controller not idle, sensor not ready, or recovery pending"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/delayed-start", HTTP_POST, [](){
    DynamicJsonDocument d(512); if (!parseBody(d)) return;
    int index = d["index"] | -1;
    int delayMin = d["delayMin"] | 60;
    delayMin = constrain((delayMin / 15) * 15, 15, (int)MAX_DELAY_MIN);
    if (!scheduleDelayedProgram(index, (uint16_t)delayMin)) { server.send(409, "text/plain", "Cannot schedule delayed start"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/manual-temp", HTTP_POST, [](){
    DynamicJsonDocument d(512); if (!parseBody(d)) return;
    float t = d["target"] | 0.0f;
    if (!startManualTemp(t)) { server.send(409, "text/plain", "Cannot start manual temperature mode"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/manual-power", HTTP_POST, [](){
    DynamicJsonDocument d(512); if (!parseBody(d)) return;
    float p = d["power"] | 0.0f;
    if (!startManualPower(p)) { server.send(409, "text/plain", "Cannot start manual power mode"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/pause", HTTP_POST, [](){
    if (!pauseProgram()) { server.send(409, "text/plain", "Program is not running or already paused"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/resume", HTTP_POST, [](){
    if (!resumeProgram()) { server.send(409, "text/plain", "Program is not paused"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/stop", HTTP_POST, [](){ stopControlByUser(); server.send(200, "text/plain", "ok"); });

  server.on("/api/recovery/resume", HTTP_POST, [](){
    if (!resumeRecoveryProgram()) { server.send(409, "text/plain", "Recovery cannot be resumed; check sensor/program state"); return; }
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/recovery/discard", HTTP_POST, [](){
    if (!recovery.available) { server.send(404, "text/plain", "No recovery pending"); return; }
    discardRecoveryToHistory();
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/settings", HTTP_GET, [](){
    DynamicJsonDocument d(4096);
    d["kp"] = settings.kp; d["ki"] = settings.ki; d["kd"] = settings.kd;
    d["tcOffset"] = settings.tcOffset; d["rawTemp"] = rawTemp;
    d["maxTemp"] = settings.maxTemp; d["maxOutput"] = settings.maxOutput; d["maxOvershoot"] = settings.maxOvershoot;
    d["manualPowerMax"] = settings.manualPowerMax; d["manualPowerTempLimit"] = settings.manualPowerTempLimit; d["manualPowerTimeoutMin"] = settings.manualPowerTimeoutMin;
    d["hostname"] = settings.hostname; d["ssid"] = wifiSsid; d["ip"] = stationIp(); d["rssi"] = wifiRssi(); d["wifiStatus"] = wifiStatusText();
    d["apActive"] = setupApActive; d["apIp"] = apIp(); d["firmware"] = FIRMWARE_VERSION; d["heaterEnabled"] = ENABLE_REAL_HEATER_OUTPUT;
    sendJson(d);
  });

  server.on("/api/settings", HTTP_POST, [](){
    if (runMode != RunMode::IDLE) { server.send(409, "text/plain", "Stop the kiln before changing settings"); return; }
    DynamicJsonDocument d(3072); if (!parseBody(d)) return;
    settings.kp = d["kp"] | settings.kp; settings.ki = d["ki"] | settings.ki; settings.kd = d["kd"] | settings.kd;
    settings.tcOffset = d["tcOffset"] | settings.tcOffset;
    settings.maxTemp = constrain((float)(d["maxTemp"] | settings.maxTemp), 100.0f, 1400.0f);
    settings.maxOutput = constrain((float)(d["maxOutput"] | settings.maxOutput), 0.0f, 100.0f);
    settings.maxOvershoot = constrain((float)(d["maxOvershoot"] | settings.maxOvershoot), 5.0f, 300.0f);
    settings.manualPowerMax = constrain((float)(d["manualPowerMax"] | settings.manualPowerMax), 0.0f, 100.0f);
    settings.manualPowerTempLimit = constrain((float)(d["manualPowerTempLimit"] | settings.manualPowerTempLimit), 50.0f, settings.maxTemp);
    settings.manualPowerTimeoutMin = constrain((int)(d["manualPowerTimeoutMin"] | settings.manualPowerTimeoutMin), 1, 720);
    if (d["hostname"].is<const char *>()) settings.hostname = String((const char *)d["hostname"]);
    saveSettingsToStorage();
    server.send(200, "text/plain", "ok");
  });

  server.on("/api/wifi", HTTP_POST, [](){
    if (runMode != RunMode::IDLE) { server.send(409, "text/plain", "Stop the kiln before changing Wi-Fi"); return; }
    DynamicJsonDocument d(1024); if (!parseBody(d)) return;
    String ssid = String((const char *)(d["ssid"] | ""));
    String pass = String((const char *)(d["password"] | ""));
    saveWifiCredentials(ssid, pass, true);
    server.send(200, "text/plain", "Saved. Restarting...");
    delay(300); ESP.restart();
  });

  server.on("/api/wifi/retry", HTTP_POST, [](){ requestWifiRetry(); server.send(200, "text/plain", "Retry requested"); });

  server.on("/api/ap/start", HTTP_POST, [](){
    if (runMode != RunMode::IDLE) { server.send(409, "text/plain", "Start the setup hotspot while idle"); return; }
    bool ok = startSetupAP(); server.send(ok ? 200 : 500, "text/plain", ok ? "Setup hotspot started" : "Failed to start hotspot");
  });
  server.on("/api/ap/stop", HTTP_POST, [](){ stopSetupAP(); server.send(200, "text/plain", "Setup hotspot stopped"); });

  server.on("/api/ota", HTTP_POST,
    [](){
      if (!otaAllowed) { server.send(409, "text/plain", "Firmware update is only allowed while idle"); return; }
      if (otaFailed || Update.hasError()) {
        String m = otaError.isEmpty() ? String("OTA failed, error ") + String(Update.getError()) : otaError;
        server.send(500, "text/plain", m); return;
      }
      server.send(200, "text/plain", "Firmware updated. Restarting..."); delay(500); ESP.restart();
    },
    [](){
      HTTPUpload &u = server.upload();
      if (u.status == UPLOAD_FILE_START) {
        otaAllowed = runMode == RunMode::IDLE;
        otaFailed = false; otaError = "";
        if (!otaAllowed) return;
        if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) { otaFailed = true; otaError = String("Update.begin error ") + Update.getError(); }
      } else if (u.status == UPLOAD_FILE_WRITE) {
        if (!otaAllowed || otaFailed) return;
        if (Update.write(u.buf, u.currentSize) != u.currentSize) { otaFailed = true; otaError = String("Update.write error ") + Update.getError(); }
      } else if (u.status == UPLOAD_FILE_END) {
        if (!otaAllowed || otaFailed) return;
        if (!Update.end(true)) { otaFailed = true; otaError = String("Update.end error ") + Update.getError(); }
      } else if (u.status == UPLOAD_FILE_ABORTED) {
        otaFailed = true; otaError = "Upload aborted";
      }
    }
  );

  server.on("/api/restart", HTTP_POST, [](){
    if (runMode != RunMode::IDLE) { server.send(409, "text/plain", "Stop the kiln before restarting"); return; }
    server.send(200, "text/plain", "Restarting..."); delay(300); ESP.restart();
  });

  server.on("/api/factory-reset", HTTP_POST, [](){
    if (runMode != RunMode::IDLE) { server.send(409, "text/plain", "Stop the kiln before factory reset"); return; }
    factoryResetStorage();
    server.send(200, "text/plain", "Factory reset complete. Restarting..."); delay(400); ESP.restart();
  });

  server.begin();
  Serial.println("Web server started");
}

void handleWebServer() {
  server.handleClient();
}
