#pragma once
#include <Arduino.h>

// ============================================================
// BUILD / SAFETY SWITCHES
// ============================================================
static constexpr const char *FIRMWARE_VERSION = "0.4.1";

// Use the real MAX31855 by default.
static constexpr bool SIMULATION_MODE = false;

// IMPORTANT: leave false until the PWM -> 0-10 V chain has been verified.
static constexpr bool ENABLE_REAL_HEATER_OUTPUT = true;
static constexpr bool PWM_INVERTED = false;

// ============================================================
// PIN MAP
// ============================================================
// ILI9486, 8-bit parallel
static constexpr uint8_t TFT_D0 = 4;
static constexpr uint8_t TFT_D1 = 5;
static constexpr uint8_t TFT_D2 = 6;
static constexpr uint8_t TFT_D3 = 7;
static constexpr uint8_t TFT_D4 = 8;
static constexpr uint8_t TFT_D5 = 9;
static constexpr uint8_t TFT_D6 = 10;
static constexpr uint8_t TFT_D7 = 11;
static constexpr uint8_t TFT_RS = 12;
static constexpr uint8_t TFT_WR = 13;
static constexpr uint8_t TFT_RD = 14;
static constexpr uint8_t TFT_CS = 15;
static constexpr uint8_t TFT_RST = 16;

// MAX31855
static constexpr uint8_t TC_CLK = 17;
static constexpr uint8_t TC_DO  = 18;
static constexpr uint8_t TC_CS  = 21;

// Rotary encoder
static constexpr uint8_t ENC_A = 1;
static constexpr uint8_t ENC_B = 2;
static constexpr uint8_t ENC_BTN = 40;
static constexpr int ENCODER_TRANSITIONS_PER_DETENT = 4;

// Heater output / physical STOP
static constexpr uint8_t HEATER_PWM_PIN = 39;
static constexpr uint8_t STOP_PIN = 42; // active LOW

// ============================================================
// DISPLAY
// ============================================================
static constexpr int LCD_WIDTH = 480;
static constexpr int LCD_HEIGHT = 320;

static constexpr uint16_t BLACK    = 0x0000;
static constexpr uint16_t WHITE    = 0xFFFF;
static constexpr uint16_t CYAN     = 0x07FF;
static constexpr uint16_t BLUE     = 0x001F;
static constexpr uint16_t DARKGREY = 0x4208;
static constexpr uint16_t MIDGREY  = 0x8410;
static constexpr uint16_t GREEN    = 0x07E0;
static constexpr uint16_t YELLOW   = 0xFFE0;
static constexpr uint16_t RED      = 0xF800;

// ============================================================
// PWM
// ============================================================
static constexpr uint8_t PWM_CHANNEL = 0;
static constexpr uint16_t PWM_FREQ = 2000;
static constexpr uint8_t PWM_BITS = 12;
static constexpr uint16_t PWM_MAX = (1u << PWM_BITS) - 1;

// ============================================================
// LIMITS / TIMINGS
// ============================================================
static constexpr size_t MAX_PROGRAMS = 12;
static constexpr size_t MAX_STEPS = 12;
static constexpr size_t HISTORY_SIZE = 2048; // 30 sec/sample ~= 17 h
static constexpr size_t RUN_LOG_SIZE = 5;
static constexpr size_t RATE_BUFFER_SIZE = 75; // ~75 seconds at 1 Hz

static constexpr uint32_t HISTORY_INTERVAL_MS = 30000UL;
static constexpr uint32_t RATE_SAMPLE_INTERVAL_MS = 1000UL;
static constexpr uint32_t RECOVERY_SAVE_INTERVAL_MS = 60000UL;
static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 15000UL;
static constexpr uint32_t AP_TIMEOUT_MS = 10UL * 60UL * 1000UL;
static constexpr uint16_t DELAY_INCREMENT_MIN = 15;
static constexpr uint16_t MAX_DELAY_MIN = 48 * 60;

// Lag detection is intentionally conservative.
static constexpr float LAG_TEMP_ERROR_C = 15.0f;
static constexpr float LAG_RATE_RATIO = 0.75f;
static constexpr uint32_t LAG_CONFIRM_MS = 60000UL;
