#include "control.h"
#include "app_config.h"
#include "storage.h"
#include <Adafruit_MAX31855.h>
#include <math.h>

static Adafruit_MAX31855 thermocouple(TC_CLK, TC_CS, TC_DO);

static void writePhysicalOutput(float pct) {
  pct = constrain(pct, 0.0f, 100.0f);
  float physical = ENABLE_REAL_HEATER_OUTPUT ? pct : 0.0f;
  uint32_t duty = (uint32_t)roundf(physical * PWM_MAX / 100.0f);
  if (PWM_INVERTED) duty = PWM_MAX - duty;
  ledcWrite(PWM_CHANNEL, duty);
}

static void resetPid() {
  pidIntegral = 0.0f;
  pidLastError = 0.0f;
  pidLastMs = millis();
}

static bool sensorReadyForHeat() {
  if (SIMULATION_MODE) return true;
  return thermocoupleValid && currentTemp < settings.maxTemp;
}

static uint32_t currentProgramElapsedSec() {
  return elapsedRunSeconds();
}

static void resetRunStateToIdle() {
  runMode = RunMode::IDLE;
  activeProgram = -1;
  activeStep = -1;
  pendingProgram = -1;
  pendingDelayMin = 0;
  delayedStartAtMs = 0;
  programPaused = false;
  heaterPower = 0.0f;
  faultMessage = "";
  runStartMs = 0;
  resetPid();
  writePhysicalOutput(0);
}

static void finishProgram(RunResult result, const String &eventType) {
  String name = (activeProgram >= 0 && activeProgram < (int)programCount)
                  ? programs[activeProgram].name : String("Program");
  uint32_t elapsed = currentProgramElapsedSec();
  appendRunRecord(name, result, elapsed);
  clearRecoverySnapshot();
  emitEvent(eventType, name + " - " + resultName(result));
  resetRunStateToIdle();
}

static void advanceStep() {
  activeStep++;
  if (activeProgram < 0 || activeStep >= programs[activeProgram].stepCount) {
    finishProgram(RunResult::COMPLETED, "complete");
    return;
  }
  programPhase = ProgramPhase::RAMP;
  resumePhase = ProgramPhase::RAMP;
  phaseStartMs = millis();
  phaseStartTarget = targetTemp;
  saveRecoverySnapshot(true);
}

void initControlHardware() {
  pinMode(STOP_PIN, INPUT_PULLUP);
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_BITS);
  ledcAttachPin(HEATER_PWM_PIN, PWM_CHANNEL);
  writePhysicalOutput(0);
  pidLastMs = millis();
  simLastMs = millis();
}

bool startProgramNow(int index) {
  if (runMode != RunMode::IDLE) return false;
  if (recovery.available) return false;
  if (index < 0 || index >= (int)programCount) return false;
  if (!sensorReadyForHeat()) return false;

  activeProgram = index;
  activeStep = 0;
  runMode = RunMode::PROGRAM;
  programPhase = ProgramPhase::RAMP;
  resumePhase = ProgramPhase::RAMP;
  programPaused = false;
  phaseStartMs = millis();
  phaseStartTarget = currentTemp;
  targetTemp = currentTemp;
  runStartMs = millis();
  faultMessage = "";
  resetPid();
  clearRecoverySnapshot();
  saveRecoverySnapshot(true);
  emitEvent("start", programs[index].name + " started");
  return true;
}

bool scheduleDelayedProgram(int index, uint16_t delayMinutes) {
  if (runMode != RunMode::IDLE || recovery.available) return false;
  if (index < 0 || index >= (int)programCount) return false;
  if (delayMinutes == 0) return startProgramNow(index);
  delayMinutes = constrain((int)delayMinutes, (int)DELAY_INCREMENT_MIN, (int)MAX_DELAY_MIN);
  delayMinutes = (delayMinutes / DELAY_INCREMENT_MIN) * DELAY_INCREMENT_MIN;
  pendingProgram = index;
  pendingDelayMin = delayMinutes;
  delayedStartAtMs = millis() + (uint32_t)delayMinutes * 60000UL;
  runMode = RunMode::DELAYED_START;
  heaterPower = 0.0f;
  writePhysicalOutput(0);
  emitEvent("delay", programs[index].name + " scheduled in " + String(delayMinutes) + " min");
  return true;
}

void cancelDelayedStart() {
  if (runMode != RunMode::DELAYED_START) return;
  emitEvent("cancel", "Delayed start cancelled");
  resetRunStateToIdle();
}

