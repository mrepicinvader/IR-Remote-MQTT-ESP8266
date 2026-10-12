#pragma once
#include <ArduinoOTA.h>
#include <string>
class OtaManager {
 public:
  void begin();
  void loop();
  const std::string& status() const { return status_; }
  uint8_t progress() const { return progress_; }
 private:
  bool started_=false;
  std::string status_="waiting_wifi";
  uint8_t progress_=0;
};
extern OtaManager otaManager;
