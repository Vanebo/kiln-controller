#pragma once
#include <Arduino.h>
#include "app_state.h"

void initControlHardware();
void updateTemperature();
void updateRateOfRise();
void updateProfileAndDelay();
void updateControlOutput();
void checkSafety();
void addHistorySample();
void updateLagDetection();

bool startProgramNow(int index);
bool scheduleDelayedProgram(int index, uint16_t delayMinutes);
void cancelDelayedStart();
bool startManualTemp(float temp);
bool startManualPower(float power);
bool pauseProgram();
bool resumeProgram();
void stopControlByUser();
void tripFault(const String &message);
bool clearFault();
bool resumeRecoveryProgram();

float currentStepPowerLimit();
float requestedRampRate();
int64_t estimatedRemainingSeconds();
int64_t delayedRemainingSeconds();
int64_t manualPowerRemainingSeconds();
uint32_t nominalProgramDurationSeconds(int index, float startTemp = 20.0f);
String etaConfidence();
