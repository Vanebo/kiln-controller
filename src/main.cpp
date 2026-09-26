#include <Arduino.h>
#include "app_config.h"
#include "app_state.h"
#include "storage.h"
#include "control.h"
#include "input.h"
#include "display_ui.h"
#include "network.h"
#include "web_server.h"

void setup() {
  Serial.begin(115200);
  delay(1200);

  Serial.println();
  Serial.println("========================================");
  Serial.print("Kiln Controller v");
  Serial.println(FIRMWARE_VERSION);
  Serial.println(SIMULATION_MODE ? "SIMULATION MODE" : "REAL SENSOR MODE");
  Serial.println(ENABLE_REAL_HEATER_OUTPUT ? "REAL HEATER OUTPUT ENABLED" : "REAL HEATER OUTPUT DISABLED");
  Serial.println("========================================");

  initStorage();
  loadSettingsFromStorage();
  loadPrograms();
  loadRunLog();
  loadRecoverySnapshot();

  if (recovery.available) {
    Serial.println(String("Interrupted firing found: ") + recovery.programName);
    Serial.println("Heater remains OFF until the user explicitly resumes or discards it.");
  }

  initControlHardware();
  initInput();
  initLocalUi();
  initNetwork();
  initWebServer();

  lastHistoryMs = millis() - HISTORY_INTERVAL_MS;
  lastRateSampleMs = millis() - RATE_SAMPLE_INTERVAL_MS;
}

void loop() {
  handleWebServer();
  updateNetwork();
  updateLocalUi();

  updateTemperature();
  updateRateOfRise();
  updateProfileAndDelay();
  updateControlOutput();
  checkSafety();
  updateLagDetection();
  addHistorySample();

  if (millis() - lastDisplayMs >= 500) {
    lastDisplayMs = millis();
    refreshMainDisplay(false);
  }

  delay(2);
}