bool startManualTemp(float temp) {
  if (runMode != RunMode::IDLE || recovery.available) return false;
  if (!sensorReadyForHeat()) return false;
  if (temp < 0 || temp > settings.maxTemp) return false;
  manualTarget = temp;
  targetTemp = temp;
  runMode = RunMode::MANUAL_TEMP;
  runStartMs = millis();
  faultMessage = "";
  resetPid();
  emitEvent("start", "Manual temperature started");
  return true;
}

bool startManualPower(float power) {
  if (runMode != RunMode::IDLE || recovery.available) return false;
  if (!sensorReadyForHeat()) return false;
  if (currentTemp >= settings.manualPowerTempLimit) return false;
  manualPowerRequest = constrain(power, 0.0f, settings.manualPowerMax);
  runMode = RunMode::MANUAL_POWER;
  manualPowerStartMs = millis();
  runStartMs = millis();
  faultMessage = "";
  resetPid();
  emitEvent("start", "Manual power started");
  return true;
}

bool pauseProgram() {
  if (runMode != RunMode::PROGRAM || programPaused) return false;
  programPaused = true;
  resumePhase = programPhase;
  pauseStartMs = millis();
  pausedTarget = currentTemp;
  targetTemp = pausedTarget;
  resetPid();
  saveRecoverySnapshot(true);
  emitEvent("pause", programs[activeProgram].name + " paused");
  return true;
}

bool resumeProgram() {
  if (runMode != RunMode::PROGRAM || !programPaused) return false;
  uint32_t pausedFor = millis() - pauseStartMs;
  phaseStartMs += pausedFor;
  programPhase = resumePhase;
  programPaused = false;
  resetPid();
  saveRecoverySnapshot(true);
  emitEvent("resume", programs[activeProgram].name + " resumed");
  return true;
}

void stopControlByUser() {
  if (runMode == RunMode::PROGRAM && activeProgram >= 0) {
    finishProgram(RunResult::ABORTED, "aborted");
    return;
  }
  if (runMode == RunMode::DELAYED_START) {
    cancelDelayedStart();
    return;
  }
  if (runMode == RunMode::MANUAL_TEMP || runMode == RunMode::MANUAL_POWER) {
    emitEvent("stopped", "Manual mode stopped");
  }
  resetRunStateToIdle();
}

void tripFault(const String &message) {
  if (runMode == RunMode::FAULT && faultMessage == message) return;

  if (runMode == RunMode::PROGRAM && activeProgram >= 0) {
    String name = programs[activeProgram].name;
    appendRunRecord(name, RunResult::FAULTED, currentProgramElapsedSec());
    clearRecoverySnapshot();
  }

  faultMessage = message;
  runMode = RunMode::FAULT;
  programPaused = false;
  heaterPower = 0.0f;
  writePhysicalOutput(0);
  emitEvent("fault", message);
}

bool clearFault() {
  if (runMode != RunMode::FAULT) return false;
  if (!sensorReadyForHeat()) return false;
  resetRunStateToIdle();
  emitEvent("fault-clear", "Fault cleared");
  return true;
}

bool resumeRecoveryProgram() {
  if (!recovery.available || runMode != RunMode::IDLE) return false;
  if (!sensorReadyForHeat()) return false;

  int index = recovery.programIndex;
  if (index < 0 || index >= (int)programCount || programs[index].name != recovery.programName) {
    index = -1;
    for (size_t i = 0; i < programCount; i++) {
      if (programs[i].name == recovery.programName) { index = (int)i; break; }
    }
  }
  if (index < 0 || recovery.activeStep < 0 || recovery.activeStep >= programs[index].stepCount) return false;

  activeProgram = index;
  activeStep = recovery.activeStep;
  programPhase = recovery.phase;
  resumePhase = recovery.resumePhase;
  programPaused = recovery.paused;
  phaseStartTarget = recovery.phaseStartTarget;
  pausedTarget = recovery.pausedTarget;
  targetTemp = recovery.targetTemp;
  uint32_t now = millis();
  phaseStartMs = now - recovery.phaseElapsedSec * 1000UL;
  runStartMs = now - recovery.runElapsedSec * 1000UL;
  pauseStartMs = programPaused ? now : 0;
  runMode = RunMode::PROGRAM;
  recovery.available = false;
  resetPid();
  saveRecoverySnapshot(true);
  emitEvent("resume", programs[index].name + " resumed after power loss");
  return true;
}

