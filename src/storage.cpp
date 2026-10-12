#include "storage.h"
#include <algorithm>
#include <set>
#include <user_interface.h>
#include "logging.h"
#include "backup.h"

Storage storage;
namespace {
constexpr char INDEX[] = "/data/remotes.json";
constexpr char CONFIG[] = "/data/config.json";
bool memoryAvailable(size_t bytes, std::string& error) {
  if (ESP.getMaxFreeBlockSize() < bytes + 1024 || ESP.getFreeHeap() < bytes + 4096) {
    error = "Not enough free heap; shorten signal or reduce configured limits"; return false;
  }
  return true;
}
}
bool LittleFileOps::read(const std::string& path, std::string& content, size_t limit) {
  File file = LittleFS.open(path.c_str(), "r");
  if (!file || file.size() > limit || ESP.getMaxFreeBlockSize() < file.size() + 1024) return false;
  content.clear(); content.reserve(file.size());
  while (file.available()) { char bytes[256]; const size_t n = file.readBytes(bytes, sizeof(bytes)); content.append(bytes, n); yield(); }
  return true;
}
bool LittleFileOps::write(const std::string& path, const std::string& content) {
  File file = LittleFS.open(path.c_str(), "w");
  if (!file) return false;
  const bool result = file.write(reinterpret_cast<const uint8_t*>(content.data()), content.size()) == content.size();
  file.flush(); file.close(); return result;
}
bool LittleFileOps::replace(const std::string& from, const std::string& to) { return LittleFS.rename(from.c_str(), to.c_str()); }
bool LittleFileOps::exists(const std::string& path) { return LittleFS.exists(path.c_str()); }
bool LittleFileOps::remove(const std::string& path) { return LittleFS.remove(path.c_str()); }
std::string Storage::newId() { char id[9]; snprintf(id, sizeof(id), "%08lx", static_cast<unsigned long>(os_random())); return id; }

