#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../../include/config.h"

namespace Bridge {
enum class SendMode { Auto, Decoded, Raw };
struct Signal {
  std::string protocol = "RAW";
  uint16_t bits = 0;
  uint64_t value = 0;
  uint32_t address = 0;
  uint32_t command = 0;
  uint32_t frequency = Config::DEFAULT_FREQUENCY_HZ;
  std::string frequencySource = "default";
  bool repeat = false;
  bool overflow = false;
  bool decodedReliable = false;
  std::vector<uint8_t> state;  // Complete A/C state; never read the union as value.
  std::vector<uint16_t> raw;   // Microseconds, starts with mark; no leading idle gap.
};

std::string hex64(uint64_t value);
bool validSignal(const Signal& signal, std::string& error);
void appendTiming(std::vector<uint16_t>& raw, uint32_t microseconds);
uint64_t durationUs(const Signal& signal);
bool preferDecoded(const Signal& signal, SendMode mode);
bool canonicalNec(const Signal& signal);
}
