#include "model.h"
#include "atomic_store.h"
#include "learning.h"
#include "discovery.h"
#include "backup.h"
#include "retry.h"
#include "runtime_config.h"
#include "command_queue.h"
#include "firmware_check.h"
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace Bridge;
namespace {
void check(bool truth, const char* message) { if (!truth) throw std::runtime_error(message); }
class StringReader : public Reader {
 public:
  explicit StringReader(const std::string& text) : text_(text) {}
  int read() override { return pos_ < text_.size() ? static_cast<unsigned char>(text_[pos_++]) : -1; }
 private:
  const std::string& text_; size_t pos_ = 0;
};
class StringWriter : public Writer {
 public:
  std::string text;
  bool write(const char* bytes, size_t length) override { text.append(bytes, length); return true; }
};
class BudgetReader : public StringReader {
 public:
  BudgetReader(const std::string& text,size_t budget):StringReader(text),budget_(budget) {}
  bool canAllocate(size_t bytes) const override { return bytes<=budget_; }
 private: size_t budget_;
};
class MemoryFiles : public FileOps {
 public:
  std::map<std::string, std::string> files;
  int fault = 0;
  bool read(const std::string& path, std::string& text, size_t max) override {
    if (!exists(path) || files[path].size() > max) return false;
    text = files[path]; if (fault == 2) text += "corrupt"; return true;
  }
  bool write(const std::string& path, const std::string& text) override {
    files[path] = fault == 1 ? text.substr(0, text.size() / 2) : text;
    return fault != 1;
  }
  bool replace(const std::string& from, const std::string& to) override {
    if (fault == 3) return false;
    files[to] = files[from]; files.erase(from); return fault != 4;
  }
  bool exists(const std::string& path) override { return files.count(path); }
  bool remove(const std::string& path) override { return files.erase(path); }
};
Signal nec() {
  Signal s; s.protocol = "NEC"; s.bits = 32; s.value = 0x20DF10EFULL;
  s.address = 0x04; s.command = 0x08; s.decodedReliable = true;
  s.frequencySource = "protocol_default"; s.raw = {9000,4500};
  for (int bit = 31; bit >= 0; --bit) { s.raw.push_back(560); s.raw.push_back(s.value & (1ULL << bit) ? 1690 : 560); }
  s.raw.push_back(560); return s;
}
Signal raw() { Signal s; s.raw = {950,450,300,650,300,350,950}; return s; }
Button button(const char* id, const char* name, const char* file) { Button b; b.id=id; b.name=name; b.file=file; return b; }
Remotes fixture() {
  Remote fan; fan.id = "00000001"; fan.name = "Ventilator";
  fan.buttons.push_back(button("00000002", "Power", "/data/codes/c00000010.json"));
  fan.buttons.push_back(button("00000003", "Speed +", "/data/codes/c00000011.json"));
  Remote tv; tv.id = "00000004"; tv.name = "Fernseher";
  tv.buttons.push_back(button("00000005", "Power", "/data/codes/c00000012.json"));
  return {fan,tv};
}
void roundTrip(const Signal& source) {
  std::string error; StringWriter writer;
  check(writeSignal(source, writer, error), error.c_str());
  StringReader reader(writer.text); Signal result;
  check(readSignal(reader, result, error), error.c_str());
  check(source.raw == result.raw && source.protocol == result.protocol && source.bits == result.bits &&
        source.value == result.value && source.state == result.state && source.address == result.address &&
        source.command == result.command && source.frequency == result.frequency && source.repeat == result.repeat,
        "Signal roundtrip differs");
}
}
int main() {
  int failures = 0, total = 0;
  auto test = [&](const char* name, const std::function<void()>& fn) {
    ++total;
    try { fn(); std::cout << "PASS " << name << '\n'; }
    catch (const std::exception& e) { ++failures; std::cout << "FAIL " << name << ": " << e.what() << '\n'; }
  };
  test("JSON serialization/deserialization: NEC, RAW, full-state and 64-bit hex", [] {
    roundTrip(nec()); roundTrip(raw());
    Signal full = raw(); full.protocol="DAIKIN"; full.bits=280; full.state.resize(35, 0xAB); roundTrip(full);
    Signal wide=nec(); wide.protocol="SAMSUNG"; wide.bits=64; wide.value=0xFEDCBA9876543210ULL; roundTrip(wide);
  });
  test("Empty database load", [] {
    DynamicJsonDocument doc(1024); indexToJson({}, doc); Remotes result; std::string error;
    check(indexFromJson(doc.as<JsonVariantConst>(), result, error), error.c_str()); check(result.empty(), "Not empty");
  });
  test("Public command field types reject fallback-driven unintended actions", [] {
    std::string error;
    for (const char* text : {"{\"action\":false}","{\"button\":{}}","{\"repeat_count\":1.5}","{\"repeat_count\":-1}","{\"optimistic_switch\":\"true\"}",R"({"action":"send\u0000extra"})"}) {
      DynamicJsonDocument doc(512); deserializeJson(doc,text);
      check(!validRequestTypes(doc.as<JsonObjectConst>(),{"action","button"},{"repeat_count"},{"optimistic_switch"},error),"Invalid command field accepted");
    }
    DynamicJsonDocument valid(512); deserializeJson(valid,"{\"action\":\"send\",\"button\":\"Power\",\"repeat_count\":1,\"optimistic_switch\":false}");
    check(validRequestTypes(valid.as<JsonObjectConst>(),{"action","button"},{"repeat_count"},{"optimistic_switch"},error),error.c_str());
  });
  test("Empty runtime configuration, password-free export and invalid config", [] {
    RuntimeConfig config; DynamicJsonDocument doc(4096); doc.to<JsonObject>(); std::string error;
    check(configFromJson(doc.as<JsonObjectConst>(),config,error),error.c_str());
    check(config.wifiSsid.empty() && config.mqttPort==1883 && !config.webAuth,"Wrong empty config defaults");
    config.wifiPassword="TEST_ONLY_SECRET"; config.mqttPassword="TEST_ONLY_SECRET";
    configToJson(config,doc.to<JsonObject>(),false); std::string json; serializeJson(doc,json);
    check(json.find("TEST_ONLY_SECRET")==std::string::npos && !doc.containsKey("mqtt_password"),"Export leaks password");
    for (const auto& invalid : {std::string("{\"mqtt_port\":0}"),std::string("{\"mqtt_port\":\"1883\"}"),std::string("{\"web_auth\":true}"),std::string("{\"wifi_ssid\":{}}")}) {
      doc.clear(); deserializeJson(doc,invalid); RuntimeConfig next=config;
      check(!configFromJson(doc.as<JsonObjectConst>(),next,error),"Invalid config accepted");
      check(next.mqttPort==config.mqttPort && next.webAuth==config.webAuth,"Invalid config modified runtime");
    }
  });
  test("Multiple remotes load with unique stable IDs", [] {
    DynamicJsonDocument doc(4096); indexToJson(fixture(), doc); std::string text; serializeJson(doc, text);
    doc.clear(); check(!deserializeJson(doc, text), "JSON parse"); Remotes result; std::string error;
    check(indexFromJson(doc.as<JsonVariantConst>(), result, error), error.c_str());
    check(result.size()==2 && result[0].buttons.size()==2 && result[1].buttons.size()==1, "Missing buttons");
    check(!validCodePath("/data/codes/../../config.json"), "Path traversal accepted");
  });
  test("Fast learn of decoded NEC and RAW", [] {
    for (const auto& s : {nec(),raw()}) { LearningSession learner; std::string error;
      check(learner.start(1,20,0,error), "Start"); check(learner.observe(s,100)==LearnResult::Confirmed,"Not confirmed");
      learner.saved(true, ""); check(learner.status()=="saved", "Not saved"); roundTrip(s);
    }
  });
  test("Reliable learning tolerance and held-key rejection", [] {
    LearningSession l; std::string error; auto a=raw(); auto b=a;
    for (auto& sample : b.raw) sample = sample * 110 / 100;
    check(l.start(3,20,0,error), "Start"); check(l.observe(a,100)==LearnResult::Candidate,"First");
    check(l.observe(a,200)==LearnResult::Ignored,"Held key counted");
    check(l.observe(b,700)==LearnResult::Candidate,"Tolerance");
    check(l.observe(a,1200)==LearnResult::Confirmed,"Third");
    b.raw[1] *= 2; check(!similarSignals(a,b),"Mismatch accepted");
  });
  test("Learning timeout and millis wraparound", [] {
    LearningSession l; std::string error; const uint32_t start=UINT32_MAX-100;
    check(l.start(2,20,start,error),"Start"); l.tick(start+1000); check(l.active(),"Early timeout");
    l.tick(start+Config::LEARN_TIMEOUT_MS); check(l.status()=="timeout","No timeout");
  });
  test("Reliable RAW learning across uint16 timing split boundaries", [] {
    auto withGap=[](uint32_t gap) { Signal s; s.raw={900,1000,500}; appendTiming(s.raw,gap); s.raw.insert(s.raw.end(),{500,450,500}); return s; };
    for (auto durations : {std::make_pair(63000U,66000U),std::make_pair(129000U,134000U)}) {
      auto a=withGap(durations.first),b=withGap(durations.second);
      check(a.raw.size()!=b.raw.size(),"Fixture did not cross a split boundary");
      check(similarSignals(a,b,20) && similarSignals(b,a,20),"Equivalent long pause rejected");
      LearningSession learner; std::string error; check(learner.start(3,20,0,error),"Start");
      check(learner.observe(a,100)==LearnResult::Candidate,"First");
      check(learner.observe(b,700)==LearnResult::Candidate,"Second");
      check(learner.observe(a,1300)==LearnResult::Confirmed,"No reliable confirmation");
      b.raw.insert(b.raw.end(),{300,300}); check(!similarSignals(a,b),"Extra physical edges ignored");
    }
  });
  test("Very long RAW, long-gap splitting and overflow rejection", [] {
    Signal s; s.raw.assign(4095,200); roundTrip(s);
    s=raw(); appendTiming(s.raw,150000); s.raw.push_back(500); roundTrip(s);
    check(s.raw[7]==65535 && s.raw[8]==0 && s.raw[9]==65535 && s.raw[10]==0,"Long gap split");
    std::string error; s.overflow=true; check(!validSignal(s,error),"Overflow accepted");
    s.overflow=false; s.raw.resize(Config::MAX_RAW_SAMPLES+1,100); check(!validSignal(s,error),"Oversize accepted");
  });
  test("Invalid signal JSON, duplicate fields and truncated data", [] {
    StringWriter writer; std::string error; writeSignal(nec(),writer,error);
    for (const auto& text : {std::string("{}"), writer.text.substr(0,writer.text.size()-2),
                            writer.text+"junk", std::string("{\"raw\":[1,2],\"raw\":[3]}")}) {
      StringReader r(text); Signal s; check(!readSignal(r,s,error),"Invalid JSON accepted");
    }
  });
  test("Strict IR metadata types, unknown fields and frequency source", [] {
    StringWriter writer; std::string error; check(writeSignal(nec(),writer,error),"Encode");
    for (const char* key : {"protocol","repeat","decoded_reliable","frequency_source","state","raw_length","command","unexpected"}) {
      DynamicJsonDocument doc(8192); check(!deserializeJson(doc,writer.text),"Parse fixture");
      if (std::string(key)=="frequency_source") doc[key]="invented";
      else if (std::string(key)=="repeat") doc[key]="false";
      else if (std::string(key)=="state") doc.createNestedArray(key);
      else if (std::string(key)=="raw_length") doc[key]="67";
      else doc[key]=123;
      std::string invalid; serializeJson(doc,invalid); Signal result;
      StringReader typedReader(invalid); check(!readSignal(typedReader,result,error),"Invalid IR field accepted");
    }
  });
  test("RAW parsing stops before an allocation exceeds available memory", [] {
    StringWriter writer; std::string error; check(writeSignal(nec(),writer,error),"Encode");
    BudgetReader hinted(writer.text,100); Signal result;
    check(!readSignal(hinted,result,error) && error.find("memory")!=std::string::npos,"Declared length ignored heap budget");
    DynamicJsonDocument doc(8192),reordered(8192); deserializeJson(doc,writer.text);
    reordered["raw"]=doc["raw"];
    for (JsonPair field : doc.as<JsonObject>()) if (std::string(field.key().c_str())!="raw") reordered[field.key().c_str()]=field.value();
    std::string text; serializeJson(reordered,text); BudgetReader growing(text,100);
    check(!readSignal(growing,result,error) && error.find("memory")!=std::string::npos,"Growing RAW ignored heap budget");
    BudgetReader enough(writer.text,1024); check(readSignal(enough,result,error),error.c_str());
    check(result.raw==nec().raw,"Heap guard changed valid timings");
  });
  test("AUTO preserves RAW and carrier when decoded metadata is unreliable", [] {
    auto s=nec(); check(preferDecoded(s,SendMode::Auto),"Canonical NEC does not use protocol sender");
    s.frequency=40000; check(!preferDecoded(s,SendMode::Auto),"Stored carrier ignored");
    s=nec(); s.value^=1; check(!preferDecoded(s,SendMode::Auto),"Unverified value used in AUTO");
    s=nec(); s.repeat=true; check(!preferDecoded(s,SendMode::Auto),"Repeat capture was sent as initial frame");
    s=nec(); s.protocol="SAMSUNG"; check(!preferDecoded(s,SendMode::Auto),"Unverified protocol flag trusted");
    check(preferDecoded(s,SendMode::Decoded),"Explicit decoded override ignored");
    s=nec(); s.frequencySource="manual"; check(!preferDecoded(s,SendMode::Auto),"Manual frequency ignored");
  });
  test("Restart during write, validation, before/after atomic replace", [] {
    const std::string path="/data/remotes.json", old="{\"version\":1,\"remotes\":[]}";
    std::string next; DynamicJsonDocument doc(4096); indexToJson(fixture(),doc); serializeJson(doc,next);
    auto validator=[](const std::string& text,std::string& error) { DynamicJsonDocument d(4096); Remotes r;
      return !deserializeJson(d,text) && indexFromJson(d.as<JsonVariantConst>(),r,error); };
    for (int fault=0;fault<=4;++fault) {
      MemoryFiles files; files.files[path]=old; files.fault=fault; std::string error;
      const bool success=atomicWrite(files,path,next,validator,error);
      check(success==(fault==0),"Unexpected transaction result");
      check(files.files[path]==(fault==0 || fault==4 ? next : old),"Neither intact old nor intact new database");
      files.fault=0; check(validator(files.files[path],error),"Restart reads invalid JSON");
    }
  });
  test("Delete a learned button and deserialize remaining remotes", [] {
    auto remotes=fixture(); check(eraseButton(remotes,"00000001","00000002"),"Delete failed");
    check(remotes[0].buttons.size()==1 && remotes[0].buttons[0].name=="Speed +","Wrong button deleted");
    check(!eraseButton(remotes,"00000001","ffffffff"),"Missing button deleted");
    DynamicJsonDocument doc(4096); indexToJson(remotes,doc); Remotes loaded; std::string error;
    check(indexFromJson(doc.as<JsonVariantConst>(),loaded,error),error.c_str()); check(loaded[0].buttons.size()==1,"Delete not persistent");
  });
  test("WiFi and MQTT reconnect gates, no retry flood and wraparound", [] {
    for (auto interval : {Config::WIFI_RETRY_MS,Config::MQTT_RETRY_MS}) {
      RetryGate gate; const uint32_t now=UINT32_MAX-100;
      check(gate.due(now),"Initial attempt suppressed"); gate.schedule(now,interval);
      check(!gate.due(now+interval-1),"Premature retry"); check(gate.due(now+interval),"No retry");
      gate.reset(); check(gate.due(now+10),"Disconnect reset ineffective");
    }
  });
  test("HA discovery generation, stable rename and one device", [] {
    auto remotes=fixture(); DiscoveryContext context={"aabbccddeeff","homeassistant","irbridge/aabbccddeeff","192.168.1.5"};
    auto topics=discoveryTopics(remotes,context); check(topics.size()==8,"Wrong discovery count");
    std::string payload; check(discoveryPayload(remotes,context,topics[0],payload),"No config");
    DynamicJsonDocument doc(4096); check(!deserializeJson(doc,payload),"Invalid HA JSON");
    check(doc["name"]=="Ventilator Power" && doc["device"]["sw_version"]==Config::VERSION,"Wrong name/version");
    check(doc["device"]["identifiers"][0]=="irbridge_aabbccddeeff","Not MAC based");
    check(doc["command_topic"]=="irbridge/aabbccddeeff/send","Wrong command topic");
    check(doc.containsKey("payload_press") && doc.containsKey("availability_topic"),"Missing HA fields");
    const std::string id=doc["unique_id"].as<std::string>(); remotes[0].buttons[0].name="An/Aus";
    check(discoveryTopics(remotes,context)==topics,"Rename changed discovery identity");
    check(discoveryPayload(remotes,context,topics[0],payload),"Rename config"); deserializeJson(doc,payload);
    check(doc["unique_id"].as<std::string>()==id && doc["name"]=="Ventilator An/Aus","Rename config wrong");
  });
  test("HA retained empty deletion including offline history", [] {
    auto remotes=fixture(); DiscoveryContext context={"aabbccddeeff","homeassistant","irbridge/aabbccddeeff","192.168.1.5"};
    auto history=discoveryTopics(remotes,context); auto deleted=history[0];
    check(eraseButton(remotes,"00000001","00000002"),"Delete");
    check(mergeDiscoveryHistory(history,discoveryTopics(remotes,context))==history,"Tombstone lost before reconnect");
    std::string payload="old"; check(!discoveryPayload(remotes,context,deleted,payload) && payload.empty(),"No empty retained tombstone");
  });
  test("Power toggle remains button; explicit ON/OFF and optimistic opt-in", [] {
    auto remotes=fixture(); remotes[0].buttons[0].entityType="toggle";
    DiscoveryContext context={"aabbccddeeff","homeassistant","irbridge/aabbccddeeff","192.168.1.5"};
    check(discoveryTopics(remotes,context).size()==8,"Toggle exposes false-state switch");
    remotes[0].optimisticSwitch=true; auto topic=discoveryTopic(context,"switch",remotes[0].id); std::string payload;
    check(discoveryPayload(remotes,context,topic,payload),"No optimistic switch"); DynamicJsonDocument doc(4096); deserializeJson(doc,payload);
    check(doc["optimistic"]==true && !doc.containsKey("state_topic"),"Claims measured state");
    check(doc["payload_on"]==doc["payload_off"],"Toggle codes differ");
    remotes[0].optimisticSwitch=false; remotes[0].buttons[0].entityType="on"; remotes[0].buttons[1].entityType="off";
    check(discoveryPayload(remotes,context,topic,payload),"No ON/OFF switch"); deserializeJson(doc,payload);
    check(doc["payload_on"]!=doc["payload_off"],"ON/OFF not separate");
  });
  test("Streaming backup import and invalid backup retains active database", [] {
    StringWriter signal; std::string error; check(writeSignal(nec(),signal,error),"Signal encode");
    const std::string good="{\"version\":1,\"configuration\":{},\"remotes\":[{\"id\":\"00000001\",\"name\":\"Ventilator\",\"buttons\":[{\"id\":\"00000002\",\"name\":\"Power\",\"signal\":"+signal.text+"}]}]}";
    int index=16;
    auto stage=[&](const Signal& s,std::string& path,std::string&) { roundTrip(s); char name[27]; snprintf(name,sizeof(name),"/data/codes/c%08x.json",index++); path=name; return true; };
    StringReader reader(good); Remotes result; std::string configuration;
    check(readBackup(reader,result,configuration,stage,error),error.c_str()); check(result.size()==1 && result[0].buttons.size()==1,"Backup lost button");
    for (const auto& invalid : {std::string("{}"),good.substr(0,good.size()-4),good+"trailing",
          std::string("{\"version\":2,\"configuration\":{},\"remotes\":[]}"),
          std::string("{\"version\":1,\"configuration\":{\"mqtt_password\":\"secret\"},\"remotes\":[]}"),
          std::string("{\"version\":1,\"version\":1,\"configuration\":{},\"remotes\":[]}")}) {
      auto active=fixture(); auto next=active; StringReader bad(invalid);
      check(!readBackup(bad,next,configuration,stage,error),"Invalid backup accepted");
      check(next.size()==active.size() && next[0].buttons.size()==active[0].buttons.size(),"Invalid import changed active database");
    }
  });
  test("Typed remote and button metadata reject malformed index and backup", [] {
    std::string error;
    for (int kind=0;kind<6;++kind) {
      DynamicJsonDocument index(8192); indexToJson(fixture(),index);
      auto remote=index["remotes"][0].as<JsonObject>(); auto button=remote["buttons"][0].as<JsonObject>();
      if (kind==0) remote["name"]=123;
      if (kind==1) remote["optimistic_switch"]="yes";
      if (kind==2) button["name"]=true;
      if (kind==3) button["repeat_count"]=1.5;
      if (kind==4) button["entity_type"]=7;
      if (kind==5) button["send_mode"]=false;
      auto active=fixture(); check(!indexFromJson(index.as<JsonVariantConst>(),active,error),"Malformed index accepted");
      check(active[0].name=="Ventilator" && active[0].buttons.size()==2,"Invalid index changed active data");
      StringWriter signal; check(writeSignal(nec(),signal,error),"Signal"); DynamicJsonDocument parsedSignal(8192); deserializeJson(parsedSignal,signal.text);
      for (JsonObject r : index["remotes"].as<JsonArray>()) for (JsonObject b : r["buttons"].as<JsonArray>()) { b.remove("file"); b["signal"]=parsedSignal.as<JsonObject>(); }
      index.createNestedObject("configuration"); std::string backup; serializeJson(index,backup); StringReader reader(backup); std::string configuration;
      int code=16;
      auto stage=[&code](const Signal&,std::string& path,std::string&) { char name[27]; snprintf(name,sizeof(name),"/data/codes/c%08x.json",code++); path=name; return true; };
      check(!readBackup(reader,active,configuration,stage,error),"Malformed backup accepted");
      check(active[0].name=="Ventilator" && active[0].buttons.size()==2,"Invalid backup changed active data");
    }
  });
  test("Scene references, appearance and order survive index and backup roundtrips", [] {
    auto remotes=fixture(); std::string error;
    remotes[0].layout="directional"; remotes[0].buttons[0].icon="power"; remotes[0].buttons[0].slot="power"; remotes[0].buttons[0].favorite=true;
    Macro scene; scene.id="00000020"; scene.name="Filmabend";
    scene.steps={{"00000001","00000002",0},{"00000004","00000005",1500}};
    remotes[0].macros.push_back(scene);
    DynamicJsonDocument doc(32768); indexToJson(remotes,doc);
    Remotes parsed; check(indexFromJson(doc.as<JsonVariantConst>(),parsed,error),error.c_str());
    check(parsed[0].macros[0].steps[1].delayMs==1500 && parsed[0].buttons[0].favorite && parsed[0].buttons[0].slot=="power", "Appearance or scene lost");
    DynamicJsonDocument order(512); deserializeJson(order,"[\"00000003\",\"00000002\"]");
    check(reorderButtons(parsed[0],order.as<JsonArrayConst>(),error),error.c_str());
    check(parsed[0].buttons[0].id=="00000003" && parsed[0].macros[0].steps[0].button=="00000002","Reorder changed references");
    order[0]="00000002"; check(!reorderButtons(parsed[0],order.as<JsonArrayConst>(),error),"Duplicate order accepted");
    doc.remove("version"); doc["version"]=1; doc.createNestedObject("configuration");
    StringWriter signal; check(writeSignal(nec(),signal,error),"Signal"); DynamicJsonDocument signalDoc(8192); deserializeJson(signalDoc,signal.text);
    for (JsonObject remote : doc["remotes"].as<JsonArray>()) for (JsonObject button : remote["buttons"].as<JsonArray>()) { button.remove("file"); button["signal"]=signalDoc.as<JsonVariantConst>(); }
    std::string text; serializeJson(doc,text); StringReader reader(text); std::string configuration; unsigned index=32;
    auto stage=[&](const Signal&,std::string& path,std::string&) { char file[27]; snprintf(file,sizeof(file),"/data/codes/c%08x.json",++index); path=file; return true; };
    Remotes imported; check(readBackup(reader,imported,configuration,stage,error),error.c_str());
    check(imported[0].layout=="directional" && imported[0].macros[0].id==scene.id && imported[0].buttons[0].favorite,"Backup lost new metadata");
  });
  test("Scenes reject missing references, invalid delays, duplicate IDs and control positions", [] {
    auto remotes=fixture(); Macro scene; scene.id="00000020"; scene.name="Scene"; scene.steps={{"00000001","00000002",0}}; remotes[0].macros.push_back(scene);
    std::string error; check(validMacros(remotes,error),"Valid scene");
    auto broken=remotes; broken[0].macros[0].steps[0].button="000000ff"; check(!validMacros(broken,error),"Missing button accepted");
    broken=remotes; broken[0].macros[0].id="00000002"; check(!validMacros(broken,error),"Duplicate ID accepted");
    broken=remotes; broken[0].buttons[0].slot=broken[0].buttons[1].slot="power"; check(!validMacros(broken,error),"Duplicate position accepted");
    DynamicJsonDocument json(8192); macroToJson(scene,json.to<JsonObject>()); Macro parsed;
    for (const char* bad : {"-1","60001","true","1.5","\"100\""}) {
      DynamicJsonDocument value(128); deserializeJson(value,bad); json["steps"][0]["delay_ms"]=value.as<JsonVariantConst>();
      check(!macroFromJson(json.as<JsonObjectConst>(),parsed,error),"Invalid delay accepted");
    }
    json["steps"][0]["delay_ms"]=0; json["steps"][0]["button"]=123;
    check(!macroFromJson(json.as<JsonObjectConst>(),parsed,error),"Typed step silently defaulted");
  });
  test("Queue preserves order, clocks delays after TX and publishes request results", [] {
    CommandQueue queue; CommandResult result; std::string error; uint32_t now=100; std::vector<std::string> sent, events;
    queue.onResult([&](const CommandResult& item){events.push_back(item.requestId+":"+item.status);});
    auto execute=[&](const CommandStep& step,std::string&){sent.push_back(step.button); now+=200; return true;};
    check(queue.enqueue("scene-1",{{"00000001","00000002",0},{"00000004","00000005",500}},"web",now,result,error),error.c_str());
    check(queue.enqueue("button-2",{{"00000001","00000003",0}},"mqtt",now,result,error),error.c_str());
    queue.tick(now,execute,[&]{return now;}); check(sent.size()==1 && queue.find("scene-1")->completed==1,"First step not sent");
    now=799; queue.tick(now,execute,[&]{return now;}); check(sent.size()==1,"Delay measured before TX ended");
    now=800; queue.tick(now,execute,[&]{return now;}); check(queue.find("scene-1")->status=="sent","Scene result not terminal");
    queue.tick(now,execute,[&]{return now;}); check(sent.size()==2,"Inter-command gap skipped");
    now+=40; queue.tick(now,execute,[&]{return now;}); check(sent==std::vector<std::string>({"00000002","00000005","00000003"}),"Queue reordered sends");
    check(queue.find("button-2")->status=="sent" && events.front()=="scene-1:queued" && events.back()=="button-2:sent","Result sequence wrong");
    check(!queue.enqueue("scene-1",{{"00000001","00000002",0}},"mqtt",now,result,error),"Duplicate request retransmitted");
    now+=0x80000000U;
    check(queue.enqueue("after-long-idle",{{"00000001","00000002",0}},"web",now,result,error),error.c_str());
    queue.tick(now,execute,[&]{return now;});
    check(queue.find("after-long-idle")->status=="sent","Old TX cooldown blocked a command after long idle");
  });
  test("Queue bounds, cancellation, TX failure, update interruption and millis wraparound", [] {
    CommandQueue queue; CommandResult result; std::string error; uint32_t now=UINT32_MAX-20; size_t calls=0;
    check(queue.enqueue("wrap",{{"00000001","00000002",50},{"00000001","00000003",0}},"mqtt",now,result,error),error.c_str());
    auto execute=[&](const CommandStep&,std::string& e){++calls; e="Missing code"; return calls==1;};
    queue.tick(now,execute,[&]{return now;}); now=28; queue.tick(now,execute,[&]{return now;}); check(!calls,"Delay expired early across wrap");
    now=29; queue.tick(now,execute,[&]{return now;}); now=69; queue.tick(now,execute,[&]{return now;});
    check(queue.find("wrap")->status=="failed" && queue.find("wrap")->completed==1 && calls==2,"Failed scene continued or lost progress");
    for (size_t i=0;i<Config::MAX_QUEUED_COMMANDS;++i) check(queue.enqueue("job-"+std::to_string(i),{{"00000001","00000002",60000}},"web",now,result,error),"Queue should accept capacity");
    check(!queue.enqueue("overflow",{{"00000001","00000002",0}},"web",now,result,error),"Queue exceeded limit");
    check(queue.cancel("job-0",now,error) && queue.find("job-0")->status=="failed","Cancellation not recorded");
    queue.failAll("Update interrupted queue",now); check(!queue.pending() && queue.find("job-5")->status=="failed","Update left commands runnable");
    for (int i=0;i<12;++i) { check(queue.enqueue("recent-"+std::to_string(i),{{"00000001","00000002",0}},"web",now,result,error),"History prevented new send"); queue.failAll("test",now); }
    check(queue.history().size()==Config::COMMAND_HISTORY_SIZE && !queue.find("wrap"),"History unbounded");
  });
  test("HA scene and five diagnostic entities share the existing device", [] {
    auto remotes=fixture(); Macro scene; scene.id="00000020"; scene.name="Scene"; scene.steps={{"00000001","00000002",0}}; remotes[0].macros.push_back(scene);
    DiscoveryContext context={"aabbccddeeff","homeassistant","irbridge/aabbccddeeff","192.168.1.5"};
    std::string text; check(discoveryTopics(remotes,context).size()==9,"Scene/diagnostics missing");
    check(discoveryPayload(remotes,context,discoveryTopic(context,"button",scene.id),text),"Scene discovery missing");
    DynamicJsonDocument doc(4096); deserializeJson(doc,text); check(doc["payload_press"].as<std::string>().find("macro")!=std::string::npos,"Scene uses wrong command");
    for (const char* id : {"diag_rssi","diag_heap","diag_uptime","diag_wifi_reconnects","diag_mqtt_reconnects"}) {
      check(discoveryPayload(remotes,context,discoveryTopic(context,"sensor",id),text),"Diagnostic missing"); deserializeJson(doc,text);
      check(doc["entity_category"]=="diagnostic" && doc["state_topic"]==context.baseTopic+"/info" && !doc.containsKey("command_topic"),"Invalid sensor payload");
      check(doc["device"]["identifiers"][0]=="irbridge_aabbccddeeff","Separate device created");
    }
  });
  test("Protected recovery configuration preserves secrets and keeps first boot reachable", [] {
    RuntimeConfig config; config.recoveryPassword="private-key-123"; std::string error; DynamicJsonDocument doc(4096);
    configToJson(config,doc.to<JsonObject>(),false); check(!doc.containsKey("recovery_password") && doc["recovery_password_set"]==true,"Recovery secret exported");
    doc["ap_fallback_enabled"]=false; check(configFromJson(doc.as<JsonObjectConst>(),config,error),error.c_str());
    check(!config.apFallbackEnabled && config.recoveryPassword=="private-key-123","Unchanged secret lost");
    doc["recovery_password"]="short"; check(!configFromJson(doc.as<JsonObjectConst>(),config,error),"Weak AP password accepted");
    check(!recoveryApRequired(true,true,false,60000),"Disabled fallback opened AP");
    check(recoveryApRequired(false,true,false,0) && recoveryApRequired(true,false,false,0),"First boot/recovery inaccessible");
    check(!recoveryApRequired(true,true,true,44999) && recoveryApRequired(true,true,true,45000),"Fallback threshold wrong");
    std::string backup="{\"version\":1,\"configuration\":{\"recovery_password\":\"secret123\"},\"remotes\":[]}";
    StringReader reader(backup); Remotes next; std::string configuration;
    check(!readBackup(reader,next,configuration,[](const Signal&,std::string&,std::string&){return true;},error),"Secret-bearing backup accepted");
  });
  test("Firmware stream rejects wrong targets, oversized and truncated images", [] {
    std::vector<uint8_t> image(256,0); image[0]=0xe9; image[1]=1; image[2]=3; image[3]=0x40;
    const std::string marker=Config::FIRMWARE_TARGET; std::copy(marker.begin(),marker.end(),image.begin()+10);
    std::string error; FirmwareCheck valid(image.size(),512);
    check(valid.feed(image.data(),16,error) && valid.feed(image.data()+16,13,error) && valid.feed(image.data()+29,image.size()-29,error) && valid.finish(error),error.c_str());
    FirmwareCheck truncated(image.size(),512); check(truncated.feed(image.data(),32,error) && !truncated.finish(error),"Truncated firmware accepted");
    FirmwareCheck oversized(image.size(),128); check(!oversized.feed(image.data(),32,error),"Oversized image accepted");
    image[0]=0x7f; FirmwareCheck elf(image.size(),512); check(!elf.feed(image.data(),image.size(),error),"ELF accepted");
    image[0]=0xe9; image[10]='X'; FirmwareCheck foreign(image.size(),512); check(foreign.feed(image.data(),image.size(),error) && !foreign.finish(error),"Foreign application accepted");
    check(validMd5("d41d8cd98f00b204e9800998ecf8427e") && !validMd5("not-a-digest") && !validMd5(std::string(32,'z')),"MD5 format not checked");
  });
  std::cout << total-failures << '/' << total << " tests passed\n";
  return failures ? 1 : 0;
}
