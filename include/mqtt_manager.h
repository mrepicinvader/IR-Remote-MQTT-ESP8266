#pragma once
#include <PubSubClient.h>
#include <ESP8266WiFi.h>
#include "retry.h"
#include "bridge_core.h"

class MqttManager {
 public:
  MqttManager();
  void begin();
  void loop();
  bool connected() { return client_.connected(); }
  uint32_t reconnectCount() const { return reconnects_; }
  const std::string& baseTopic() const { return base_; }
  bool publish(const std::string& suffix, const std::string& payload, bool retained = false);
  bool publishTopic(const std::string& topic, const std::string& payload, bool retained = false);
  void publishReceived(const Bridge::Signal& signal);
  void publishLearnStatus();
  void stop();
  void onConnected(void (*handler)()) { connectedHandler_ = handler; }
  void onBirth(void (*handler)()) { birthHandler_ = handler; }
 private:
  bool connect();
  void message(char* topic, uint8_t* payload, unsigned int length);
  void info();
  WiFiClient socket_;
  PubSubClient client_;
  Bridge::RetryGate retry_;
  std::string base_;
  std::string publishedLearnStatus_;
  uint32_t reconnects_ = 0;
  uint32_t lastInfo_ = 0;
  bool wasConnected_ = false;
  bool everAttempted_ = false;
  void (*connectedHandler_)() = nullptr;
  void (*birthHandler_)() = nullptr;
};
extern MqttManager mqttManager;
