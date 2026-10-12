#pragma once
#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include "retry.h"
#include <string>

class WifiManager {
 public:
  void begin();
  void loop();
  bool connected() const { return WiFi.status() == WL_CONNECTED; }
  bool apActive() const { return apActive_; }
  uint32_t reconnectCount() const { return reconnects_; }
  const std::string& deviceId() const { return deviceId_; }
  const std::string& hostname() const { return hostname_; }
  const std::string& apSsid() const { return apSsid_; }
  void requestRecovery();
  bool apProtected() const { return !apPassword_.empty(); }
  std::string ip() const { return (connected() ? WiFi.localIP() : WiFi.softAPIP()).toString().c_str(); }
 private:
  void startAP();
  void connectStation();
  void prepareRecoveryPassword();
  DNSServer dns_;
  Bridge::RetryGate retry_;
  std::string deviceId_, hostname_, apSsid_;
  std::string apPassword_;
  bool apActive_ = false, wasConnected_ = false;
  uint32_t disconnectedSince_ = 0, reconnects_ = 0;
};
extern WifiManager wifiManager;
