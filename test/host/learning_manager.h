#pragma once
#include "learning.h"
#include <ArduinoJson.h>
class LearningManager {
 public:
  const Bridge::LearningSession& session() const { return session_; }
  bool start(JsonObjectConst request,std::string& error) {
    const int count=request["confirmations"] | 3;
    return count>=1 && count<=3 && session_.start(count,20,millis(),error);
  }
  void cancel() { session_.cancel(); }
  void loop() { session_.tick(millis()); }
  void toJson(JsonObject object) const {
    object["status"]=session_.status(); object["captured"]=session_.count();
    object["required"]=session_.required(); object["message"]=session_.message();
  }
 private: Bridge::LearningSession session_;
};
extern LearningManager learningManager;
bool sendLearnedCode(const std::string&,const std::string&,std::string&);
