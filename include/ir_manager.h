#pragma once
#include <IRremoteESP8266.h>
#include <IRrecv.h>
#include <IRsend.h>
#include "bridge_core.h"

class IRManager {
 public:
  IRManager();
  void begin();
  void loop();
  const Bridge::Signal* latest() const { return hasLatest_ ? &latest_ : nullptr; }
  void discardLatest() { latest_=Bridge::Signal{}; hasLatest_=false; }
  uint32_t sequence() const { return sequence_; }
  uint64_t lastReceived() const { return lastReceived_; }
  const std::string& lastError() const { return lastError_; }
  void dumpLatest() const;
  bool sendSignal(const Bridge::Signal& signal, Bridge::SendMode mode,
                  uint8_t repeatCount, std::string& error);
  bool replayLatest(std::string& error);
  uint64_t lastTransmitted() const { return lastTransmitted_; }
  const std::string& lastSendMode() const { return lastSendMode_; }
  void suspend();
  void resume();
  void onReceived(void (*handler)(const Bridge::Signal&)) { handler_ = handler; }
 private:
  IRrecv receiver_;
  IRsend sender_;
  Bridge::Signal latest_;
  uint32_t sequence_ = 0;
  uint64_t lastReceived_ = 0;
  bool hasLatest_ = false;
  std::string lastError_;
  uint64_t lastTransmitted_ = 0;
  uint32_t resumeAt_ = 0;
  bool paused_ = false;
  bool externallySuspended_ = false;
  std::string lastSendMode_;
  void (*handler_)(const Bridge::Signal&) = nullptr;
};
extern IRManager irManager;