void updateTemperature() {
  if (SIMULATION_MODE) {
    uint32_t now = millis();
    float dt = simLastMs ? (now - simLastMs) / 1000.0f : 0.0f;
    simLastMs = now;
    const float ambient = 20.0f;
    float heat = heaterPower * 0.030f;
    float cool = (simulatedTemp - ambient) * 0.0025f;
    simulatedTemp += (heat - cool) * dt;
    rawTemp = simulatedTemp;
    currentTemp = rawTemp + settings.tcOffset;
    thermocoupleValid = true;
    return;
  }

  if (millis() - lastSensorMs < 500) return;
  lastSensorMs = millis();
  double c = thermocouple.readCelsius();
  if (isnan(c)) {
    thermocoupleValid = false;
    if (runMode != RunMode::IDLE && runMode != RunMode::DELAYED_START) tripFault("THERMOCOUPLE FAULT");
    return;
  }

  thermocoupleValid = true;
  rawTemp = (float)c;
  currentTemp = rawTemp + settings.tcOffset;
}

void updateRateOfRise() {
  uint32_t now = millis();
  if (now - lastRateSampleMs < RATE_SAMPLE_INTERVAL_MS) return;
  lastRateSampleMs = now;

  rateBuf[rateHead] = {now, currentTemp};
  rateHead = (rateHead + 1) % RATE_BUFFER_SIZE;
  if (rateCount < RATE_BUFFER_SIZE) rateCount++;

  if (rateCount < 20) {
    rateValid = false;
    return;
  }

  size_t oldest = (rateHead + RATE_BUFFER_SIZE - rateCount) % RATE_BUFFER_SIZE;
  RateSample &a = rateBuf[oldest];
  size_t newestIndex = (rateHead + RATE_BUFFER_SIZE - 1) % RATE_BUFFER_SIZE;
  RateSample &b = rateBuf[newestIndex];
  uint32_t dtMs = b.ms - a.ms;
  if (dtMs < 15000UL) {
    rateValid = false;
    return;
  }

  actualRateCPerHour = (b.temp - a.temp) * 3600000.0f / (float)dtMs;
  rateValid = true;
}

float requestedRampRate() {
  if (runMode != RunMode::PROGRAM || programPaused || programPhase != ProgramPhase::RAMP || activeProgram < 0 || activeStep < 0) return 0.0f;
  Step &s = programs[activeProgram].steps[activeStep];
  float direction = (s.target >= phaseStartTarget) ? 1.0f : -1.0f;
  return direction * fabsf(s.rate);
}

void updateLagDetection() {
  if (runMode != RunMode::PROGRAM || programPaused || programPhase != ProgramPhase::RAMP || !rateValid) {
    kilnLagging = false;
    lagCandidateSinceMs = 0;
    return;
  }

  float desired = requestedRampRate();
  if (fabsf(desired) < 1.0f) {
    kilnLagging = false;
    lagCandidateSinceMs = 0;
    return;
  }

  float sign = desired > 0 ? 1.0f : -1.0f;
  float progressRate = actualRateCPerHour * sign;
  float tempLag = (targetTemp - currentTemp) * sign;
  bool candidate = tempLag > LAG_TEMP_ERROR_C && progressRate < fabsf(desired) * LAG_RATE_RATIO;

  if (candidate) {
    if (!lagCandidateSinceMs) lagCandidateSinceMs = millis();
    if (millis() - lagCandidateSinceMs >= LAG_CONFIRM_MS) kilnLagging = true;
  } else {
    lagCandidateSinceMs = 0;
    kilnLagging = false;
  }
}

void updateProfileAndDelay() {
  if (runMode == RunMode::DELAYED_START) {
    heaterPower = 0.0f;
    writePhysicalOutput(0);
    if ((int32_t)(millis() - delayedStartAtMs) >= 0) {
      int index = pendingProgram;
      runMode = RunMode::IDLE;
      pendingProgram = -1;
      if (!sensorReadyForHeat()) {
        tripFault("DELAYED START BLOCKED");
      } else if (!startProgramNow(index)) {
        tripFault("DELAYED START FAILED");
      }
    }
    return;
  }

  if (runMode == RunMode::MANUAL_TEMP) {
    targetTemp = manualTarget;
    return;
  }
  if (runMode != RunMode::PROGRAM) return;

  if (programPaused) {
    targetTemp = pausedTarget;
    return;
  }

  Program &p = programs[activeProgram];
  if (activeStep < 0 || activeStep >= p.stepCount) {
    finishProgram(RunResult::COMPLETED, "complete");
    return;
  }

  Step &s = p.steps[activeStep];
  if (programPhase == ProgramPhase::RAMP) {
    float rate = max(1.0f, fabsf(s.rate));
    float maxMove = rate * ((millis() - phaseStartMs) / 3600000.0f);
    float diff = s.target - phaseStartTarget;

    if (fabsf(diff) <= maxMove) {
      targetTemp = s.target;
      if (s.holdMin > 0) {
        programPhase = ProgramPhase::HOLD;
        resumePhase = ProgramPhase::HOLD;
        phaseStartMs = millis();
        saveRecoverySnapshot(true);
      } else {
        advanceStep();
      }
    } else {
      targetTemp = phaseStartTarget + (diff > 0 ? maxMove : -maxMove);
    }
  } else {
    targetTemp = s.target;
    if (millis() - phaseStartMs >= (uint32_t)s.holdMin * 60000UL) advanceStep();
  }

  saveRecoverySnapshot(false);
}

