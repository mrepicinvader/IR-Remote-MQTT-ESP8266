#include "model.h"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>

namespace Bridge {
bool validName(const std::string& name) {
  if (name.empty() || name.size() > 48) return false;
  bool visible = false;
  for (unsigned char c : name) {
    if (c < 32 || c == 127) return false;
    visible |= c != ' ';
  }
  return visible;
}
bool validId(const std::string& id) {
  return id.size() == 8 && std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  });
}
bool validCodePath(const std::string& path) {
  return path.size() == 26 && path.substr(0, 13) == "/data/codes/c" &&
      validId(path.substr(13, 8)) && path.substr(21) == ".json";
}
bool validEntityType(const std::string& type) {
  return type == "button" || type == "on" || type == "off" || type == "toggle" || type == "custom";
}
bool validIcon(const std::string& icon) {
  for (const char* value : {"", "power", "up", "down", "left", "right", "ok", "volume_up", "volume_down", "mute", "play", "pause", "stop", "tv", "fan", "light", "source"})
    if (icon == value) return true;
  return false;
}
bool validSlot(const std::string& slot) {
  for (const char* value : {"", "power", "up", "down", "left", "right", "ok", "volume_up", "volume_down", "mute", "play", "pause", "stop", "source"})
    if (slot == value) return true;
  return false;
}
bool validLayout(const std::string& layout) { return layout == "grid" || layout == "directional" || layout == "media"; }
void macroToJson(const Macro& macro, JsonObject json) {
  json["id"] = macro.id; json["name"] = macro.name;
  auto steps = json.createNestedArray("steps");
  for (const auto& step : macro.steps) {
    auto item = steps.createNestedObject();
    item["remote"] = step.remote; item["button"] = step.button; item["delay_ms"] = step.delayMs;
  }
}
bool macroFromJson(JsonObjectConst json, Macro& result, std::string& error) {
  if (!validRequestTypes(json, {"id", "name"}, {}, {}, error)) return false;
  Macro macro; macro.id = json["id"] | ""; macro.name = json["name"] | "";
  if ((!macro.id.empty() && !validId(macro.id)) || !validName(macro.name) || !json["steps"].is<JsonArrayConst>()) {
    error = "Invalid scene ID, name or steps"; return false;
  }
  uint32_t delays = 0;
  for (JsonObjectConst step : json["steps"].as<JsonArrayConst>()) {
    if (!validRequestTypes(step, {"remote", "button"}, {"delay_ms"}, {}, error)) return false;
    CommandStep item{step["remote"] | "", step["button"] | "", step["delay_ms"] | 0U};
    if (!validId(item.remote) || !validId(item.button) || item.delayMs > Config::MAX_STEP_DELAY_MS || macro.steps.size() >= Config::MAX_MACRO_STEPS) {
      error = "Scene requires stable button IDs, at most 16 steps and delays of 0..60000 ms"; return false;
    }
    delays += item.delayMs;
    if (delays > Config::MAX_MACRO_DELAY_MS) { error = "Scene delays exceed five minutes"; return false; }
    macro.steps.push_back(std::move(item));
  }
  if (macro.steps.empty()) { error = "Scene needs at least one step"; return false; }
  result = std::move(macro); return true;
}
bool validMacros(const Remotes& remotes, std::string& error) {
  std::set<std::string> ids; size_t count = 0;
  for (const auto& remote : remotes) {
    ids.insert(remote.id);
    for (const auto& button : remote.buttons) ids.insert(button.id);
  }
  for (const auto& remote : remotes) {
    std::set<std::string> names, slots;
    if (!validLayout(remote.layout)) { error = "Invalid remote layout"; return false; }
    for (const auto& button : remote.buttons) {
      if (!validIcon(button.icon) || !validSlot(button.slot) || (!button.slot.empty() && !slots.insert(button.slot).second)) {
        error = "Invalid icon or duplicate control position"; return false;
      }
    }
    for (const auto& macro : remote.macros) {
      if (!validId(macro.id) || !validName(macro.name) || !ids.insert(macro.id).second || !names.insert(macro.name).second ||
          ++count > Config::MAX_MACROS || macro.steps.empty() || macro.steps.size() > Config::MAX_MACRO_STEPS) {
        error = "Invalid/duplicate scene or scene limit exceeded"; return false;
      }
      uint32_t delays = 0;
      for (const auto& step : macro.steps) {
        const auto target = std::find_if(remotes.begin(), remotes.end(), [&](const Remote& r) { return r.id == step.remote; });
        if (target == remotes.end() || std::none_of(target->buttons.begin(), target->buttons.end(), [&](const Button& b) { return b.id == step.button; }) ||
            step.delayMs > Config::MAX_STEP_DELAY_MS || (delays += step.delayMs) > Config::MAX_MACRO_DELAY_MS) {
          error = "Scene refers to a missing button or invalid delay"; return false;
        }
      }
    }
  }
  return true;
}
void remoteMetaToJson(const Remote& remote, JsonObject json) {
  json["id"] = remote.id; json["name"] = remote.name; json["optimistic_switch"] = remote.optimisticSwitch;
  json["layout"] = remote.layout;
  auto macros = json.createNestedArray("macros");
  for (const auto& macro : remote.macros) macroToJson(macro, macros.createNestedObject());
}
bool reorderButtons(Remote& remote, JsonArrayConst order, std::string& error) {
  if (order.isNull() || order.size() != remote.buttons.size()) { error = "Order must contain every button ID once"; return false; }
  std::vector<Button> buttons; std::set<std::string> seen;
  for (JsonVariantConst id : order) {
    if (!validJsonText(id)) { error = "Button order must contain IDs"; return false; }
    const std::string value = id.as<std::string>();
    const auto button = std::find_if(remote.buttons.begin(), remote.buttons.end(), [&](const Button& b) { return b.id == value; });
    if (button == remote.buttons.end() || !seen.insert(value).second) { error = "Unknown/duplicate button in order"; return false; }
    buttons.push_back(*button);
  }
  remote.buttons = std::move(buttons); return true;
}
const char* modeName(SendMode mode) {
  return mode == SendMode::Raw ? "RAW" : mode == SendMode::Decoded ? "DECODED" : "AUTO";
}
bool parseMode(const std::string& name, SendMode& result) {
  if (name == "AUTO") result = SendMode::Auto;
  else if (name == "DECODED") result = SendMode::Decoded;
  else if (name == "RAW") result = SendMode::Raw;
  else return false;
  return true;
}
bool validJsonText(JsonVariantConst value) {
  if (!value.is<const char*>()) return false;
  const auto text=value.as<JsonString>();
  return text.c_str() && std::strlen(text.c_str())==text.size();
}
bool validRemoteFields(JsonObjectConst o, std::string& error) {
  if (o.isNull() || !validJsonText(o["id"]) || !validJsonText(o["name"]) ||
      (o.containsKey("optimistic_switch") && !o["optimistic_switch"].is<bool>())) {
    error="Invalid remote field types"; return false;
  }
  if ((o.containsKey("layout") && (!validJsonText(o["layout"]) || !validLayout(o["layout"].as<std::string>()))) ||
      (o.containsKey("macros") && !o["macros"].is<JsonArrayConst>())) { error="Invalid remote layout/scenes"; return false; }
  return true;
}
bool validButtonFields(JsonObjectConst o, std::string& error, bool requireFile) {
  if (o.isNull() || !validJsonText(o["id"]) || !validJsonText(o["name"]) ||
      (requireFile && !validJsonText(o["file"])) ||
      (o.containsKey("entity_type") && !validJsonText(o["entity_type"])) ||
      (o.containsKey("send_mode") && !validJsonText(o["send_mode"])) ||
      (o.containsKey("repeat_count") && !o["repeat_count"].is<int>())) {
    error="Invalid button field types"; return false;
  }
  if ((o.containsKey("icon") && (!validJsonText(o["icon"]) || !validIcon(o["icon"].as<std::string>()))) ||
      (o.containsKey("slot") && (!validJsonText(o["slot"]) || !validSlot(o["slot"].as<std::string>()))) ||
      (o.containsKey("favorite") && !o["favorite"].is<bool>())) { error="Invalid button appearance"; return false; }
  return true;
}
bool validRequestTypes(JsonObjectConst o, std::initializer_list<const char*> strings,
                       std::initializer_list<const char*> integers,
                       std::initializer_list<const char*> booleans, std::string& error) {
  if (o.isNull()) { error="Request must be an object"; return false; }
  for (const char* key : strings) if (o.containsKey(key) && !validJsonText(o[key])) {
    error=std::string(key)+" must be a string without NUL"; return false;
  }
  for (const char* key : integers) if (o.containsKey(key) && !o[key].is<uint32_t>()) {
    error=std::string(key)+" must be an unsigned integer"; return false;
  }
  for (const char* key : booleans) if (o.containsKey(key) && !o[key].is<bool>()) {
    error=std::string(key)+" must be boolean"; return false;
  }
  return true;
}
void buttonToJson(const Button& b, JsonObject o, bool includeFile) {
  o["id"] = b.id; o["name"] = b.name;
  if (includeFile) o["file"] = b.file;
  o["entity_type"] = b.entityType;
  o["send_mode"] = modeName(b.mode);
  o["repeat_count"] = b.repeatCount;
  o["icon"] = b.icon; o["slot"] = b.slot; o["favorite"] = b.favorite;
}
void indexToJson(const Remotes& remotes, JsonDocument& doc) {
  doc.clear();
  doc["version"] = 1;
  JsonArray array = doc.createNestedArray("remotes");
  for (const auto& remote : remotes) {
    JsonObject object = array.createNestedObject();
    remoteMetaToJson(remote, object);
    JsonArray buttons = object.createNestedArray("buttons");
    for (const auto& button : remote.buttons) buttonToJson(button, buttons.createNestedObject());
  }
}
size_t indexCapacity(const Remotes& remotes) {
  size_t capacity=512+JSON_OBJECT_SIZE(2)+JSON_ARRAY_SIZE(remotes.size());
  for (const auto& r : remotes) {
    capacity+=JSON_OBJECT_SIZE(6)+JSON_ARRAY_SIZE(r.buttons.size())+JSON_ARRAY_SIZE(r.macros.size())+r.id.size()+r.name.size()+r.layout.size()+3;
    for (const auto& b : r.buttons)
      capacity+=JSON_OBJECT_SIZE(9)+b.id.size()+b.name.size()+b.file.size()+b.entityType.size()+b.icon.size()+b.slot.size()+6;
    for (const auto& macro : r.macros) {
      capacity+=JSON_OBJECT_SIZE(3)+JSON_ARRAY_SIZE(macro.steps.size())+macro.id.size()+macro.name.size()+2;
      for (const auto& step : macro.steps) capacity+=JSON_OBJECT_SIZE(3)+step.remote.size()+step.button.size()+2;
    }
  }
  return capacity;
}
bool indexFromJson(JsonVariantConst json, Remotes& result, std::string& error) {
  Remotes next;
  if (!json["version"].is<int>() || json["version"].as<int>() != 1 || !json["remotes"].is<JsonArrayConst>()) {
    error = "Invalid database version/remotes"; return false;
  }
  std::set<std::string> ids, remoteNames, files;
  size_t buttonCount = 0;
  for (JsonObjectConst object : json["remotes"].as<JsonArrayConst>()) {
    if (!validRemoteFields(object,error)) return false;
    Remote remote;
    remote.id = object["id"].as<std::string>(); remote.name = object["name"].as<std::string>();
    remote.optimisticSwitch = object["optimistic_switch"] | false;
    remote.layout = object["layout"] | "grid";
    if (object.containsKey("macros")) for (JsonObjectConst json : object["macros"].as<JsonArrayConst>()) {
      Macro macro;
      if (remote.macros.size() >= Config::MAX_MACROS || !macroFromJson(json, macro, error)) return false;
      remote.macros.push_back(std::move(macro));
    }
    if (!validId(remote.id) || !validName(remote.name) || !ids.insert(remote.id).second ||
        !remoteNames.insert(remote.name).second || !object["buttons"].is<JsonArrayConst>() || next.size() >= Config::MAX_REMOTES) {
      error = "Invalid/duplicate remote or remote limit exceeded"; return false;
    }
    std::set<std::string> names;
    for (JsonObjectConst b : object["buttons"].as<JsonArrayConst>()) {
      if (!validButtonFields(b,error)) return false;
      Button button;
      button.id = b["id"].as<std::string>(); button.name = b["name"].as<std::string>();
      button.file = b["file"].as<std::string>(); button.entityType = b["entity_type"] | "button";
      const int count = b["repeat_count"] | 1;
      if (!validId(button.id) || !validName(button.name) || !validCodePath(button.file) ||
          !validEntityType(button.entityType) || !parseMode(b["send_mode"] | "AUTO", button.mode) ||
          count < 1 || count > 10 || !ids.insert(button.id).second || !names.insert(button.name).second ||
          !files.insert(button.file).second || ++buttonCount > Config::MAX_BUTTONS) {
        error = "Invalid/duplicate button or button limit exceeded"; return false;
      }
      button.repeatCount = count;
      button.icon = b["icon"] | ""; button.slot = b["slot"] | ""; button.favorite = b["favorite"] | false;
      remote.buttons.push_back(std::move(button));
    }
    next.push_back(std::move(remote));
  }
  if (!validMacros(next, error)) return false;
  result = std::move(next); error.clear(); return true;
}

