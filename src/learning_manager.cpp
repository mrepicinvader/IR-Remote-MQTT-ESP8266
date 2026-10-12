#include "learning_manager.h"
#include "ir_manager.h"
#include "logging.h"
#include "system_status.h"
#include "command_manager.h"

LearningManager learningManager;

bool LearningManager::start(JsonObjectConst request, std::string& error) {
  if (commandManager.busy() || commandManager.suspended()) { error="Wait for queued sends or update to finish"; return false; }
  if (!Bridge::validRequestTypes(request,{"remote","name","replace_id","entity_type","send_mode","mode"},
                                 {"confirmations","tolerance_percent","repeat_count"},{},error)) return false;
  if (!Bridge::validName(request["name"] | "")) { error="Invalid button name"; return false; }
  if (session_.active()) { error="Learning already active"; return false; }
  const std::string mode=request["mode"] | "fast";
  const uint32_t confirmations=request["confirmations"] | (mode=="reliable"?3U:1U);
  const uint32_t tolerance=request["tolerance_percent"] | 20U;
  const uint32_t requestedRepeats=request["repeat_count"] | 1U;
  Bridge::SendMode requestedMode;
  if ((mode!="fast" && mode!="reliable") || confirmations<1 || confirmations>3 ||
      (mode=="reliable" && confirmations<2) || tolerance<15 || tolerance>25 || requestedRepeats<1 || requestedRepeats>10 ||
      !Bridge::validEntityType(request["entity_type"] | "button") || !Bridge::parseMode(request["send_mode"] | "AUTO",requestedMode)) {
    error="Invalid learning mode, confirmations or button settings"; return false;
  }
  std::string remote = request["remote"] | "";
  const std::string replace = request["replace_id"] | "";
  if (remote.empty()) {
    if (storage.remotes().empty() && !replace.empty()) { error="Cannot relearn without an existing remote"; return false; }
    if (storage.remotes().empty() && !storage.createRemote("Remote", error)) return false;
    if (storage.remotes().size() != 1) { error = "Select remote when several remotes exist"; return false; }
    remote = storage.remotes().front().id;
  }
  Bridge::Button settings;
  if (auto* old = storage.findButton(remote, replace)) settings = *old;
  settings.entityType = request["entity_type"] | settings.entityType.c_str();
  if (!Bridge::parseMode(request["send_mode"] | Bridge::modeName(settings.mode), settings.mode)) { error = "Invalid send_mode"; return false; }
  const uint32_t repeats = request["repeat_count"] | static_cast<uint32_t>(settings.repeatCount);
  if (confirmations < 1 || confirmations > 3 || tolerance < 15 || tolerance > 25 || repeats < 1 || repeats > 10) {
    error = "Invalid confirmations, tolerance or repeat count"; return false;
  }
  settings.repeatCount = repeats;
  return start(remote, request["name"] | "", replace, confirmations, tolerance, settings, error);
}

bool LearningManager::start(const std::string& remote, const std::string& name, const std::string& replace,
                             uint8_t confirmations, uint8_t tolerance, const Bridge::Button& settings, std::string& error) {
  if (commandManager.busy() || commandManager.suspended()) { error="Wait for queued sends or update to finish"; return false; }
  auto* r = storage.findRemote(remote);
  auto* old = replace.empty() ? nullptr : storage.findButton(remote, replace);
  if (!storage.ready() || !r || !Bridge::validName(name) || (!replace.empty() && !old) ||
      (storage.findButton(remote, name) && storage.findButton(remote, name) != old) ||
      (!old && storage.buttonCount() >= Config::MAX_BUTTONS)) {
    error = "Invalid target remote/button or duplicate name"; return false;
  }
  if (!Bridge::validEntityType(settings.entityType) || settings.repeatCount < 1 || settings.repeatCount > 10) {
    error = "Invalid entity type/repeat count"; return false;
  }
  if (!session_.start(confirmations, tolerance, millis(), error)) return false;
  remote_ = r->id; name_ = name; replaceId_ = old ? old->id : ""; settings_ = settings;
  LOGI("LEARN", "Waiting for %s/%s (%u confirmations)", r->name.c_str(), name.c_str(), confirmations);
  return true;
}
void LearningManager::receive(const Bridge::Signal& signal) {
  if (!session_.active()) return;
  if (session_.required() > 1 && !session_.count() && ESP.getMaxFreeBlockSize() < signal.raw.size() * 2 + 4096) {
    session_.saved(false, "Insufficient heap for reliable learning; use Fast Learn for this long signal"); return;
  }
  const auto result = session_.observe(signal, millis());
  if (result == Bridge::LearnResult::Candidate) LOGI("LEARN", "Candidate %u/%u captured", session_.count(), session_.required());
  if (result != Bridge::LearnResult::Confirmed) return;
  LOGI("LEARN", "Signal confirmed");
  std::string error;
  const bool success = storage.saveButton(remote_, name_, signal, &settings_, replaceId_, error);
  session_.saved(success, error);
  if (!success) LOGE("LEARN", "%s", error.c_str());
}
bool LearningManager::useLatest(const std::string& remote, const std::string& name, uint32_t sequence, std::string& error) {
  if (session_.active()) { error = "Cancel learning first"; return false; }
  if (!irManager.latest() || sequence != irManager.sequence()) { error = "Signal changed; refresh monitor before saving"; return false; }
  return storage.saveButton(remote, name, *irManager.latest(), nullptr, "", error);
}
void LearningManager::toJson(JsonObject object) const {
  object["status"] = session_.status(); object["message"] = session_.message();
  object["remote"] = remote_; object["name"] = name_;
  object["captured"] = session_.count(); object["required"] = session_.required();
  object["remaining_ms"] = session_.remaining(millis());
}
bool LearningManager::sendLearnedCode(const std::string& remote, const std::string& name, std::string& error) {
  if (session_.active()) { error = "Cancel learning before transmitting"; return false; }
  auto* b = storage.findButton(remote, name);
  if (!storage.ready() || !b) { error = "Button not found or database unavailable"; return false; }
  irManager.discardLatest();
  Bridge::Signal signal;
  if (!storage.loadSignal(*b, signal, error)) return false;
  if (!irManager.sendSignal(signal, b->mode, b->repeatCount, error)) return false;
  b->lastTransmitted = uptimeMillis();
  LOGI("IR-TX", "Sent %s/%s", remote.c_str(), name.c_str()); return true;
}
bool sendLearnedCode(const std::string& remote, const std::string& button, std::string& error) {
  return learningManager.sendLearnedCode(remote, button, error);
}
