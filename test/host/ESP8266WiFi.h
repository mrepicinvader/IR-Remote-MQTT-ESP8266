#pragma once
#include "Client.h"
// WiFiClient's Arduino interface backed by actual localhost TCP on the host.
class WiFiClient : public Client {
 public:
  WiFiClient()=default;
  ~WiFiClient() override { stop(); }
  int connect(const char*,uint16_t) override;
  int connect(IPAddress,uint16_t) override;
  int available() override;
  int read() override;
  int peek() override;
  void flush() override {}
  void stop() override;
  uint8_t connected() override;
  size_t write(uint8_t value) override { return write(&value,1); }
  size_t write(const uint8_t*,size_t) override;
  void setTimeout(uint32_t value) { timeout_=value; }
 private:
  intptr_t socket_=-1;
  uint32_t timeout_=2000;
};
class HostWiFi {
 public: int RSSI() const { return -50; }
};
extern HostWiFi WiFi;
