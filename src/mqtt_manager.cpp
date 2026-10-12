#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "storage.h"
#include "learning_manager.h"
#include "logging.h"
#include "system_status.h"
#include "command_manager.h"

MqttManager mqttManager;
MqttManager::MqttManager() : client_(socket_) {}
void MqttManager::begin() {
  base_ = "irbridge/" + wifiManager.deviceId();
  if (!client_.setBufferSize(Config::MQTT_BUFFER_SIZE)) LOGE("MQTT", "Packet buffer allocation failed");
  client_.setKeepAlive(20); client_.setSocketTimeout(2);
  socket_.setTimeout(2000);
  client_.setCallback([this](char* topic, uint8_t* bytes, unsigned int count) { message(topic,bytes,count); });
}
bool MqttManager::connect() {
  const auto& c=storage.config();
  client_.setServer(c.mqttHost.c_str(),c.mqttPort);
  const std::string clientId="irbridge_"+wifiManager.deviceId();
  const std::string status=base_+"/status";
  LOGI("MQTT", "Connecting to configured broker:%u", c.mqttPort);
  const bool ok=client_.connect(clientId.c_str(),c.mqttUser.empty()?nullptr:c.mqttUser.c_str(),
    c.mqttPassword.empty()?nullptr:c.mqttPassword.c_str(),status.c_str(),1,true,"offline");
  if (!ok) { LOGW("MQTT", "Connection failed, state=%d", client_.state()); return false; }
  if (!client_.subscribe((base_+"/send").c_str()) || !client_.subscribe((base_+"/learn").c_str()) ||
      !client_.subscribe((base_+"/command").c_str()) || !client_.subscribe("homeassistant/status")) {
    stop(); return false;
  }
  if (c.discoveryPrefix!="homeassistant") client_.subscribe((c.discoveryPrefix+"/status").c_str());
  publish("status","online",true); info(); publishedLearnStatus_.clear();
  LOGI("MQTT", "Connected; Last Will configured, subscribed to %s", base_.c_str());
  if (connectedHandler_) connectedHandler_();
  return true;
}
bool MqttManager::publishTopic(const std::string& topic, const std::string& payload, bool retained) {
  return client_.connected() && client_.publish(topic.c_str(),payload.c_str(),retained);
}
bool MqttManager::publish(const std::string& suffix, const std::string& payload, bool retained) {
  return publishTopic(base_+"/"+suffix,payload,retained);
}
void MqttManager::info() {
  StaticJsonDocument<768> doc;
  doc["firmware_version"]=Config::VERSION; doc["device_id"]=wifiManager.deviceId();
  doc["ip"]=wifiManager.ip(); doc["rssi"]=WiFi.RSSI(); doc["heap"]=ESP.getFreeHeap(); doc["uptime_ms"]=uptimeMillis();
  doc["uptime_seconds"]=uptimeMillis()/1000; doc["wifi_reconnects"]=wifiManager.reconnectCount(); doc["mqtt_reconnects"]=reconnects_;
  std::string json; serializeJson(doc,json); publish("info",json,true); lastInfo_=millis();
}
void MqttManager::publishReceived(const Bridge::Signal& signal) {
  if (!client_.connected()) return;
  DynamicJsonDocument doc(2048); Bridge::signalMetaToJson(signal,doc.to<JsonObject>());
  std::string error; doc["valid"]=Bridge::validSignal(signal,error); doc["error"]=error;
  std::string json; serializeJson(doc,json); publish("received",json,false);
  // RAW arrays are available via HTTP/LittleFS; do not overflow a bounded MQTT packet.
}
void MqttManager::publishLearnStatus() {
  if (!client_.connected()) return;
  DynamicJsonDocument doc(1024); learningManager.toJson(doc.to<JsonObject>());
  std::string json; serializeJson(doc,json);
  if (publish("learn/status",json,false)) publishedLearnStatus_=learningManager.session().status()+":"+std::to_string(learningManager.session().count());
}
void MqttManager::message(char* topic, uint8_t* bytes, unsigned int length) {
  const std::string incoming=topic;
  if (incoming=="homeassistant/status" || incoming==storage.config().discoveryPrefix+"/status") {
    if (std::string(reinterpret_cast<char*>(bytes),length)=="online" && birthHandler_) birthHandler_();
    return;
  }
  std::string error, requestId;
  bool success=false;
  bool queued=false;
  if (length>1024) error="MQTT command too large";
  else {
    DynamicJsonDocument doc(2048);
    if (deserializeJson(doc,bytes,length) || !doc.is<JsonObject>()) error="Invalid MQTT JSON";
    else {
    if (Bridge::validJsonText(doc["request_id"]) && Bridge::validRequestId(doc["request_id"].as<std::string>())) requestId=doc["request_id"].as<std::string>();
    if (!Bridge::validRequestTypes(doc.as<JsonObjectConst>(),{"action","remote","button","macro","request_id"},{},{},error)) { success=false; }
    else if (incoming==base_+"/send") { Bridge::CommandResult result; success=commandManager.enqueue(doc.as<JsonObjectConst>(),"mqtt",result,error); queued=success; }
    else if (incoming==base_+"/learn") {
      const std::string action=doc["action"] | "start";
      if (action=="cancel") { learningManager.cancel(); success=true; }
      else if (action=="status") success=true;
      else if (action=="start") {
        if (commandManager.busy() || commandManager.suspended()) error="Wait for queued sends or update to finish";
        else success=learningManager.start(doc.as<JsonObjectConst>(),error);
      }
      else error="Unknown learning action";
      publishLearnStatus();
    } else if (incoming==base_+"/command") {
      const std::string action=doc["action"] | "send";
      if (action=="send" || action=="macro") { Bridge::CommandResult result; success=commandManager.enqueue(doc.as<JsonObjectConst>(),"mqtt",result,error); queued=success; }
      else if (action=="learn") {
        if (commandManager.busy() || commandManager.suspended()) error="Wait for queued sends or update to finish";
        else success=learningManager.start(doc.as<JsonObjectConst>(),error);
      }
      else error="Unknown command action";
    } else error="Unknown MQTT command topic";
    }
  }
  if (queued) return;  // Queue listener publishes queued and terminal results.
  StaticJsonDocument<512> response; response["success"]=success;
  if (!success) {
    response["status"]="failed";
    if (!requestId.empty()) response["request_id"]=requestId;
  }
  if (!success) { response["error"]=error; LOGW("MQTT", "%s",error.c_str()); }
  std::string json; serializeJson(response,json); publish("command/result",json,false);
}
void MqttManager::loop() {
  const bool online=wifiManager.connected();
  if (!online) { if (wasConnected_) { socket_.stop(); wasConnected_=false; retry_.reset(); } return; }
  if (storage.config().mqttHost.empty()) return;
  const uint32_t now=millis();
  if (client_.connected()) {
    client_.loop();
    if (!client_.connected()) { wasConnected_=false; retry_.reset(); LOGW("MQTT", "Disconnected"); return; }
    wasConnected_=true;
    if (now-lastInfo_>=30000) info();
    const std::string state=learningManager.session().status()+":"+std::to_string(learningManager.session().count());
    if (state!=publishedLearnStatus_) publishLearnStatus();
  } else if (retry_.due(now) && !learningManager.session().active()) {
    if (everAttempted_) ++reconnects_;
    everAttempted_=true;
    retry_.schedule(now,Config::MQTT_RETRY_MS); wasConnected_=connect();
  }
}
void MqttManager::stop() { if (client_.connected()) { publish("status","offline",true); client_.disconnect(); } socket_.stop(); wasConnected_=false; }
