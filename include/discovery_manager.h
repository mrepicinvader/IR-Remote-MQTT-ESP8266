#pragma once
#include "discovery.h"
#include "retry.h"

class DiscoveryManager {
 public:
  void begin();
  void loop();
  void schedule() { dirty_=true; }
  const std::string& status() const { return status_; }
 private:
  Bridge::DiscoveryContext context() const;
  bool saveHistory(const std::vector<std::string>& history, std::string& error);
  std::vector<std::string> history_;
  bool dirty_=true, publishing_=false;
  size_t cursor_=0;
  Bridge::RetryGate retry_;
  std::string status_="pending";
};
extern DiscoveryManager discoveryManager;
