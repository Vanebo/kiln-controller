#include "app_state.h"

Settings settings;
Program programs[MAX_PROGRAMS];
size_t programCount = 0;

RunMode runMode = RunMode::IDLE;
ProgramPhase programPhase = ProgramPhase::RAMP;
ProgramPhase resumePhase = ProgramPhase::RAMP;
bool programPaused = false;
int activeProgram = -1;
int activeStep = -1;
uint32_t phaseStartMs = 0;
float phaseStartTarget = 20.0f;
uint32_t pauseStartMs = 0;
float pausedTarget = 20.0f;
uint32_t runStartMs = 0;

int pendingProgram = -1;
uint16_t pendingDelayMin = 0;
uint32_t delayedStartAtMs = 0;

float manualTarget = 100.0f;
float manualPowerRequest = 25.0f;
uint32_t manualPowerStartMs = 0;

float targetTemp = 20.0f;
float rawTemp = 20.0f;
float currentTemp = 20.0f;
float heaterPower = 0.0f;
bool thermocoupleValid = false;
String faultMessage;

float pidIntegral = 0.0f;
float pidLastError = 0.0f;
uint32_t pidLastMs = 0;

float simulatedTemp = 20.0f;
uint32_t simLastMs = 0;

HistorySample historyBuf[HISTORY_SIZE];
size_t historyHead = 0;
size_t historyCount = 0;
uint32_t lastHistoryMs = 0;

RateSample rateBuf[RATE_BUFFER_SIZE];
size_t rateHead = 0;
size_t rateCount = 0;
uint32_t lastRateSampleMs = 0;
float actualRateCPerHour = 0.0f;
bool rateValid = false;
bool kilnLagging = false;
uint32_t lagCandidateSinceMs = 0;

RunRecord runLog[RUN_LOG_SIZE];
size_t runLogCount = 0;
RecoverySnapshot recovery;
uint32_t lastRecoverySaveMs = 0;

String wifiSsid;
String wifiPassword;
bool setupApActive = false;
uint32_t setupApStartedMs = 0;
uint32_t lastWifiAttemptMs = 0;
bool mdnsStarted = false;

uint32_t lastSensorMs = 0;
uint32_t lastDisplayMs = 0;
uint32_t lastStopRawChangeMs = 0;
bool lastStopRaw = HIGH;
bool stableStop = HIGH;

uint32_t eventCounter = 0;
String lastEventType;
String lastEventMessage;

String stateName() {
  if (runMode == RunMode::FAULT) return "FAULT";
  if (runMode == RunMode::DELAYED_START) return "DELAYED";
  if (runMode == RunMode::MANUAL_TEMP) return "MANUAL TEMP";
  if (runMode == RunMode::MANUAL_POWER) return "MANUAL POWER";
  if (runMode == RunMode::PROGRAM) {
    if (programPaused) return "PAUSED";
    return programPhase == ProgramPhase::HOLD ? "HOLD" : "RUNNING";
  }
  return "IDLE";
}

bool modeHasTarget() {
  return runMode == RunMode::PROGRAM || runMode == RunMode::MANUAL_TEMP;
}

String stepInfo() {
  if (runMode != RunMode::PROGRAM || activeProgram < 0 || activeStep < 0) return "";
  return String("STEP ") + String(activeStep + 1) + "/" + String(programs[activeProgram].stepCount);
}

String resultName(RunResult result) {
  switch (result) {
    case RunResult::COMPLETED: return "Completed";
    case RunResult::ABORTED: return "Aborted";
    case RunResult::FAULTED: return "Fault";
    case RunResult::INTERRUPTED: return "Interrupted";
  }
  return "Unknown";
}

uint32_t elapsedRunSeconds() {
  if (!runStartMs) return 0;
  return (millis() - runStartMs) / 1000UL;
}

void emitEvent(const String &type, const String &message) {
  eventCounter++;
  lastEventType = type;
  lastEventMessage = message;
  Serial.println(String("EVENT ") + type + ": " + message);
}
