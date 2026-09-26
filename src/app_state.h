#pragma once
#include <Arduino.h>
#include "models.h"

extern Settings settings;
extern Program programs[MAX_PROGRAMS];
extern size_t programCount;

extern RunMode runMode;
extern ProgramPhase programPhase;
extern ProgramPhase resumePhase;
extern bool programPaused;
extern int activeProgram;
extern int activeStep;
extern uint32_t phaseStartMs;
extern float phaseStartTarget;
extern uint32_t pauseStartMs;
extern float pausedTarget;
extern uint32_t runStartMs;

extern int pendingProgram;
extern uint16_t pendingDelayMin;
extern uint32_t delayedStartAtMs;

extern float manualTarget;
extern float manualPowerRequest;
extern uint32_t manualPowerStartMs;

extern float targetTemp;
extern float rawTemp;
extern float currentTemp;
extern float heaterPower;
extern bool thermocoupleValid;
extern String faultMessage;

extern float pidIntegral;
extern float pidLastError;
extern uint32_t pidLastMs;

extern float simulatedTemp;
extern uint32_t simLastMs;

extern HistorySample historyBuf[HISTORY_SIZE];
extern size_t historyHead;
extern size_t historyCount;
extern uint32_t lastHistoryMs;

extern RateSample rateBuf[RATE_BUFFER_SIZE];
extern size_t rateHead;
extern size_t rateCount;
extern uint32_t lastRateSampleMs;
extern float actualRateCPerHour;
extern bool rateValid;
extern bool kilnLagging;
extern uint32_t lagCandidateSinceMs;

extern RunRecord runLog[RUN_LOG_SIZE];
extern size_t runLogCount;
extern RecoverySnapshot recovery;
extern uint32_t lastRecoverySaveMs;

extern String wifiSsid;
extern String wifiPassword;
extern bool setupApActive;
extern uint32_t setupApStartedMs;
extern uint32_t lastWifiAttemptMs;
extern bool mdnsStarted;

extern uint32_t lastSensorMs;
extern uint32_t lastDisplayMs;
extern uint32_t lastStopRawChangeMs;
extern bool lastStopRaw;
extern bool stableStop;

extern uint32_t eventCounter;
extern String lastEventType;
extern String lastEventMessage;

String stateName();
bool modeHasTarget();
String stepInfo();
String resultName(RunResult result);
uint32_t elapsedRunSeconds();
void emitEvent(const String &type, const String &message);
