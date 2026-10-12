#include "backup.h"
#include <algorithm>
#include <set>

namespace Bridge {
bool validateRemotes(const Remotes& remotes, std::string& error) {
  if (remotes.size()>Config::MAX_REMOTES) { error="Remote limit exceeded"; return false; }
  std::set<std::string> ids,names,files; size_t buttons=0;
  for (const auto& r : remotes) {
    if (!validId(r.id) || !validName(r.name) || !ids.insert(r.id).second || !names.insert(r.name).second) { error="Invalid/duplicate remote"; return false; }
    std::set<std::string> buttonNames;
    for (const auto& b : r.buttons) {
      if (!validId(b.id) || !validName(b.name) || !validCodePath(b.file) || !validEntityType(b.entityType) ||
          b.repeatCount<1 || b.repeatCount>10 || !ids.insert(b.id).second || !buttonNames.insert(b.name).second ||
          !files.insert(b.file).second || ++buttons>Config::MAX_BUTTONS) { error="Invalid/duplicate button or button limit"; return false; }
    }
  }
  if (!validMacros(remotes, error)) return false;
  error.clear(); return true;
}
bool eraseButton(Remotes& remotes, const std::string& remoteId, const std::string& buttonId) {
  for (auto& r : remotes) if (r.id==remoteId) {
    auto it=std::find_if(r.buttons.begin(),r.buttons.end(),[&](const Button& b) { return b.id==buttonId; });
    if (it==r.buttons.end()) return false;
    r.buttons.erase(it); return true;
  }
  return false;
}
namespace {
bool metadataValue(JsonStream& stream, const std::string& key, JsonDocument& metadata, std::string& error) {
  std::string text;
  if (!stream.value(text,1024)) { error="Invalid/oversized backup metadata"; return false; }
  DynamicJsonDocument value(1536);
  if (deserializeJson(value,text)) { error="Invalid backup JSON value"; return false; }
  metadata[key]=value.as<JsonVariantConst>();
  if (metadata.overflowed()) { error="Backup metadata limit exceeded"; return false; }
  return true;
}
bool readButton(JsonStream& stream, Button& button, const CodeStager& stage, std::string& error) {
  if (!stream.expect('{')) { error="Backup button must be object"; return false; }
  DynamicJsonDocument metadata(2048); std::set<std::string> fields; bool hasSignal=false;
  while (true) {
    std::string key;
    if (!stream.key(key) || !fields.insert(key).second) { error="Invalid/duplicate button field"; return false; }
    if (key=="signal") {
      Signal signal; hasSignal=true;
      if (!readSignal(stream,signal,error) || !stage(signal,button.file,error)) return false;
    } else {
      if (key!="id" && key!="name" && key!="entity_type" && key!="send_mode" && key!="repeat_count" && key!="icon" && key!="slot" && key!="favorite") {
        error="Unknown backup button field"; return false;
      }
      if (!metadataValue(stream,key,metadata,error)) return false;
    }
    stream.whitespace(); const int separator=stream.get();
    if (separator=='}') break;
    if (separator!=',') { error="Invalid backup button separator"; return false; }
  }
  if (!validButtonFields(metadata.as<JsonObjectConst>(),error,false)) return false;
  button.id=metadata["id"].as<std::string>(); button.name=metadata["name"].as<std::string>();
  button.entityType=metadata["entity_type"] | "button";
  const int repeat=metadata["repeat_count"] | 1;
  if (!hasSignal || !validId(button.id) || !validName(button.name) || !validEntityType(button.entityType) ||
      !parseMode(metadata["send_mode"] | "AUTO",button.mode) || repeat<1 || repeat>10) {
    error="Invalid backup button metadata/signal"; return false;
  }
  button.repeatCount=repeat;
  button.icon=metadata["icon"] | ""; button.slot=metadata["slot"] | ""; button.favorite=metadata["favorite"] | false;
  return true;
}
bool readButtons(JsonStream& stream, std::vector<Button>& result, size_t& total, const CodeStager& stage,std::string& error) {
  if (!stream.expect('[')) { error="Buttons must be array"; return false; }
  stream.whitespace(); if (stream.peek()==']') { stream.get(); return true; }
  while (true) {
    Button button;
    if (++total>Config::MAX_BUTTONS || !readButton(stream,button,stage,error)) {
      if (error.empty()) error="Button limit exceeded";
      return false;
    }
    result.push_back(std::move(button)); stream.whitespace(); const int separator=stream.get();
    if (separator==']') return true;
    if (separator!=',') { error="Invalid buttons separator"; return false; }
  }
}
bool readRemote(JsonStream& stream, Remote& remote,size_t& total,const CodeStager& stage,std::string& error) {
  if (!stream.expect('{')) { error="Remote must be object"; return false; }
  DynamicJsonDocument metadata(1024); std::set<std::string> fields; bool buttons=false;
  while (true) {
    std::string key;
    if (!stream.key(key) || !fields.insert(key).second) { error="Invalid/duplicate remote field"; return false; }
    if (key=="buttons") { buttons=true; if (!readButtons(stream,remote.buttons,total,stage,error)) return false; }
    else if (key=="macros") {
      if (!stream.expect('[')) { error="Scenes must be an array"; return false; }
      stream.whitespace();
      if (stream.peek()==']') stream.get();
      else while (true) {
        std::string value; Macro macro;
        if (remote.macros.size()>=Config::MAX_MACROS || !stream.value(value,4096)) { error="Scene metadata limit exceeded"; return false; }
        DynamicJsonDocument doc(4096);
        if (deserializeJson(doc,value) || !macroFromJson(doc.as<JsonObjectConst>(),macro,error)) return false;
        remote.macros.push_back(std::move(macro));
        stream.whitespace(); const int separator=stream.get();
        if (separator==']') break;
        if (separator!=',') { error="Invalid scenes separator"; return false; }
      }
    }
    else {
      if (key!="id" && key!="name" && key!="optimistic_switch" && key!="layout") { error="Unknown remote field"; return false; }
      if (!metadataValue(stream,key,metadata,error)) return false;
    }
    stream.whitespace(); const int separator=stream.get();
    if (separator=='}') break;
    if (separator!=',') { error="Invalid remote separator"; return false; }
  }
  if (!validRemoteFields(metadata.as<JsonObjectConst>(),error)) return false;
  remote.id=metadata["id"].as<std::string>(); remote.name=metadata["name"].as<std::string>();
  remote.optimisticSwitch=metadata["optimistic_switch"] | false;
  remote.layout=metadata["layout"] | "grid";
  if (!buttons || !validId(remote.id) || !validName(remote.name)) { error="Invalid remote metadata"; return false; }
  return true;
}
bool readRemotes(JsonStream& stream,Remotes& result,const CodeStager& stage,std::string& error) {
  if (!stream.expect('[')) { error="Remotes must be array"; return false; }
  stream.whitespace(); if (stream.peek()==']') { stream.get(); return true; }
  size_t buttons=0;
  while (true) {
    Remote remote;
    if (result.size()>=Config::MAX_REMOTES || !readRemote(stream,remote,buttons,stage,error)) return false;
    result.push_back(std::move(remote)); stream.whitespace(); const int separator=stream.get();
    if (separator==']') return true;
    if (separator!=',') { error="Invalid remotes separator"; return false; }
  }
}
}
bool readBackup(Reader& reader, Remotes& result,std::string& config,const CodeStager& stage,std::string& error) {
  JsonStream stream(reader); Remotes next; std::set<std::string> keys;
  bool version=false,remotes=false,configuration=false;
  if (!stream.expect('{')) { error="Backup must be JSON object"; return false; }
  while (true) {
    std::string key;
    if (!stream.key(key) || !keys.insert(key).second) { error="Invalid/duplicate backup field"; return false; }
    if (key=="remotes") { remotes=true; if (!readRemotes(stream,next,stage,error)) return false; }
    else {
      std::string value;
      if (!stream.value(value,4096)) { error="Backup metadata too large"; return false; }
      DynamicJsonDocument doc(4096);
      if (deserializeJson(doc,value)) { error="Invalid backup metadata JSON"; return false; }
      if (key=="version") { version=doc.is<int>() && doc.as<int>()==1; }
      else if (key=="firmware_version") { if (!doc.is<const char*>()) { error="Invalid firmware version"; return false; } }
      else if (key=="configuration") {
        if (!doc.is<JsonObject>()) { error="Configuration must be object"; return false; }
        for (const char* password : {"wifi_password","mqtt_password","ota_password","web_password","recovery_password"})
          if (doc.containsKey(password)) { error="Backup must not contain passwords"; return false; }
        config=value; configuration=true;
      } else { error="Unknown backup field"; return false; }
    }
    stream.whitespace(); const int separator=stream.get();
    if (separator=='}') break;
    if (separator!=',') { error="Invalid backup separator"; return false; }
  }
  stream.whitespace();
  if (!version || !remotes || !configuration || stream.peek()!=-1 || !validateRemotes(next,error)) {
    if (error.empty()) error="Unsupported/incomplete backup or trailing data";
    return false;
  }
  result=std::move(next); error.clear(); return true;
}
}
