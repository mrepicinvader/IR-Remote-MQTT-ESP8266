#include "learning.h"
#include <algorithm>

namespace Bridge {
namespace {
uint32_t nextTiming(const std::vector<uint16_t>& raw, size_t& position) {
  uint32_t duration=raw[position++];
  // A 65535/0/remainder sequence represents one continuous mark or space.
  // Compare its duration, not its storage representation across split boundaries.
  while (position+1<raw.size() && raw[position]==0 && raw[position-1]==UINT16_MAX) {
    ++position; duration+=raw[position++];
  }
  return duration;
}
}
bool similarSignals(const Signal& a, const Signal& b, uint8_t tolerance) {
  if (a.protocol != b.protocol || a.bits != b.bits || a.repeat != b.repeat ||
      a.frequency != b.frequency || a.state != b.state) return false;
  if (a.protocol != "RAW" && (a.value != b.value || a.address != b.address || a.command != b.command)) return false;
  size_t left=0,right=0;
  while (left<a.raw.size() && right<b.raw.size()) {
    const uint32_t x=nextTiming(a.raw,left),y=nextTiming(b.raw,right);
    const uint32_t difference=x>y?x-y:y-x;
    if (!x || !y) { if (x!=y) return false; }
    else if (difference>std::max<uint64_t>(80,static_cast<uint64_t>(x)*tolerance/100)) return false;
  }
  return left==a.raw.size() && right==b.raw.size();
}
bool LearningSession::start(uint8_t confirmations, uint8_t tolerance, uint32_t now, std::string& error) {
  if (active()) { error = "Learning already active"; return false; }
  if (confirmations < 1 || confirmations > 3 || tolerance < 15 || tolerance > 25) {
    error = "confirmations must be 1..3, tolerance_percent 15..25"; return false;
  }
  cancel(); required_ = confirmations; tolerance_ = tolerance; started_ = now;
  status_ = "waiting"; message_ = "Press and release the same remote key; leave at least 400 ms silence between presses";
  error.clear(); return true;
}
void LearningSession::cancel() {
  status_ = "idle"; message_.clear(); count_ = 0; hasSignal_ = false; candidate_ = Signal{};
}
uint32_t LearningSession::remaining(uint32_t now) const {
  const uint32_t elapsed = now - started_;
  return active() && elapsed < Config::LEARN_TIMEOUT_MS ? Config::LEARN_TIMEOUT_MS - elapsed : 0;
}
void LearningSession::tick(uint32_t now) {
  if (!active()) return;
  if (!remaining(now)) { status_ = "timeout"; message_ = "Learning timeout"; candidate_ = Signal{}; }
  else if (status_ == "receiving" && now - lastSignal_ > 200) status_ = "waiting";
}
LearnResult LearningSession::observe(const Signal& signal, uint32_t now) {
  tick(now); if (!active()) return LearnResult::Ignored;
  const bool separatePress = !hasSignal_ || now - lastSignal_ >= Config::DISTINCT_PRESS_MS;
  hasSignal_ = true; lastSignal_ = now;
  std::string error;
  if (!validSignal(signal, error)) { message_ = error; return LearnResult::Rejected; }
  if (signal.repeat || !separatePress) { message_ = "Release key; repeats/held key do not count as confirmations"; return LearnResult::Ignored; }
  status_ = "receiving";
  if (required_ == 1) { count_ = 1; message_ = "Signal captured"; return LearnResult::Confirmed; }
  if (!count_ || !similarSignals(candidate_, signal, tolerance_)) {
    const bool mismatch = count_ > 0;
    candidate_=Signal{};
    candidate_ = signal; count_ = 1;
    message_ = mismatch ? "Different signal; candidate restarted at 1" : "Candidate 1 captured; release and press again";
    return LearnResult::Candidate;
  }
  ++count_;
  if (count_ >= required_) {
    candidate_ = Signal{};  // Save current matched capture; avoid a third RAW copy during validation.
    message_ = "Signal confirmed"; return LearnResult::Confirmed;
  }
  message_ = "Matching candidate captured; press again";
  return LearnResult::Candidate;
}
void LearningSession::saved(bool success, const std::string& error) {
  status_ = success ? "saved" : "error";
  message_ = success ? "Signal saved in LittleFS" : error;
  candidate_ = Signal{};
}
}
