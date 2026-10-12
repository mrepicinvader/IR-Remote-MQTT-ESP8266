#pragma once
#include <Arduino.h>
#define LOGI(tag, fmt, ...) Serial.printf("[%s][INFO] " fmt "\n", tag, ##__VA_ARGS__)
#define LOGW(tag, fmt, ...) Serial.printf("[%s][WARN] " fmt "\n", tag, ##__VA_ARGS__)
#define LOGE(tag, fmt, ...) Serial.printf("[%s][ERROR] " fmt "\n", tag, ##__VA_ARGS__)
