#pragma once
#include <Arduino.h>

void initInput();
int32_t consumeEncoderDelta();
bool consumeEncoderPress();
bool consumeStopPress();
