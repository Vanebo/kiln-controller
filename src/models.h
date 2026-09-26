#pragma once
#include <Arduino.h>
#include "app_config.h"

struct Step {
  float target;
  float rate;      // degC/h, magnitude; direction follows target
  uint16_t holdMin;
  float maxPower;  // 0..100%, additionally limited by global max

  Step()
      : target(100.0f), rate(100.0f), holdMin(0), maxPower(100.0f) {}

  Step(float targetValue, float rateValue, uint16_t holdValue, float maxPowerValue = 100.0f)
      : target(targetValue), rate(rateValue), holdMin(holdValue), maxPower(maxPowerValue) {}
};

struct Program {
  String name;
  uint8_t stepCount = 0;
  Step steps[MAX_STEPS];
};

struct Settings {
  float kp = 4.0f;
  float ki = 0.12f;
  float kd = 8.0f;
  float tcOffset = 0.0f;
  float maxTemp = 1300.0f;
  float maxOutput = 100.0f;
  float maxOvershoot = 80.0f;

  float manualPowerMax = 100.0f;
  float manualPowerTempLimit = 1000.0f;
  uint16_t manualPowerTimeoutMin = 30;

  String hostname = "kiln-controller";
};

enum class RunMode {
  IDLE,
  DELAYED_START,
  PROGRAM,
  MANUAL_TEMP,
  MANUAL_POWER,
  FAULT
};

enum class ProgramPhase { RAMP, HOLD };

enum class RunResult { COMPLETED, ABORTED, FAULTED, INTERRUPTED };

enum class UiPage {
  MAIN,
  MENU,
  DETAILS,
  EDIT_MANUAL_TEMP,
  EDIT_MANUAL_POWER,
  PROGRAM_SELECT,
  PROGRAM_ACTION,
  DELAY_EDIT,
  RECOVERY_ACTION
};

struct HistorySample {
  uint32_t t;
  float temp;
  float target;
  float power;
  bool hasTarget;
};

struct RateSample {
  uint32_t ms;
  float temp;
};

struct RunRecord {
  String programName;
  String result;
  uint32_t elapsedSec = 0;
};

struct RecoverySnapshot {
  bool available = false;
  int programIndex = -1;
  String programName;
  int activeStep = -1;
  ProgramPhase phase = ProgramPhase::RAMP;
  ProgramPhase resumePhase = ProgramPhase::RAMP;
  bool paused = false;
  float phaseStartTarget = 20.0f;
  float pausedTarget = 20.0f;
  float targetTemp = 20.0f;
  uint32_t phaseElapsedSec = 0;
  uint32_t runElapsedSec = 0;
};
