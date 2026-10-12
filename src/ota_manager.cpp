#include "ota_manager.h"
#include "storage.h"
#include "wifi_manager.h"
#include "ir_manager.h"
#include "learning_manager.h"
#include "mqtt_manager.h"
#include "logging.h"
#include "command_manager.h"

OtaManager otaManager;
void OtaManager::begin() { status_=storage.config().otaEnabled?"waiting_wifi":"disabled"; }
void OtaManager::loop() {
  if (commandManager.suspended()) return;
  if (!storage.config().otaEnabled) return;
  if (!started_ && wifiManager.connected()) {
    ArduinoOTA.setHostname(wifiManager.hostname().c_str());
    if (!storage.config().otaPassword.empty()) ArduinoOTA.setPassword(storage.config().otaPassword.c_str());
    ArduinoOTA.onStart([this] {
      status_="updating"; progress_=0;
      learningManager.cancel(); irManager.suspend();
      commandManager.suspend("Cancelled by OTA update");
      mqttManager.stop();
      if (ArduinoOTA.getCommand()==U_FS) LittleFS.end();
      LOGI("OTA", "Update started (%s)",ArduinoOTA.getCommand()==U_FLASH?"firmware":"filesystem");
    });
    ArduinoOTA.onProgress([this](unsigned int done,unsigned int total) {
      const auto next=total?static_cast<uint8_t>(static_cast<uint64_t>(done)*100/total):0;
      if (next/10!=progress_/10) LOGI("OTA", "Progress %u%%",next);
      progress_=next;
    });
    ArduinoOTA.onEnd([this] { status_="complete"; progress_=100; LOGI("OTA", "Update complete; restarting"); });
    ArduinoOTA.onError([this](ota_error_t error) {
      status_="error_"+std::to_string(static_cast<unsigned>(error));
      if (ArduinoOTA.getCommand()==U_FS) storage.begin();
      irManager.resume(); commandManager.resume(); LOGE("OTA", "Update error %u",error);
    });
    ArduinoOTA.begin(); started_=true; status_="ready";
    LOGI("OTA", "Ready on %s.local (password %s)",wifiManager.hostname().c_str(),storage.config().otaPassword.empty()?"not set":"configured");
  }
  if (started_ && wifiManager.connected()) ArduinoOTA.handle();
}
