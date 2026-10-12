#include "firmware_check.h"
#include <algorithm>
#include <cstring>

namespace Bridge {
bool validMd5(const std::string& digest) {
  return digest.size() == 32 && std::all_of(digest.begin(), digest.end(), [](unsigned char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
  });
}
bool FirmwareCheck::feed(const uint8_t* bytes, size_t length, std::string& error) {
  if (expected_ < 64 || expected_ > limit_ || length > expected_ - received_) { error = "Firmware size invalid or exceeds available update space"; return false; }
  if (!received_ && (length < 4 || bytes[0] != 0xe9 || !bytes[1] || bytes[1] > 16 || bytes[2] > 3 || (bytes[3] >> 4) != 4)) {
    error = "Expected an ESP8266 firmware image for 4 MB flash"; return false;
  }
  const size_t targetLength = std::strlen(Config::FIRMWARE_TARGET);
  for (size_t i = 0; i < length && !targetFound_; ++i) {
    if (bytes[i] == static_cast<uint8_t>(Config::FIRMWARE_TARGET[matching_])) ++matching_;
    else matching_ = bytes[i] == static_cast<uint8_t>(Config::FIRMWARE_TARGET[0]) ? 1 : 0;
    if (matching_ == targetLength) targetFound_ = true;
  }
  received_ += length; return true;
}
bool FirmwareCheck::finish(std::string& error) const {
  if (received_ != expected_) { error = "Firmware upload incomplete"; return false; }
  if (!targetFound_) { error = "Image does not identify the ESP IR Bridge 4 MB / 1 MB filesystem target"; return false; }
  return true;
}
}
