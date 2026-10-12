#include "discovery_manager.h"
#include "mqtt_manager.h"
#include "wifi_manager.h"
#include "storage.h"
#include "learning_manager.h"
#include "logging.h"
#include <algorithm>

DiscoveryManager discoveryManager;
namespace {
constexpr char HISTORY[]="/data/discovery.json";
constexpr size_t LIMIT=192;
class TextReader : public Bridge::Reader {
 public:
  explicit TextReader(const std::string& text): text_(text) {}
  int read() override { return offset_<text_.size()?static_cast<unsigned char>(text_[offset_++]):-1; }
 private: const std::string& text_; size_t offset_=0;
};
bool parseHistory(const std::string& text, std::vector<std::string>& result, std::string& error) {
  TextReader reader(text); Bridge::JsonStream stream(reader);
  if (!stream.expect('[')) { error="Invalid discovery ledger"; return false; }
  stream.whitespace(); result.clear();
  if (stream.peek()==']') stream.get();
  else while (true) {
    std::string value;
    if (!stream.value(value,256)) { error="Invalid discovery topic"; return false; }
    StaticJsonDocument<384> doc;
    if (deserializeJson(doc,value) || !doc.is<const char*>() || !Bridge::validDiscoveryTopic(doc.as<std::string>()) || result.size()>=LIMIT) {
      error="Invalid/full discovery ledger"; return false;
    }
    result.push_back(doc.as<std::string>()); stream.whitespace(); const int separator=stream.get();
    if (separator==']') break;
    if (separator!=',') { error="Invalid discovery ledger separator"; return false; }
  }
  stream.whitespace(); return stream.peek()==-1;
}
}
Bridge::DiscoveryContext DiscoveryManager::context() const {
  return {wifiManager.deviceId(),storage.config().discoveryPrefix,mqttManager.baseTopic(),wifiManager.ip()};
}
void DiscoveryManager::begin() {
  if (!storage.mounted()) { status_="LittleFS unavailable"; return; }
  LittleFileOps files; std::string text,error;
  if (files.exists(HISTORY) && (!files.read(HISTORY,text,40000) || !parseHistory(text,history_,error))) {
    status_="Invalid discovery ledger; publication paused"; LOGE("HA", "%s",status_.c_str()); dirty_=false; return;
  }
  storage.onChanged([] { discoveryManager.schedule(); });
  mqttManager.onConnected([] { discoveryManager.schedule(); });
  mqttManager.onBirth([] { discoveryManager.schedule(); });
}
bool DiscoveryManager::saveHistory(const std::vector<std::string>& history,std::string& error) {
  if (history.size()>LIMIT) { error="Discovery history limit (192) reached; export backup and maintain broker ledger"; return false; }
  std::string text="[";
  for (size_t i=0;i<history.size();++i) {
    if (i) text+=',';
    StaticJsonDocument<384> doc; doc.set(history[i]); serializeJson(doc,text);
  }
  text+=']'; LittleFileOps files;
  return Bridge::atomicWrite(files,HISTORY,text,[](const std::string& value,std::string& e) {
    std::vector<std::string> verified; return parseHistory(value,verified,e);
  },error);
}
void DiscoveryManager::loop() {
  if (!mqttManager.connected() || !storage.ready() || learningManager.session().active()) return;
  if (dirty_) {
    const auto desired=Bridge::discoveryTopics(storage.remotes(),context());
    auto merged=Bridge::mergeDiscoveryHistory(history_,desired); std::string error;
    // Persist all publication intents BEFORE publishing new configs. Deleted
    // topics stay in the ledger so QoS0/timing/power cuts cannot lose tombstones.
    if (merged!=history_ && !saveHistory(merged,error)) { status_=error; LOGE("HA", "%s",error.c_str()); dirty_=false; return; }
    history_=std::move(merged); cursor_=0; publishing_=true; dirty_=false; status_="publishing"; retry_.reset();
  }
  if (!publishing_ || !retry_.due(millis())) return;
  if (cursor_>=history_.size()) { publishing_=false; status_="synced"; LOGI("HA", "Discovery synchronized"); return; }
  const auto& topic=history_[cursor_]; std::string payload;
  if (!Bridge::discoveryPayload(storage.remotes(),context(),topic,payload)) {
    const auto desired=Bridge::discoveryTopics(storage.remotes(),context());
    if (std::find(desired.begin(),desired.end(),topic)!=desired.end()) {
      status_="Insufficient heap for discovery; retrying"; retry_.schedule(millis(),1000); return;
    }
  }
  // Empty retained payload removes obsolete HA discovery entities.
  if (!mqttManager.publishTopic(topic,payload,true)) { status_="retrying"; retry_.schedule(millis(),1000); return; }
  LOGI("HA", "%s %s",payload.empty()?"Removed entity":"Published discovery entity",topic.c_str());
  ++cursor_; retry_.schedule(millis(),75);
}
