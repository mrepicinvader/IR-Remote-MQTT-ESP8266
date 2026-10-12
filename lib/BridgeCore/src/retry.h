#pragma once
#include <cstdint>
namespace Bridge {
class RetryGate {
 public:
  bool due(uint32_t now) const { return !scheduled_ || now - last_ >= interval_; }
  void schedule(uint32_t now, uint32_t interval) { last_ = now; interval_ = interval; scheduled_ = true; }
  void reset() { scheduled_ = false; }
 private:
  uint32_t last_ = 0, interval_ = 0;
  bool scheduled_ = false;
};
}
