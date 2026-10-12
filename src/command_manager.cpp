#include "command_manager.h"
#include "storage.h"
#include "learning_manager.h"
#include "mqtt_manager.h"

CommandManager commandManager;
void CommandManager::begin() {
  queue_.onResult([](const Bridge::CommandResult& result) {
    StaticJsonDocument<768> doc; Bridge::commandResultToJson(result, doc.to<JsonObject>());
    std::string json; serializeJson(doc, json); mqttManager.publish("command/result", json, false);
  });
}
bool CommandManager::enqueue(JsonObjectConst request, const char* source, Bridge::CommandResult& result, std::string& error) {
  if (suspended_ || !storage.ready()) { error = "IR commands unavailable during update or database recovery"; return false; }
  if (learningManager.session().active()) { error = "Cancel learning before transmitting"; return false; }
  if (!Bridge::validRequestTypes(request, {"remote", "button", "macro", "request_id"}, {}, {}, error)) return false;
  auto* remote = storage.findRemote(request["remote"] | "");
  if (!remote) { error = "Remote not found"; return false; }
  std::vector<Bridge::CommandStep> steps;
  if (request.containsKey("macro")) {
    if (request.containsKey("button")) { error = "Specify a button or a scene"; return false; }
    auto* macro = storage.findMacro(remote->id, request["macro"] | "");
    if (!macro) { error = "Scene not found"; return false; }
    steps = macro->steps;
  } else {
    auto* button = storage.findButton(remote->id, request["button"] | "");
    if (!button) { error = "Button not found"; return false; }
    steps.push_back({remote->id, button->id, 0});
  }
  for (const auto& step : steps) if (!storage.findButton(step.remote, step.button)) { error = "Scene button no longer exists"; return false; }
  std::string id = request["request_id"] | "";
  if (!request.containsKey("request_id")) do { id = Storage::newId(); } while (queue_.find(id));
  return queue_.enqueue(id, steps, source, millis(), result, error);
}
void CommandManager::loop() {
  if (suspended_ || learningManager.session().active()) return;
  queue_.tick(millis(), [](const Bridge::CommandStep& step, std::string& error) {
    return sendLearnedCode(step.remote, step.button, error);
  }, [] { return millis(); });
}
void CommandManager::suspend(const std::string& reason) {
  suspended_ = true; queue_.failAll(reason, millis());
}
