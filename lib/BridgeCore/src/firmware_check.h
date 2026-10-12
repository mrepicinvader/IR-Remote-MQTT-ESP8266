#pragma once
#include "bridge_core.h"
#include <cstddef>

namespace Bridge {
bool validMd5(const std::string& digest);
class FirmwareCheck {
 public:
  FirmwareCheck(size_t expected, size_t limit) : expected_(expected), limit_(limit) {}
  bool feed(const uint8_t* bytes, size_t length, std::string& error);
  bool finish(std::string& error) const;
  size_t received() const { return received_; }
 private:
  size_t expected_, limit_, received_ = 0, matching_ = 0;
  bool targetFound_ = false;
};
}
