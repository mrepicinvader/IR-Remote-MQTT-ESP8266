#pragma once
#include "learning.h"
#include "storage.h"

class LearningManager {
 public:
  bool start(const std::string& remote, const std::string& name, const std::string& replaceId,
             uint8_t confirmations, uint8_t tolerance, const Bridge::Button& settings, std::string& error);
  bool start(JsonObjectConst request, std::string& error);
  void cancel() { session_.cancel(); }
  void loop() { session_.tick(millis()); }
  void receive(const Bridge::Signal& signal);
  bool useLatest(const std::string& remote, const std::string& name, uint32_t sequence, std::string& error);
  const Bridge::LearningSession& session() const { return session_; }
  void toJson(JsonObject object) const;
  bool sendLearnedCode(const std::string& remote, const std::string& button, std::string& error);
 private:
  Bridge::LearningSession session_;
  std::string remote_;
  std::string name_;
  std::string replaceId_;
  Bridge::Button settings_;
};
extern LearningManager learningManager;
bool sendLearnedCode(const std::string& remote, const std::string& button, std::string& error);