float currentStepPowerLimit() {
  float limit = settings.maxOutput;
  if (runMode == RunMode::PROGRAM && activeProgram >= 0 && activeStep >= 0 && activeStep < programs[activeProgram].stepCount) {
    limit = min(limit, programs[activeProgram].steps[activeStep].maxPower);
  }
  if (runMode == RunMode::MANUAL_POWER) limit = min(limit, settings.manualPowerMax);
  return constrain(limit, 0.0f, 100.0f);
}

void updateControlOutput() {
  if (runMode == RunMode::IDLE || runMode == RunMode::FAULT || runMode == RunMode::DELAYED_START) {
    heaterPower = 0.0f;
    writePhysicalOutput(0);
    return;
  }

  if (runMode == RunMode::MANUAL_POWER) {
    heaterPower = min(manualPowerRequest, currentStepPowerLimit());
    writePhysicalOutput(heaterPower);
    return;
  }

  uint32_t now = millis();
  if (!pidLastMs) pidLastMs = now;
  if (now - pidLastMs < 250) return;

  float dt = (now - pidLastMs) / 1000.0f;
  pidLastMs = now;
  float error = targetTemp - currentTemp;
  float derivative = (error - pidLastError) / max(dt, 0.001f);
  float trialIntegral = pidIntegral + error * dt;
  float output = settings.kp * error + settings.ki * trialIntegral + settings.kd * derivative;
  float limit = currentStepPowerLimit();
  float clamped = constrain(output, 0.0f, limit);

  if (output == clamped || ((output > limit) && error < 0) || ((output < 0) && error > 0)) pidIntegral = trialIntegral;
  pidLastError = error;
  heaterPower = clamped;
  writePhysicalOutput(heaterPower);
}

void checkSafety() {
  // Physical STOP handling itself is debounced by the local UI module. This
  // second level is intentional: while heating, a continuously held LOW must
  // still force output off even if the UI task is delayed.
  if (digitalRead(STOP_PIN) == LOW && (runMode == RunMode::PROGRAM || runMode == RunMode::MANUAL_TEMP || runMode == RunMode::MANUAL_POWER || runMode == RunMode::DELAYED_START)) {
    stopControlByUser();
    return;
  }

  if (runMode == RunMode::IDLE || runMode == RunMode::FAULT || runMode == RunMode::DELAYED_START) return;

  if (!thermocoupleValid && !SIMULATION_MODE) {
    tripFault("THERMOCOUPLE FAULT");
    return;
  }

  if (currentTemp > settings.maxTemp) {
    tripFault("MAX TEMPERATURE");
    return;
  }

  if (runMode == RunMode::MANUAL_POWER) {
    if (currentTemp > settings.manualPowerTempLimit) {
      tripFault("MANUAL TEMP LIMIT");
      return;
    }
    uint32_t timeoutMs = (uint32_t)settings.manualPowerTimeoutMin * 60000UL;
    if (timeoutMs > 0 && millis() - manualPowerStartMs >= timeoutMs) {
      tripFault("MANUAL POWER TIMEOUT");
      return;
    }
  } else if (currentTemp > targetTemp + settings.maxOvershoot) {
    tripFault("TEMPERATURE OVERSHOOT");
  }
}

void addHistorySample() {
  if (millis() - lastHistoryMs < HISTORY_INTERVAL_MS) return;
  lastHistoryMs = millis();
  bool hasTarget = modeHasTarget();
  historyBuf[historyHead] = {(uint32_t)(millis() / 1000UL), currentTemp, targetTemp, heaterPower, hasTarget};
  historyHead = (historyHead + 1) % HISTORY_SIZE;
  if (historyCount < HISTORY_SIZE) historyCount++;
}

