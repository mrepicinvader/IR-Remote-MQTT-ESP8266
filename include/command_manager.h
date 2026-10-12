#pragma once
#include <Arduino.h>
#include "command_queue.h"

class CommandManager {
 public:
  void begin();
  void loop();
  bool enqueue(JsonObjectConst request, const char* source, Bridge::CommandResult& result, std::string& error);
  bool busy() const { return queue_.pending() != 0; }
  bool suspended() const { return suspended_; }
  void suspend(const std::string& reason);
  void resume() { suspended_ = false; }
  bool cancel(const std::string& id, std::string& error) { return queue_.cancel(id, millis(), error); }
  const Bridge::CommandQueue& queue() const { return queue_; }
 private:
  Bridge::CommandQueue queue_;
  bool suspended_ = false;
};
extern CommandManager commandManager;
