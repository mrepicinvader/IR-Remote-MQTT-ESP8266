#include "bridge_core.h"
#include <cstdio>
#include <algorithm>

namespace Bridge {
bool canonicalNec(const Signal& signal) {
  if (signal.protocol!="NEC" || signal.bits!=32 || signal.value>UINT32_MAX || signal.raw.size()!=67 || !signal.state.empty()) return false;
  if (((signal.value>>8)&255)!=(~signal.value&255)) return false;
  auto match=[](uint32_t actual,uint32_t nominal) {
    const uint32_t delta=actual>nominal?actual-nominal:nominal-actual;
    return delta<=std::max<uint32_t>(80,nominal/4);
  };
  if (!match(signal.raw[0],9000) || !match(signal.raw[1],4500) || !match(signal.raw.back(),560)) return false;
  for (size_t bit=0;bit<32;++bit) {
    if (!match(signal.raw[2+bit*2],560) || !match(signal.raw[3+bit*2],signal.value&(1ULL<<(31-bit))?1690:560)) return false;
  }
  return true;
}
bool preferDecoded(const Signal& signal, SendMode mode) {
  return mode == SendMode::Decoded ||
      (mode == SendMode::Auto && signal.decodedReliable && !signal.repeat &&
       signal.frequencySource != "manual" && signal.frequency==38000 && canonicalNec(signal));
}
std::string hex64(uint64_t value) {
  char buffer[19];
  snprintf(buffer, sizeof(buffer), "0x%llX", static_cast<unsigned long long>(value));
  return buffer;
}

void appendTiming(std::vector<uint16_t>& raw, uint32_t microseconds) {
  // The zero flips parity twice, preserving the original mark/space for long gaps.
  while (microseconds > UINT16_MAX) {
    raw.push_back(UINT16_MAX);
    raw.push_back(0);
    microseconds -= UINT16_MAX;
  }
  raw.push_back(static_cast<uint16_t>(microseconds));
}

uint64_t durationUs(const Signal& signal) {
  uint64_t result = 0;
  for (auto timing : signal.raw) result += timing;
  return result;
}

bool validSignal(const Signal& signal, std::string& error) {
  if (signal.overflow) error = "IR capture buffer overflow; signal was not saved";
  else if (signal.raw.size() < Config::MIN_RAW_SAMPLES) error = "IR signal too short/noise";
  else if (signal.raw.size() > Config::MAX_RAW_SAMPLES) error = "Too many RAW samples";
  else if (!signal.raw.front() || !signal.raw.back()) error = "Invalid RAW boundary";
  else if (signal.frequency < 10000 || signal.frequency > 60000) error = "Carrier must be 10000..60000 Hz";
  else if (durationUs(signal) > Config::MAX_SIGNAL_US) error = "IR burst exceeds duration limit";
  else if (signal.state.size() > 128 || signal.bits > 1024) error = "Invalid state/bits";
  else if ((!signal.state.empty() && signal.state.size()!=(signal.bits+7U)/8U) ||
           (signal.state.empty() && signal.protocol!="RAW" && signal.bits>64)) error="State length does not match bit count";
  else if (signal.protocol.empty() || signal.protocol.size()>32) error="Invalid protocol name";
  else if (signal.frequencySource!="default" && signal.frequencySource!="protocol_default" && signal.frequencySource!="manual") error="Invalid frequency_source";
  else {
    for (size_t i = 0; i < signal.raw.size(); ++i) {
      if (!signal.raw[i] && (i == 0 || signal.raw[i - 1] != UINT16_MAX)) {
        error = "Zero RAW timing only allowed after a long-duration split";
        return false;
      }
    }
    error.clear();
    return true;
  }
  return false;
}
}
