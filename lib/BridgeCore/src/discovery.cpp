#include "discovery.h"
#include <algorithm>
#include <set>

namespace Bridge {
namespace {
struct SwitchCodes { const Button* on=nullptr; const Button* off=nullptr; bool toggle=false; };
struct Diagnostic { const char* id; const char* name; const char* key; const char* unit; const char* deviceClass; };
const Diagnostic diagnostics[] = {
  {"diag_rssi", "WiFi signal", "rssi", "dBm", "signal_strength"},
  {"diag_heap", "Free heap", "heap", "B", "data_size"},
  {"diag_uptime", "Uptime", "uptime_seconds", "s", "duration"},
  {"diag_wifi_reconnects", "WiFi reconnects", "wifi_reconnects", "", ""},
  {"diag_mqtt_reconnects", "MQTT reconnects", "mqtt_reconnects", "", ""}
};
SwitchCodes switchCodes(const Remote& remote) {
  SwitchCodes result;
  size_t on=0,off=0,toggle=0; const Button* toggleButton=nullptr;
  for (const auto& b : remote.buttons) {
    if (b.entityType=="on") { result.on=&b; ++on; }
    else if (b.entityType=="off") { result.off=&b; ++off; }
    else if (b.entityType=="toggle") { toggleButton=&b; ++toggle; }
  }
  if (on==1 && off==1) return result;
  result=SwitchCodes{};
  if (remote.optimisticSwitch && !on && !off && toggle==1) { result.on=toggleButton; result.off=toggleButton; result.toggle=true; }
  return result;
}
std::string command(const Remote& remote, const Button& button) {
  StaticJsonDocument<192> doc; doc["remote"]=remote.id; doc["button"]=button.id;
  std::string json; serializeJson(doc,json); return json;
}
void common(JsonDocument& doc, const DiscoveryContext& c) {
  doc["command_topic"]=c.baseTopic+"/send"; doc["availability_topic"]=c.baseTopic+"/status";
  doc["payload_available"]="online"; doc["payload_not_available"]="offline";
  auto device=doc.createNestedObject("device"); device["name"]="ESP IR Bridge";
  device["manufacturer"]="Custom"; device["model"]="ESP_IR_TR_WIFI / ESP-12F";
  device["sw_version"]=Config::VERSION;
  device.createNestedArray("identifiers").add("irbridge_"+c.deviceId);
  device["configuration_url"]="http://"+c.ip+"/";
}
}
std::string discoveryTopic(const DiscoveryContext& c, const char* component, const std::string& id) {
  return c.prefix+"/"+component+"/irbridge_"+c.deviceId+"/"+id+"/config";
}
std::vector<std::string> discoveryTopics(const Remotes& remotes, const DiscoveryContext& c) {
  std::vector<std::string> topics;
  for (const auto& r : remotes) {
    for (const auto& b : r.buttons) topics.push_back(discoveryTopic(c,"button",b.id));
    for (const auto& macro : r.macros) topics.push_back(discoveryTopic(c,"button",macro.id));
    if (switchCodes(r).on) topics.push_back(discoveryTopic(c,"switch",r.id));
  }
  for (const auto& diagnostic : diagnostics) topics.push_back(discoveryTopic(c,"sensor",diagnostic.id));
  return topics;
}
bool discoveryPayload(const Remotes& remotes, const DiscoveryContext& c, const std::string& topic, std::string& payload) {
  payload.clear(); DynamicJsonDocument doc(2048);
  for (const auto& r : remotes) {
    for (const auto& b : r.buttons) if (topic==discoveryTopic(c,"button",b.id)) {
      common(doc,c); doc["name"]=r.name+" "+b.name;
      doc["unique_id"]="irbridge_"+c.deviceId+"_button_"+b.id;
      doc["payload_press"]=command(r,b);
      if (doc.overflowed()) return false;
      serializeJson(doc,payload); return true;
    }
    for (const auto& macro : r.macros) if (topic==discoveryTopic(c,"button",macro.id)) {
      common(doc,c); doc["name"]=r.name+" "+macro.name;
      doc["unique_id"]="irbridge_"+c.deviceId+"_scene_"+macro.id;
      StaticJsonDocument<192> press; press["remote"]=r.id; press["macro"]=macro.id;
      std::string json; serializeJson(press,json); doc["payload_press"]=json;
      if (doc.overflowed()) return false;
      serializeJson(doc,payload); return true;
    }
    const auto codes=switchCodes(r);
    if (codes.on && topic==discoveryTopic(c,"switch",r.id)) {
      common(doc,c);
      doc["name"]=r.name+(codes.toggle?" Optimistic – real device state unknown":" Power ON/OFF");
      doc["unique_id"]="irbridge_"+c.deviceId+"_switch_"+r.id;
      doc["payload_on"]=command(r,*codes.on); doc["payload_off"]=command(r,*codes.off);
      // Both types know sent commands only; no measured device state is published.
      doc["optimistic"]=true;
      if (doc.overflowed()) return false;
      serializeJson(doc,payload); return true;
    }
  }
  for (const auto& diagnostic : diagnostics) if (topic==discoveryTopic(c,"sensor",diagnostic.id)) {
    common(doc,c); doc.remove("command_topic");
    doc["name"]=diagnostic.name; doc["unique_id"]="irbridge_"+c.deviceId+"_"+diagnostic.id;
    doc["state_topic"]=c.baseTopic+"/info";
    doc["value_template"]=std::string("{{ value_json.")+diagnostic.key+" }}";
    doc["entity_category"]="diagnostic"; doc["expire_after"]=90;
    if (*diagnostic.unit) doc["unit_of_measurement"]=diagnostic.unit;
    if (*diagnostic.deviceClass) doc["device_class"]=diagnostic.deviceClass;
    if (doc.overflowed()) return false;
    serializeJson(doc,payload); return true;
  }
  return false;
}
std::vector<std::string> mergeDiscoveryHistory(const std::vector<std::string>& history, const std::vector<std::string>& desired) {
  std::vector<std::string> result=history;
  for (const auto& topic : desired) if (std::find(result.begin(),result.end(),topic)==result.end()) result.push_back(topic);
  return result;
}
bool validDiscoveryTopic(const std::string& topic) {
  return !topic.empty() && topic.size()<=192 && topic.find_first_of("+# \r\n") == std::string::npos &&
      std::none_of(topic.begin(),topic.end(),[](unsigned char ch) { return ch<32 || ch==127; }) &&
      topic.size()>7 && topic.substr(topic.size()-7)=="/config" && topic.find("/irbridge_") != std::string::npos;
}
}