bool Storage::begin() {
  ready_=false; mounted_=false; error_.clear(); remotes_.clear();
  LittleFS.setConfig(LittleFSConfig(false));  // Never format on a mount error.
  mounted_ = LittleFS.begin();
  if (!mounted_) { error_ = "LittleFS mount failed; upload filesystem, restore backup before replacing existing FS"; LOGE("FS", "%s", error_.c_str()); return false; }
  LittleFS.mkdir("/data"); LittleFS.mkdir("/data/codes");
  if (files_.exists(CONFIG)) {
    std::string content;
    if (!files_.read(CONFIG, content, 4096)) { error_ = "Cannot read configuration"; return false; }
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, content) || !configFromJson(doc.as<JsonObjectConst>(), config_, error_)) {
      if (error_.empty()) error_ = "Invalid configuration JSON";
      LOGE("FS", "%s; original file retained", error_.c_str()); return false;
    }
  }
  if (files_.exists(INDEX)) {
    std::string content;
    if (!files_.read(INDEX, content, 16000) || !validateIndex(content, remotes_, error_)) {
      if (error_.empty()) error_ = "Cannot read database";
      LOGE("FS", "%s; original database retained", error_.c_str()); return false;
    }
    for (const auto& r : remotes_) for (const auto& b : r.buttons) {
      Bridge::Signal signal;
      if (!loadSignal(b, signal, error_)) { LOGE("FS", "Invalid code %s: %s", b.id.c_str(), error_.c_str()); return false; }
    }
  }
  ready_ = true;
  cleanupCodes();
  LOGI("FS", "Loaded %u remotes, %u buttons", static_cast<unsigned>(remotes_.size()), static_cast<unsigned>(buttonCount()));
  return true;
}
Bridge::Remote* Storage::findRemote(const std::string& name) {
  for (auto& remote : remotes_) if (remote.id == name || remote.name == name) return &remote;
  return nullptr;
}
Bridge::Button* Storage::findButton(const std::string& remote, const std::string& name) {
  auto* r = findRemote(remote); if (!r) return nullptr;
  for (auto& b : r->buttons) if (b.id == name || b.name == name) return &b;
  return nullptr;
}
Bridge::Macro* Storage::findMacro(const std::string& remote, const std::string& name) {
  auto* r = findRemote(remote); if (!r) return nullptr;
  for (auto& macro : r->macros) if (macro.id == name || macro.name == name) return &macro;
  return nullptr;
}
bool Storage::saveMacro(const std::string& remote, JsonObjectConst request, std::string& error) {
  auto* owner = findRemote(remote);
  if (!ready_ || !owner) { error = "Remote not found or database unavailable"; return false; }
  Bridge::Macro macro;
  if (!Bridge::macroFromJson(request, macro, error)) return false;
  if (macro.id.empty()) macro.id = newId();
  else if (!findMacro(owner->id, macro.id)) { error = "Scene not found"; return false; }
  auto next = remotes_;
  for (auto& r : next) if (r.id == owner->id) {
    auto old = std::find_if(r.macros.begin(), r.macros.end(), [&](const Bridge::Macro& item) { return item.id == macro.id; });
    if (old == r.macros.end()) r.macros.push_back(macro); else *old = macro;
  }
  if (!Bridge::validMacros(next, error)) return false;
  return commitIndex(std::move(next), error);
}
bool Storage::deleteMacro(const std::string& remote, const std::string& name, std::string& error) {
  auto* macro = findMacro(remote, name);
  if (!ready_ || !macro) { error = "Scene not found"; return false; }
  const auto id = macro->id; auto next = remotes_;
  for (auto& r : next) r.macros.erase(std::remove_if(r.macros.begin(), r.macros.end(), [&](const Bridge::Macro& item) { return item.id == id; }), r.macros.end());
  return commitIndex(std::move(next), error);
}
bool Storage::updateLayout(const std::string& remote, JsonObjectConst request, std::string& error) {
  auto* owner = findRemote(remote);
  if (!ready_ || !owner) { error = "Remote not found"; return false; }
  if (!Bridge::validRequestTypes(request, {"layout"}, {}, {}, error)) return false;
  const std::string layout = request["layout"] | owner->layout.c_str();
  if (!Bridge::validLayout(layout)) { error = "Unknown remote layout"; return false; }
  auto next = remotes_;
  for (auto& r : next) if (r.id == owner->id) {
    r.layout = layout;
    if (request.containsKey("button_order") && !Bridge::reorderButtons(r, request["button_order"].as<JsonArrayConst>(), error)) return false;
  }
  return commitIndex(std::move(next), error);
}
size_t Storage::buttonCount() const { size_t count = 0; for (const auto& r : remotes_) count += r.buttons.size(); return count; }
bool Storage::validateIndex(const std::string& content, Bridge::Remotes& next, std::string& error) {
  const size_t capacity=std::min<size_t>(Config::INDEX_JSON_CAPACITY,content.size()*2+1024);
  if (!memoryAvailable(capacity, error)) return false;
  DynamicJsonDocument doc(capacity);
  if (deserializeJson(doc, content) || !Bridge::indexFromJson(doc.as<JsonVariantConst>(), next, error)) {
    if (error.empty()) error = "Invalid database JSON";
    return false;
  }
  for (const auto& r : next) for (const auto& b : r.buttons) if (!files_.exists(b.file)) {
    error = "Database refers to missing IR code"; return false;
  }
  return true;
}
bool Storage::commitIndex(Bridge::Remotes next, std::string& error) {
  if (!mounted_) { error = "LittleFS unavailable"; return false; }
  if (!Bridge::validateRemotes(next, error)) return false;
  std::string content;
  {
    const size_t capacity=Bridge::indexCapacity(next);
    if (capacity>Config::INDEX_JSON_CAPACITY) { error="Database exceeds JSON capacity"; return false; }
    if (!memoryAvailable(capacity, error)) return false;
    DynamicJsonDocument doc(capacity); Bridge::indexToJson(next, doc);
    if (doc.overflowed()) { error = "Database exceeds JSON capacity"; return false; }
    serializeJson(doc, content);
  }
  if (content.size() > 16000) { error = "Database exceeds index limit"; return false; }
  if (!Bridge::atomicWrite(files_, INDEX, content, [&](const std::string& text, std::string& e) {
        Bridge::Remotes verified; return validateIndex(text, verified, e);
      }, error)) return false;
  remotes_ = std::move(next); ready_ = true;
  if (changed_) changed_();
  cleanupCodes(); return true;
}
void Storage::cleanupCodes() {
  std::set<std::string> used;
  for (const auto& r : remotes_) for (const auto& b : r.buttons) used.insert(b.file);
  Dir dir = LittleFS.openDir("/data/codes");
  std::vector<std::string> obsolete;
  while (dir.next()) {
    std::string path = dir.fileName().c_str();
    if (path.substr(0, 1) != "/") path = "/data/codes/" + path;
    if (!used.count(path)) obsolete.push_back(path);
    yield();
  }
  for (const auto& path : obsolete) files_.remove(path);
}
bool Storage::createRemote(const std::string& name, std::string& error, const std::string& layout) {
  if (!ready_) { error = "Database unavailable"; return false; }
  if (!Bridge::validName(name) || !Bridge::validLayout(layout) || findRemote(name) || remotes_.size() >= Config::MAX_REMOTES) { error = "Invalid/duplicate remote, layout or remote limit"; return false; }
  auto next = remotes_; Bridge::Remote r; r.id = newId(); r.name = name; r.layout = layout; next.push_back(std::move(r));
  return commitIndex(std::move(next), error);
}
bool Storage::renameRemote(const std::string& remote, const std::string& name, bool optimistic, std::string& error, const std::string& layout) {
  auto* r = findRemote(remote);
  if (!layout.empty() && !Bridge::validLayout(layout)) { error = "Unknown remote layout"; return false; }
  if (!ready_ || !r || !Bridge::validName(name) || (findRemote(name) && findRemote(name) != r)) { error = "Invalid remote/name"; return false; }
  const auto id = r->id; auto next = remotes_;
  for (auto& item : next) if (item.id == id) { item.name = name; item.optimisticSwitch = optimistic; if (!layout.empty()) item.layout = layout; }
  return commitIndex(std::move(next), error);
}
bool Storage::deleteRemote(const std::string& remote, std::string& error) {
  auto* r = findRemote(remote); if (!ready_ || !r) { error = "Remote not found"; return false; }
  const auto id = r->id; auto next = remotes_;
  for (const auto& owner : remotes_) if (owner.id != id) for (const auto& macro : owner.macros) for (const auto& step : macro.steps)
    if (step.remote == id) { error = "Remote is used by scene: " + macro.name; return false; }
  next.erase(std::remove_if(next.begin(), next.end(), [&](const Bridge::Remote& item) { return item.id == id; }), next.end());
  return commitIndex(std::move(next), error);
}
bool Storage::deleteButton(const std::string& remote, const std::string& button, std::string& error) {
  auto* b = findButton(remote, button); if (!ready_ || !b) { error = "Button not found"; return false; }
  const auto id = b->id; auto next = remotes_;
  const auto remoteId = findRemote(remote)->id;
  for (const auto& owner : remotes_) for (const auto& macro : owner.macros) for (const auto& step : macro.steps)
    if (step.remote == remoteId && step.button == id) { error = "Button is used by scene: " + macro.name; return false; }
  Bridge::eraseButton(next,remoteId,id);
  return commitIndex(std::move(next), error);
}
bool Storage::loadSignal(const Bridge::Button& button, Bridge::Signal& signal, std::string& error) {
  if (!mounted_ || !Bridge::validCodePath(button.file)) { error = "Invalid IR file path"; return false; }
  File file = LittleFS.open(button.file.c_str(), "r");
  if (!file || file.size() > 60000) { error="IR code file unavailable/too large"; return false; }
  StaticJsonDocument<64> filter; filter["raw_length"]=true;
  StaticJsonDocument<128> metadata;
  if (deserializeJson(metadata,file,DeserializationOption::Filter(filter)) || !metadata["raw_length"].is<size_t>()) { error="Invalid IR raw_length"; return false; }
  const size_t samples=metadata["raw_length"].as<size_t>();
  if (samples>Config::MAX_RAW_SAMPLES || !memoryAvailable(samples*2+8192,error)) return false;
  file.seek(0);
  FileReader reader(file); return Bridge::readSignal(reader, signal, error);
}
bool Storage::writeCode(const Bridge::Signal& signal, std::string& path, std::string& error) {
  if (!mounted_ || !Bridge::validSignal(signal, error)) return false;
  do { path = "/data/codes/c" + newId() + ".json"; } while (files_.exists(path));
  const std::string temp = path + ".tmp";
  {
    File file = LittleFS.open(temp.c_str(), "w");
    if (!file) { error = "Cannot open IR temporary file"; return false; }
    FileWriter writer(file);
    const bool success = Bridge::writeSignal(signal, writer, error);
    file.flush(); file.close();
    if (!success) { files_.remove(temp); if (error.empty()) error = "IR write failed"; return false; }
  }
  {
    File file = LittleFS.open(temp.c_str(), "r");
    class CompareWriter : public Bridge::Writer {
     public:
      explicit CompareWriter(File& file):file_(file) {}
      bool write(const char* bytes,size_t length) override {
        for (size_t i=0;i<length;++i) if (file_.read()!=static_cast<unsigned char>(bytes[i])) return false;
        if (!(++calls_%128)) yield();
        return true;
      }
     private: File& file_; size_t calls_=0;
    } compare(file);
    // Validate complete byte equality with the canonical serialization of the
    // already validated original, without a second long timing vector.
    if (!file || !memoryAvailable(4096,error) || !Bridge::writeSignal(signal,compare,error) || file.available()) {
      files_.remove(temp); if (error.empty()) error = "IR read-back mismatch"; return false;
    }
  }
  if (!files_.replace(temp, path)) { error = "IR commit failed"; return false; }
  return true;
}
bool Storage::saveButton(const std::string& remote, const std::string& name, const Bridge::Signal& signal,
                         const Bridge::Button* settings, const std::string& replaceId, std::string& error) {
  auto* r = findRemote(remote); auto* old = replaceId.empty() ? nullptr : findButton(remote, replaceId);
  if (!ready_ || !r || !Bridge::validName(name) || (!replaceId.empty() && !old) ||
      (!old && buttonCount() >= Config::MAX_BUTTONS) || (findButton(remote, name) && findButton(remote, name) != old)) {
    error = "Invalid/duplicate button, remote missing or button limit"; return false;
  }
  Bridge::Button b = settings ? *settings : old ? *old : Bridge::Button{};
  b.id = old ? old->id : newId(); b.name = name;
  if (!Bridge::validEntityType(b.entityType) || b.repeatCount < 1 || b.repeatCount > 10) { error = "Invalid button settings"; return false; }
  if (!writeCode(signal, b.file, error)) return false;
  const auto remoteId = r->id; auto next = remotes_;
  for (auto& item : next) if (item.id == remoteId) {
    auto found = std::find_if(item.buttons.begin(), item.buttons.end(), [&](const Bridge::Button& x) { return x.id == b.id; });
    if (found == item.buttons.end()) item.buttons.push_back(b); else *found = b;
  }
  const bool success = commitIndex(std::move(next), error);
  if (success) LOGI("FS", "Saved %s/%s", remote.c_str(), name.c_str());
  return success;
}
bool Storage::updateButton(const std::string& remote, const std::string& button, const std::string& name,
                           const std::string& entity, Bridge::SendMode mode, uint8_t repeats, uint32_t frequency,
                           const std::string& icon, const std::string& slot, bool favorite, std::string& error) {
  auto* b = findButton(remote, button); if (!b) { error = "Button not found"; return false; }
  Bridge::Button settings = *b; settings.entityType = entity; settings.mode = mode; settings.repeatCount = repeats;
  settings.icon = icon; settings.slot = slot; settings.favorite = favorite;
  if (!Bridge::validName(name) || !Bridge::validIcon(icon) || !Bridge::validSlot(slot) || !Bridge::validEntityType(entity)) { error="Invalid button settings"; return false; }
  settings.name = name; auto next = remotes_;
  for (auto& r : next) for (auto& item : r.buttons) if (item.id == settings.id) item = settings;
  if (!Bridge::validateRemotes(next,error)) return false;
  if (!frequency) return commitIndex(std::move(next), error);
  Bridge::Signal signal; if (!loadSignal(*b, signal, error)) return false;
  if (frequency == signal.frequency) return commitIndex(std::move(next), error);
  signal.frequency = frequency; signal.frequencySource = "manual";
  return saveButton(remote, name, signal, &settings, settings.id, error);
}

