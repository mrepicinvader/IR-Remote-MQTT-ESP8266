#include "wifi_manager.h"
#include "storage.h"
#include "logging.h"

WifiManager wifiManager;
void WifiManager::begin() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);  // Explicit, observable retry schedule.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  String mac = WiFi.macAddress(); mac.replace(":", ""); mac.toLowerCase(); deviceId_ = mac.c_str();
  const auto suffix = deviceId_.substr(deviceId_.size() - 6);
  hostname_ = storage.config().hostname.empty() ? "irbridge-" + suffix : storage.config().hostname;
  String upper(suffix.c_str()); upper.toUpperCase(); apSsid_ = "IRBridge-" + std::string(upper.c_str());
  WiFi.hostname(hostname_.c_str());
  prepareRecoveryPassword();
  disconnectedSince_ = millis();
  if (storage.config().wifiSsid.empty() || !storage.ready()) startAP();
  if (!storage.config().wifiSsid.empty()) connectStation();
}
void WifiManager::prepareRecoveryPassword() {
  apPassword_ = storage.config().recoveryPassword;
  if (apPassword_.empty()) apPassword_ = Config::DEFAULT_RECOVERY_PASSWORD;
}
void WifiManager::requestRecovery() { startAP(); }
void WifiManager::connectStation() {
  WiFi.begin(storage.config().wifiSsid.c_str(), storage.config().wifiPassword.c_str());
  retry_.schedule(millis(), Config::WIFI_RETRY_MS);
  LOGI("WiFi", "Connecting to configured network (attempt %lu)", static_cast<unsigned long>(reconnects_ + 1));
}
void WifiManager::startAP() {
  if (apActive_) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0));
  if (!Bridge::validRecoveryPassword(apPassword_) || !WiFi.softAP(apSsid_.c_str(), apPassword_.c_str())) { LOGE("WiFi", "Cannot start protected configuration AP"); return; }
  apActive_ = true; dns_.start(53, "*", IPAddress(192,168,4,1));
  LOGI("WiFi", "Configuration AP %s: http://192.168.4.1", apSsid_.c_str());
}
void WifiManager::loop() {
  const uint32_t now = millis();
  const bool online = connected();
  if (online && !wasConnected_) {
    LOGI("WiFi", "Connected, IP=%s, RSSI=%d dBm", ip().c_str(), WiFi.RSSI());
    if (apActive_ && storage.ready() && !storage.config().wifiSsid.empty()) {
      dns_.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); apActive_ = false;
    }
  }
  if (!online && wasConnected_) { disconnectedSince_ = now; retry_.reset(); LOGW("WiFi", "Disconnected"); }
  wasConnected_ = online;
  if (!online) {
    if (!storage.config().wifiSsid.empty() && retry_.due(now)) { ++reconnects_; connectStation(); }
    if (Bridge::recoveryApRequired(!storage.config().wifiSsid.empty(), storage.ready(), storage.config().apFallbackEnabled, now-disconnectedSince_)) startAP();
  }
  if (apActive_) dns_.processNextRequest();
}