void signalMetaToJson(const Signal& s, JsonObject o) {
  o["protocol"] = s.protocol; o["bits"] = s.bits;
  o["value"] = hex64(s.value); o["address"] = hex64(s.address); o["command"] = hex64(s.command);
  o["frequency"] = s.frequency; o["frequency_source"] = s.frequencySource;
  o["repeat"] = s.repeat; o["decoded_reliable"] = s.decodedReliable;
  o["raw_length"] = s.raw.size();
  if (!s.state.empty()) {
    std::string state;
    for (auto byte : s.state) {
      char hex[3]; snprintf(hex, sizeof(hex), "%02X", byte); state += hex;
    }
    o["state"] = state;
  }
}
namespace {
bool parseHex(const char* text, uint64_t& value) {
  if (!text || text[0] != '0' || text[1] != 'x' || !text[2]) return false;
  if (strlen(text) > 18) return false;
  for (const char* c = text + 2; *c; ++c) if (!std::isxdigit(static_cast<unsigned char>(*c))) return false;
  char* end = nullptr;
  errno = 0; value = strtoull(text + 2, &end, 16);
  return !errno && end && !*end;
}
}
bool signalMetaFromJson(JsonObjectConst o, Signal& s, std::string& error) {
  for (const char* key : {"protocol","value","address","command","frequency_source","state"}) {
    if (o.containsKey(key) && !validJsonText(o[key])) { error="IR metadata field must be a string without NUL"; return false; }
  }
  for (const char* key : {"repeat","decoded_reliable"}) {
    if (o.containsKey(key) && !o[key].is<bool>()) { error="IR metadata field must be boolean"; return false; }
  }
  s.protocol = o["protocol"].as<std::string>();
  uint64_t address, command;
  if (s.protocol.empty() || s.protocol.size() > 32 || !o["bits"].is<uint16_t>() ||
      !o["frequency"].is<uint32_t>() || !parseHex(o["value"], s.value) ||
      !parseHex(o["address"], address) || address > UINT32_MAX ||
      !parseHex(o["command"], command) || command > UINT32_MAX) {
    error = "Invalid IR metadata"; return false;
  }
  s.address = address; s.command = command; s.bits = o["bits"];
  s.frequency = o["frequency"]; s.repeat = o["repeat"] | false;
  s.frequencySource = o["frequency_source"] | "default";
  s.decodedReliable = o["decoded_reliable"] | false;
  const std::string state = o["state"] | "";
  if (state.size() > 256 || state.size() % 2) { error = "Invalid state bytes"; return false; }
  for (size_t i = 0; i < state.size(); i += 2) {
    if (!std::isxdigit(static_cast<unsigned char>(state[i])) || !std::isxdigit(static_cast<unsigned char>(state[i + 1]))) {
      error = "Invalid state hex"; return false;
    }
    s.state.push_back(strtoul(state.substr(i, 2).c_str(), nullptr, 16));
  }
  if ((!s.state.empty() && s.state.size() != (s.bits + 7U) / 8U) || (s.state.empty() && s.protocol != "RAW" && s.bits > 64)) {
    error = "State length does not match bit count"; return false;
  }
  s.decodedReliable=s.decodedReliable && canonicalNec(s);
  return true;
}