void Storage::configToJson(const RuntimeConfig& c, JsonObject o, bool passwords) { Bridge::configToJson(c,o,passwords); }
bool Storage::configFromJson(JsonObjectConst o, RuntimeConfig& c, std::string& error, bool requireWifi) { return Bridge::configFromJson(o,c,error,requireWifi); }
bool Storage::saveConfig(const RuntimeConfig& config, std::string& error) {
  if (!mounted_) { error = "LittleFS unavailable"; return false; }
  std::string content;
  { DynamicJsonDocument doc(4096); configToJson(config, doc.to<JsonObject>(), true); serializeJson(doc, content); }
  if (!Bridge::atomicWrite(files_, CONFIG, content, [](const std::string& text, std::string& e) {
    DynamicJsonDocument doc(4096); RuntimeConfig verified;
    return !deserializeJson(doc, text) && configFromJson(doc.as<JsonObjectConst>(), verified, e);
  }, error)) return false;
  config_ = config; return true;
}

bool Storage::exportBackup(const char* path,std::string& error) {
  if (!ready_) { error="Database unavailable"; return false; }
  File file=LittleFS.open(path,"w"); if (!file) { error="Cannot open backup file"; return false; }
  FileWriter writer(file); bool success=true;
  success &= writer.text("{\"version\":1,\"firmware_version\":\""+std::string(Config::VERSION)+"\",\"configuration\":");
  {
    DynamicJsonDocument doc(2048); configToJson(config_,doc.to<JsonObject>(),false);
    std::string text; serializeJson(doc,text); success &= !doc.overflowed() && writer.text(text);
  }
  success &= writer.text(",\"remotes\":[");
  for (size_t r=0;success && r<remotes_.size();++r) {
    if (r) success &= writer.text(",");
    DynamicJsonDocument metadata(512); metadata["id"]=remotes_[r].id; metadata["name"]=remotes_[r].name;
    metadata["optimistic_switch"]=remotes_[r].optimisticSwitch;
    metadata["layout"]=remotes_[r].layout;
    if (metadata.overflowed()) { error="Insufficient heap for remote backup metadata"; success=false; break; }
    std::string text; serializeJson(metadata,text); text.pop_back(); success &= writer.text(text+",\"buttons\":[");
    for (size_t b=0;success && b<remotes_[r].buttons.size();++b) {
      if (b) success &= writer.text(",");
      DynamicJsonDocument buttonDoc(1024); Bridge::buttonToJson(remotes_[r].buttons[b],buttonDoc.to<JsonObject>(),false);
      if (buttonDoc.overflowed()) { error="Insufficient heap for button backup metadata"; success=false; break; }
      std::string buttonText; serializeJson(buttonDoc,buttonText); buttonText.pop_back();
      success &= writer.text(buttonText+",\"signal\":");
      File code=LittleFS.open(remotes_[r].buttons[b].file.c_str(),"r");
      if (!code) { error="Missing code in backup"; success=false; break; }
      while (code.available() && success) { char bytes[512]; const size_t n=code.readBytes(bytes,sizeof(bytes)); success &= writer.write(bytes,n); }
      success &= writer.text("}");
    }
    success &= writer.text("],\"macros\":[");
    for (size_t m=0; success && m<remotes_[r].macros.size(); ++m) {
      if (m) success &= writer.text(",");
      DynamicJsonDocument macroDoc(4096); Bridge::macroToJson(remotes_[r].macros[m], macroDoc.to<JsonObject>());
      std::string json; serializeJson(macroDoc, json);
      success &= !macroDoc.overflowed() && writer.text(json);
    }
    success &= writer.text("]}");
  }
  success &= writer.text("]}"); file.flush(); file.close();
  if (!success) { LittleFS.remove(path); if (error.empty()) error="Backup write failed (LittleFS full?)"; }
  return success;
}
bool Storage::importBackup(const char* path,std::string& error) {
  if (!mounted_) { error="LittleFS unavailable"; return false; }
  File file=LittleFS.open(path,"r");
  if (!file || file.size()>Config::MAX_BACKUP_BYTES) { error="Backup unavailable/too large"; return false; }
  FileReader reader(file); Bridge::Remotes next; std::string configText;
  bool success=Bridge::readBackup(reader,next,configText,[this](const Bridge::Signal& signal,std::string& code,std::string& e) {
    return writeCode(signal,code,e);
  },error);
  file.close();
  RuntimeConfig config=config_;
  if (success) {
    DynamicJsonDocument doc(4096);
    success=!deserializeJson(doc,configText);
    // Preserve local authentication and passwords when moving a password-free backup.
    doc.remove("web_auth"); doc.remove("web_user");
    if (success) success=configFromJson(doc.as<JsonObjectConst>(),config,error);
  }
  // The complete new database is committed in one atomic index replacement.
  if (success) success=commitIndex(std::move(next),error);
  if (success && !saveConfig(config,error)) {
    error="IR database imported, configuration write failed: "+error; return false;
  }
  if (!success && ready_) cleanupCodes();
  return success;
}
