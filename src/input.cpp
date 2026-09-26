#include "input.h"
#include "app_config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static volatile int32_t encoderPosition = 0;
static volatile bool encoderPressed = false;
static int32_t lastConsumedPosition = 0;

static volatile bool stopPressed = false;

static void encoderTask(void *) {
  static const int8_t table[16] = {
     0,-1, 1, 0,
     1, 0, 0,-1,
    -1, 0, 0, 1,
     0, 1,-1, 0
  };

  uint8_t prev = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
  int acc = 0;

  bool prevBtn = digitalRead(ENC_BTN);
  bool stableBtn = prevBtn;
  uint32_t rawChangedMs = millis();

  bool prevStop = digitalRead(STOP_PIN);
  bool stableStopLocal = prevStop;
  uint32_t stopChangedMs = millis();

  while (true) {
    uint8_t cur = (digitalRead(ENC_A) << 1) | digitalRead(ENC_B);
    int8_t move = table[(prev << 2) | cur];
    prev = cur;
    acc += move;

    if (acc >= ENCODER_TRANSITIONS_PER_DETENT) {
      acc = 0;
      encoderPosition++;
    } else if (acc <= -ENCODER_TRANSITIONS_PER_DETENT) {
      acc = 0;
      encoderPosition--;
    }

    bool raw = digitalRead(ENC_BTN);
    if (raw != prevBtn) {
      prevBtn = raw;
      rawChangedMs = millis();
    }
    if (millis() - rawChangedMs >= 25 && raw != stableBtn) {
      stableBtn = raw;
      if (stableBtn == LOW) encoderPressed = true;
    }

    bool rawStop = digitalRead(STOP_PIN);
    if (rawStop != prevStop) {
      prevStop = rawStop;
      stopChangedMs = millis();
    }
    if (millis() - stopChangedMs >= 25 && rawStop != stableStopLocal) {
      stableStopLocal = rawStop;
      if (stableStopLocal == LOW) stopPressed = true;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void initInput() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(ENC_BTN, INPUT_PULLUP);
  pinMode(STOP_PIN, INPUT_PULLUP);
  xTaskCreatePinnedToCore(encoderTask, "InputTask", 3072, nullptr, 1, nullptr, 0);
}

int32_t consumeEncoderDelta() {
  int32_t p = encoderPosition;
  int32_t delta = p - lastConsumedPosition;
  lastConsumedPosition = p;
  return delta;
}

bool consumeEncoderPress() {
  if (!encoderPressed) return false;
  encoderPressed = false;
  return true;
}

bool consumeStopPress() {
  if (!stopPressed) return false;
  stopPressed = false;
  return true;
}
