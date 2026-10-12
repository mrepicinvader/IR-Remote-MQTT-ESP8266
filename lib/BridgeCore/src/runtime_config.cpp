#include "runtime_config.h"
#include "model.h"
#include <cctype>

namespace Bridge {
bool validRecoveryPassword(const std::string& password) {
  if (password.size()<8 || password.size()>63) return false;
  for (unsigned char c : password) if (c<32 || c>126) return false;
  return true;
}
bool recoveryApRequired(bool wifiConfigured, bool databaseReady, bool fallbackEnabled, uint32_t disconnectedMs) {
  return !wifiConfigured || !databaseReady || (fallbackEnabled && disconnectedMs>=Config::WIFI_AP_FALLBACK_MS);
}
void configToJson(const RuntimeConfig& c, JsonObject o, bool passwords) {
  o["wifi_ssid"] = c.wifiSsid; o["hostname"] = c.hostname;
  o["mqtt_host"] = c.mqttHost; o["mqtt_port"] = c.mqttPort; o["mqtt_user"] = c.mqttUser;
  o["discovery_prefix"] = c.discoveryPrefix; o["ota_enabled"] = c.otaEnabled;
  o["web_auth"] = c.webAuth; o["web_user"] = c.webUser;
  o["ap_fallback_enabled"] = c.apFallbackEnabled;
  if (passwords) {
    o["wifi_password"] = c.wifiPassword; o["mqtt_password"] = c.mqttPassword;
    o["ota_password"] = c.otaPassword; o["web_password"] = c.webPassword;
    o["recovery_password"] = c.recoveryPassword;
  } else {
    o["wifi_password_set"] = !c.wifiPassword.empty(); o["mqtt_password_set"] = !c.mqttPassword.empty();
    o["ota_password_set"] = !c.otaPassword.empty(); o["web_password_set"] = !c.webPassword.empty();
    o["recovery_password_set"] = !c.recoveryPassword.empty();
  }
}
bool configFromJson(JsonObjectConst o, RuntimeConfig& result, std::string& error, bool requireWifi) {
  if (o.isNull()) { error = "Configuration must be an object"; return false; }
  for (const char* key : {"wifi_ssid","wifi_password","hostname","mqtt_host","mqtt_user","mqtt_password","discovery_prefix","ota_password","web_user","web_password","recovery_password"}) {
    if (o.containsKey(key) && !validJsonText(o[key])) { error="Configuration field must be a string without NUL"; return false; }
  }
  for (const char* key : {"ota_enabled","web_auth","ap_fallback_enabled"}) if (o.containsKey(key) && !o[key].is<bool>()) { error="Configuration field must be boolean"; return false; }
  if (o.containsKey("mqtt_port") && !o["mqtt_port"].is<int>()) { error="MQTT port must be integer"; return false; }
  RuntimeConfig c=result;
  auto text = [&](const char* key, std::string& field) { if (o.containsKey(key)) field = o[key].as<std::string>(); };
  text("wifi_ssid", c.wifiSsid); text("wifi_password", c.wifiPassword); text("hostname", c.hostname);
  text("mqtt_host", c.mqttHost); text("mqtt_user", c.mqttUser); text("mqtt_password", c.mqttPassword);
  text("discovery_prefix", c.discoveryPrefix); text("ota_password", c.otaPassword);
  text("web_user", c.webUser); text("web_password", c.webPassword);
  text("recovery_password", c.recoveryPassword); c.apFallbackEnabled=o["ap_fallback_enabled"] | c.apFallbackEnabled;
  if (!c.recoveryPassword.empty() && !validRecoveryPassword(c.recoveryPassword)) { error="Recovery password must contain 8..63 printable ASCII characters"; return false; }
  const int port = o["mqtt_port"] | static_cast<int>(c.mqttPort);
  c.otaEnabled = o["ota_enabled"] | c.otaEnabled; c.webAuth = o["web_auth"] | c.webAuth;
  if (port < 1 || port > 65535 || c.wifiSsid.size() > 32 || c.wifiPassword.size() > 64 ||
      (requireWifi && c.wifiSsid.empty()) || c.hostname.size() > 63 || c.mqttHost.size() > 128 ||
      c.mqttUser.size() > 64 || c.mqttPassword.size() > 128 || c.otaPassword.size() > 128 ||
      c.webUser.size() > 32 || c.webPassword.size() > 128 ||
      (c.webAuth && (c.webUser.empty() || c.webPassword.empty())) ||
      c.discoveryPrefix.empty() || c.discoveryPrefix.size() > 64 || c.discoveryPrefix.find_first_of("+# \r\n") != std::string::npos) {
    error = "Invalid configuration lengths, MQTT port or authentication"; return false;
  }
  if (!c.hostname.empty()) {
    if (c.hostname.front() == '-' || c.hostname.back() == '-') { error = "Invalid hostname"; return false; }
    for (unsigned char ch : c.hostname) if (!std::isalnum(ch) && ch != '-') { error = "Hostname permits letters, digits and hyphens"; return false; }
  }
  c.mqttPort = port; result=std::move(c); error.clear(); return true;
}
}