int JsonStream::peek() { if (next_ == -2) next_ = reader_.read(); return next_; }
int JsonStream::get() { int value = peek(); next_ = -2; return value; }
void JsonStream::whitespace() { while (peek() >= 0 && std::isspace(peek())) get(); }
bool JsonStream::expect(char character) { whitespace(); return get() == character; }
bool JsonStream::value(std::string& result, size_t maxLength) {
  whitespace(); result.clear();
  bool quoted = false, escaped = false;
  int depth = 0;
  while (peek() >= 0) {
    const char c = peek();
    if (!quoted && depth == 0 && (c == ',' || c == '}' || c == ']' || std::isspace(c))) break;
    get(); result += c;
    if (result.size() > maxLength) return false;
    if (quoted) {
      if (escaped) escaped = false;
      else if (c == '\\') escaped = true;
      else if (c == '"') quoted = false;
    } else {
      if (c == '"') quoted = true;
      else if (c == '[' || c == '{') ++depth;
      else if (c == ']' || c == '}') { if (--depth < 0) return false; }
    }
    if (!quoted && depth == 0 && (c == '"' || c == ']' || c == '}')) break;
  }
  return !result.empty() && !quoted && depth == 0;
}
bool JsonStream::key(std::string& result) {
  std::string encoded;
  if (!value(encoded, 80)) return false;
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, encoded) || !doc.is<const char*>()) return false;
  result = doc.as<std::string>();
  return expect(':');
}
bool JsonStream::raw(std::vector<uint16_t>& result, std::string& error) {
  if (!expect('[')) { error = "RAW must be an array"; return false; }
  result.clear(); whitespace();
  if (peek() == ']') { get(); return true; }
  while (true) {
    whitespace(); uint32_t number = 0; unsigned digits = 0; const bool leadingZero=peek()=='0';
    while (peek() >= '0' && peek() <= '9') {
      number = number * 10 + get() - '0';
      if (++digits > 5 || number > UINT16_MAX) { error = "RAW timing exceeds uint16"; return false; }
      if (leadingZero && digits>1) { error="RAW number has a leading zero"; return false; }
    }
    if (!digits || result.size() >= Config::MAX_RAW_SAMPLES) { error = "Invalid/too long RAW array"; return false; }
    if (result.size()==result.capacity() && !reserveRaw(result,std::min<size_t>(Config::MAX_RAW_SAMPLES,std::max<size_t>(8,result.capacity()*2)),error)) return false;
    result.push_back(number); whitespace(); const int separator = get();
    if (separator == ']') return true;
    if (separator != ',') { error = "Invalid RAW separator"; return false; }
  }
}
bool JsonStream::reserveRaw(std::vector<uint16_t>& result, size_t count, std::string& error) {
  if (count>Config::MAX_RAW_SAMPLES) { error="Too many RAW samples"; return false; }
  if (count<=result.capacity()) return true;
  if (!reader_.canAllocate(count*sizeof(uint16_t))) { error="Insufficient memory for RAW timings"; return false; }
  result.reserve(count); return true;
}
bool writeSignal(const Signal& s, Writer& w, std::string& error) {
  if (!validSignal(s, error)) return false;
  DynamicJsonDocument doc(2048); signalMetaToJson(s, doc.to<JsonObject>());
  std::string metadata; serializeJson(doc, metadata);
  if (doc.overflowed() || metadata.empty()) { error = "IR metadata allocation failed"; return false; }
  metadata.pop_back();
  if (!w.text(metadata) || !w.text(",\"raw\":[")) return false;
  for (size_t i = 0; i < s.raw.size(); ++i) {
    char number[8]; const int length = snprintf(number, sizeof(number), "%s%u", i ? "," : "", s.raw[i]);
    if (!w.write(number, length)) { error = "RAW write failed"; return false; }
  }
  return w.text("]}");
}
bool readSignal(JsonStream& stream, Signal& s, std::string& error) {
  s = Signal{};
  if (!stream.expect('{')) { error = "IR code must be an object"; return false; }
  std::string metadata = "{";
  std::set<std::string> fields;
  bool first = true, hasRaw = false;
  stream.whitespace();
  if (stream.peek() == '}') { error = "Empty IR code"; return false; }
  while (true) {
    std::string key;
    if (!stream.key(key) || !fields.insert(key).second) { error = "Invalid/duplicate IR key"; return false; }
    if (key == "raw") {
      {
        DynamicJsonDocument hint(4096);
        if (!deserializeJson(hint,metadata+"}") && hint["raw_length"].is<size_t>()) {
          const size_t count=hint["raw_length"].as<size_t>();
          if (!stream.reserveRaw(s.raw,count,error)) return false;
        }
      }
      hasRaw = true; if (!stream.raw(s.raw, error)) return false;
    } else {
      if (key!="protocol" && key!="bits" && key!="value" && key!="address" && key!="command" &&
          key!="frequency" && key!="frequency_source" && key!="repeat" && key!="decoded_reliable" &&
          key!="raw_length" && key!="state") { error="Unknown IR metadata field"; return false; }
      std::string value;
      if (!stream.value(value, 1024)) { error = "IR metadata too large/invalid"; return false; }
      StaticJsonDocument<128> keyDoc; keyDoc.set(key);
      std::string encoded; serializeJson(keyDoc, encoded);
      if (!first) metadata += ',';
      metadata += encoded + ':' + value; first = false;
      if (metadata.size() > 3072) { error = "IR metadata too large"; return false; }
    }
    stream.whitespace(); const int separator = stream.get();
    if (separator == '}') break;
    if (separator != ',') { error = "Invalid IR object separator"; return false; }
  }
  metadata += '}'; DynamicJsonDocument doc(4096);
  if (deserializeJson(doc, metadata) || !signalMetaFromJson(doc.as<JsonObjectConst>(), s, error)) {
    if (error.empty()) error = "Invalid IR JSON";
    return false;
  }
  if (!hasRaw || !doc["raw_length"].is<size_t>() || doc["raw_length"].as<size_t>() != s.raw.size()) {
    error = "Missing RAW or raw_length mismatch"; return false;
  }
  return validSignal(s, error);
}
bool readSignal(Reader& reader, Signal& s, std::string& error) {
  JsonStream stream(reader);
  if (!readSignal(stream, s, error)) return false;
  stream.whitespace();
  if (stream.peek() != -1) { error = "Trailing data after IR JSON"; return false; }
  return true;
}
}
