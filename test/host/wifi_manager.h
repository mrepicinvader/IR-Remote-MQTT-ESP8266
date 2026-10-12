#pragma once
#include "ESP8266WiFi.h"
class WifiManager {
 public:
  bool connected() const { return online; }
  std::string deviceId() const { return "aabbccddeeff"; }
  std::string ip() const { return "127.0.0.1"; }
  uint32_t reconnectCount() const { return 0; }
  bool online=true;
};
extern WifiManager wifiManager;
