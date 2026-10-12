#pragma once
#include <cstdint>
#include <cstddef>

namespace Config {
static constexpr char VERSION[] = "0.2.0";
static constexpr char FIRMWARE_TARGET[] = "IRBRIDGE:ESP8266:4M1M:v1";
static constexpr uint8_t IR_TX_PIN = 4;
static constexpr uint8_t IR_RX_PIN = 14;
static constexpr uint8_t LED_PIN = 2;
static constexpr bool USE_LED = false;  // Board revision/LED wiring must be checked.
static constexpr bool LED_ACTIVE_LOW = true;
static constexpr bool IR_TX_INVERTED = false;
static constexpr uint32_t SERIAL_BAUD = 115200;
static constexpr uint16_t RX_BUFFER_SIZE = 4096;
static constexpr uint8_t RX_TIMEOUT_MS = 90;
static constexpr uint32_t DEFAULT_FREQUENCY_HZ = 38000;
static constexpr uint32_t TX_IGNORE_MS = 350;
static constexpr uint32_t LEARN_TIMEOUT_MS = 30000;
static constexpr uint32_t DISTINCT_PRESS_MS = 400;
static constexpr uint16_t MIN_RAW_SAMPLES = 6;
static constexpr uint32_t MAX_SIGNAL_US = 2000000;
static constexpr uint32_t REPEAT_GAP_US = 40000;
static constexpr size_t MAX_RAW_SAMPLES = 8192;  // Includes splits of >65535 us.
static constexpr size_t MAX_REMOTES = 16;
static constexpr size_t MAX_BUTTONS = 64;
static constexpr size_t MAX_MACROS = 12;
static constexpr size_t MAX_MACRO_STEPS = 16;
static constexpr uint32_t MAX_STEP_DELAY_MS = 60000;
static constexpr uint32_t MAX_MACRO_DELAY_MS = 300000;
static constexpr size_t MAX_QUEUED_COMMANDS = 6;
static constexpr size_t COMMAND_HISTORY_SIZE = 8;
static constexpr size_t INDEX_JSON_CAPACITY = 18000;
static constexpr size_t MAX_BACKUP_BYTES = 900000;
static constexpr uint32_t WIFI_RETRY_MS = 15000;
static constexpr uint32_t WIFI_AP_FALLBACK_MS = 45000;
static constexpr char DEFAULT_RECOVERY_PASSWORD[] = "05102001";
static constexpr uint32_t MQTT_RETRY_MS = 5000;
static constexpr uint16_t MQTT_BUFFER_SIZE = 2048;
}
