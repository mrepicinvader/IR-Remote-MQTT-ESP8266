#pragma once
#include "bridge_core.h"

namespace Bridge {
bool similarSignals(const Signal& left, const Signal& right, uint8_t tolerancePercent = 20);
enum class LearnResult { Ignored, Candidate, Confirmed, Rejected };
class LearningSession {
 public:
  bool start(uint8_t confirmations, uint8_t tolerance, uint32_t now, std::string& error);
  void cancel();
  void tick(uint32_t now);
  LearnResult observe(const Signal& signal, uint32_t now);
  void saved(bool success, const std::string& error);
  bool active() const { return status_ == "waiting" || status_ == "receiving"; }
  const std::string& status() const { return status_; }
  const std::string& message() const { return message_; }
  uint8_t count() const { return count_; }
  uint8_t required() const { return required_; }
  uint32_t remaining(uint32_t now) const;
 private:
  std::string status_ = "idle";
  std::string message_;
  uint8_t count_ = 0;
  uint8_t required_ = 1;
  uint8_t tolerance_ = 20;
  uint32_t started_ = 0;
  uint32_t lastSignal_ = 0;
  bool hasSignal_ = false;
  Signal candidate_;
};
}