static float scheduledTargetForCurrentStep() {
  if (runMode != RunMode::PROGRAM || activeProgram < 0 || activeStep < 0) return targetTemp;
  Step &s = programs[activeProgram].steps[activeStep];
  if (programPhase == ProgramPhase::HOLD) return s.target;
  uint32_t effectiveNow = programPaused ? pauseStartMs : millis();
  float elapsedH = (effectiveNow - phaseStartMs) / 3600000.0f;
  float distance = fabsf(s.target - phaseStartTarget);
  float moved = min(distance, fabsf(s.rate) * elapsedH);
  float sign = s.target >= phaseStartTarget ? 1.0f : -1.0f;
  return phaseStartTarget + sign * moved;
}

int64_t delayedRemainingSeconds() {
  if (runMode != RunMode::DELAYED_START) return -1;
  int32_t ms = (int32_t)(delayedStartAtMs - millis());
  return ms <= 0 ? 0 : ms / 1000;
}

int64_t manualPowerRemainingSeconds() {
  if (runMode != RunMode::MANUAL_POWER) return -1;
  uint32_t total = (uint32_t)settings.manualPowerTimeoutMin * 60UL;
  uint32_t elapsed = (millis() - manualPowerStartMs) / 1000UL;
  return elapsed >= total ? 0 : (int64_t)(total - elapsed);
}

uint32_t nominalProgramDurationSeconds(int index, float startTemp) {
  if (index < 0 || index >= (int)programCount) return 0;
  const Program &p = programs[index];
  double seconds = 0.0;
  float temp = startTemp;
  for (int i = 0; i < p.stepCount; i++) {
    const Step &s = p.steps[i];
    float rate = max(1.0f, fabsf(s.rate));
    seconds += fabsf(s.target - temp) / rate * 3600.0;
    seconds += (double)s.holdMin * 60.0;
    temp = s.target;
  }
  if (seconds < 0.0) seconds = 0.0;
  if (seconds > 4294967295.0) seconds = 4294967295.0;
  return (uint32_t)llround(seconds);
}

int64_t estimatedRemainingSeconds() {
  if (runMode == RunMode::DELAYED_START) return delayedRemainingSeconds();
  if (runMode == RunMode::MANUAL_POWER) return manualPowerRemainingSeconds();
  if (runMode != RunMode::PROGRAM || activeProgram < 0 || activeStep < 0) return -1;

  Program &p = programs[activeProgram];
  Step &cur = p.steps[activeStep];
  double seconds = 0.0;

  if (programPhase == ProgramPhase::HOLD) {
    uint32_t effectiveNow = programPaused ? pauseStartMs : millis();
    uint32_t elapsedHold = (effectiveNow - phaseStartMs) / 1000UL;
    uint32_t totalHold = (uint32_t)cur.holdMin * 60UL;
    if (elapsedHold < totalHold) seconds += totalHold - elapsedHold;
  } else {
    float scheduled = scheduledTargetForCurrentStep();
    float requested = max(1.0f, fabsf(cur.rate));
    double plannedRamp = fabsf(cur.target - scheduled) / requested * 3600.0;
    double currentRamp = plannedRamp;

    if (rateValid) {
      float sign = cur.target >= currentTemp ? 1.0f : -1.0f;
      float progress = actualRateCPerHour * sign;
      if (progress > 1.0f) {
        double actualRamp = fabsf(cur.target - currentTemp) / progress * 3600.0;
        currentRamp = max(plannedRamp, actualRamp);
      }
    }
    seconds += currentRamp;
    seconds += (double)cur.holdMin * 60.0;
  }

  float prevTarget = cur.target;
  for (int i = activeStep + 1; i < p.stepCount; i++) {
    Step &s = p.steps[i];
    seconds += fabsf(s.target - prevTarget) / max(1.0f, fabsf(s.rate)) * 3600.0;
    seconds += (double)s.holdMin * 60.0;
    prevTarget = s.target;
  }

  if (seconds < 0) seconds = 0;
  return (int64_t)llround(seconds);
}

String etaConfidence() {
  if (runMode != RunMode::PROGRAM) return "--";
  if (programPaused) return "Paused";
  if (programPhase == ProgramPhase::HOLD) return "High";
  if (!rateValid) return "Estimating";
  if (kilnLagging) return "Low";
  float req = fabsf(requestedRampRate());
  if (req < 1.0f) return "High";
  float progress = actualRateCPerHour * (requestedRampRate() >= 0 ? 1.0f : -1.0f);
  if (progress >= req * 0.9f) return "High";
  return "Medium";
}
