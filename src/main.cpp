#include <Arduino.h>
#include "config.h"
#include "ir_manager.h"
#include "storage.h"
#include "learning_manager.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "mqtt_manager.h"
#include "discovery_manager.h"
#include "ota_manager.h"
#include "system_status.h"
#include "command_manager.h"

void setup() {
  Serial.begin(Config::SERIAL_BAUD);
  Serial.printf("\n[BOOT][INFO] ESP IR Bridge %s\n", Config::VERSION);
  storage.begin();
  wifiManager.begin();
  irManager.begin();
  irManager.onReceived([](const Bridge::Signal& signal) { learningManager.receive(signal); mqttManager.publishReceived(signal); });
  webServer.begin();
  mqttManager.begin();
  commandManager.begin();
  discoveryManager.begin();
  otaManager.begin();
  if (Config::USE_LED) {
    pinMode(Config::LED_PIN,OUTPUT);
    digitalWrite(Config::LED_PIN,Config::LED_ACTIVE_LOW?HIGH:LOW);
  }
}

void loop() {
  const uint64_t now=uptimeMillis();
  irManager.loop();
  learningManager.loop();
  wifiManager.loop();
  webServer.loop();
  mqttManager.loop();
  commandManager.loop();
  discoveryManager.loop();
  otaManager.loop();
  if (Config::USE_LED) {
    const bool lit=wifiManager.connected()?true:(now/500)%2;
    digitalWrite(Config::LED_PIN,lit!=Config::LED_ACTIVE_LOW?HIGH:LOW);
  }
  if (Serial.available()) {
    const char command = Serial.read();
    if (command == 'd') irManager.dumpLatest();
    if (command == 'a') wifiManager.requestRecovery();
    if (command == 'r' && !learningManager.session().active() && !commandManager.busy() && !commandManager.suspended()) {
      std::string error;
      if (!irManager.replayLatest(error)) Serial.printf("[IR-TX][ERROR] %s\n", error.c_str());
    }
  }
  delay(1);
}
