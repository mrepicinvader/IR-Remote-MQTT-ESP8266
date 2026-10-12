#pragma once
#include <ArduinoJson.h>
#include <string>

struct RuntimeConfig {
  std::string wifiSsid;
  std::string wifiPassword;
  std::string recoveryPassword;  // Empty selects Config::DEFAULT_RECOVERY_PASSWORD.
  bool apFallbackEnabled = true;
  std::string hostname;
  std::string mqttHost;
  uint16_t mqttPort = 1883;
  std::string mqttUser;
  std::string mqttPassword;
  std::string discoveryPrefix = "homeassistant";
  bool otaEnabled = true;
  std::string otaPassword;
  bool webAuth = false;
  std::string webUser = "admin";
  std::string webPassword;
};

namespace Bridge {
bool validRecoveryPassword(const std::string& password);
bool recoveryApRequired(bool wifiConfigured, bool databaseReady, bool fallbackEnabled, uint32_t disconnectedMs);
void configToJson(const RuntimeConfig& config, JsonObject object, bool passwords);
bool configFromJson(JsonObjectConst json, RuntimeConfig& config, std::string& error, bool requireWifi=false);
}
