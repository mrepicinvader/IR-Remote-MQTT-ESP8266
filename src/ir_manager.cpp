#include "ir_manager.h"
#include <IRutils.h>
#include "ir_codec.h"
#include "logging.h"
#include "system_status.h"
#include <algorithm>

IRManager irManager;

IRManager::IRManager()
    : receiver_(Config::IR_RX_PIN, Config::RX_BUFFER_SIZE, Config::RX_TIMEOUT_MS, false),
      sender_(Config::IR_TX_PIN, Config::IR_TX_INVERTED) {}

void IRManager::begin() {
  sender_.begin();
  receiver_.setUnknownThreshold(Config::MIN_RAW_SAMPLES);
  receiver_.enableIRIn();
  LOGI("IR-RX", "Receiver GPIO%u, buffer=%u, timeout=%u ms", Config::IR_RX_PIN,
       Config::RX_BUFFER_SIZE, Config::RX_TIMEOUT_MS);
}

void IRManager::loop() {
  if (externallySuspended_) return;
  if (paused_) {
    if (static_cast<int32_t>(millis() - resumeAt_) < 0) return;
    receiver_.enableIRIn();
    paused_ = false;
  }
  decode_results result{};
  if (!receiver_.decode(&result)) return;
  // Free the previous vector before allocating the new capture.
  latest_ = Bridge::Signal{};
  const auto rawLength = getCorrectedRawLength(&result);
  if (rawLength > Config::MAX_RAW_SAMPLES ||
      ESP.getMaxFreeBlockSize() < rawLength * sizeof(uint16_t) + 2048) {
    result.overflow = true;
  }
  const bool valid=Bridge::captureResult(result,latest_,lastError_);
  receiver_.resume();
  ++sequence_;
  hasLatest_=true;
  lastReceived_ = uptimeMillis();
  if (valid) {
    LOGI("IR-RX", "%s %ubit addr=%s cmd=%s, RAW=%u, carrier=%lu (%s)",
         latest_.protocol.c_str(), latest_.bits, Bridge::hex64(latest_.address).c_str(),
         Bridge::hex64(latest_.command).c_str(), static_cast<unsigned>(latest_.raw.size()),
         static_cast<unsigned long>(latest_.frequency), latest_.frequencySource.c_str());
    Serial.print("[IR-RX][DEBUG] RAW first samples (us): ");
    for (size_t i = 0; i < latest_.raw.size() && i < 16; ++i) Serial.printf("%u ", latest_.raw[i]);
    Serial.println(" (serial 'd': full RAW dump)");
  } else LOGW("IR-RX", "%s", lastError_.c_str());
  if (handler_) handler_(latest_);
}

void IRManager::suspend() {
  receiver_.disableIRIn();
  externallySuspended_ = true;
}

void IRManager::resume() {
  externallySuspended_ = false;
  paused_ = true;
  resumeAt_ = millis() + Config::TX_IGNORE_MS;
}

bool IRManager::sendSignal(const Bridge::Signal& signal, Bridge::SendMode mode,
                           uint8_t repeatCount, std::string& error) {
  if (externallySuspended_) { error = "IR is suspended"; return false; }
  if (!Bridge::validSignal(signal, error)) return false;
  if (!repeatCount || repeatCount > 10) { error = "repeat_count must be 1..10 (total bursts)"; return false; }
  const auto protocol = strToDecodeType(signal.protocol.c_str());
  receiver_.disableIRIn();
  paused_ = true;
  bool decoded = false;
  if (Bridge::preferDecoded(signal, mode) && protocol != UNKNOWN) {
    if (!signal.state.empty()) {
      // State overload has no repeat argument; repeat the complete state packet.
      decoded = sender_.send(protocol, signal.state.data(), signal.state.size());
      if (decoded) for (uint8_t i = 1; i < repeatCount; ++i) {
        delay(Config::REPEAT_GAP_US / 1000);
        sender_.send(protocol, signal.state.data(), signal.state.size());
        yield();
      }
    } else {
      decoded = sender_.send(protocol, signal.value, signal.bits,
                             std::max<uint16_t>(repeatCount - 1, IRsend::minRepeats(protocol)));
    }
  }
  bool success = decoded;
  if (!decoded && mode != Bridge::SendMode::Decoded) {
    for (uint8_t i = 0; i < repeatCount; ++i) {
      if (i) delay(Config::REPEAT_GAP_US / 1000);
      sender_.sendRaw(signal.raw.data(), signal.raw.size(), static_cast<uint16_t>(signal.frequency));
      yield();
    }
    success = true;
  }
  // Set after the blocking transmission, not before it.
  resumeAt_ = millis() + Config::TX_IGNORE_MS;
  if (!success) { error = "Protocol cannot be sent using DECODED; select RAW/AUTO"; return false; }
  lastTransmitted_ = uptimeMillis();
  lastSendMode_ = decoded ? "DECODED" : "RAW";
  LOGI("IR-TX", "Sending %s using %s, %u timings, repeat_count=%u",
       signal.protocol.c_str(), lastSendMode_.c_str(), static_cast<unsigned>(signal.raw.size()), repeatCount);
  error.clear();
  return true;
}

bool IRManager::replayLatest(std::string& error) {
  if (!latest()) { error = "No received signal"; return false; }
  return sendSignal(latest_, Bridge::SendMode::Raw, 1, error);
}

void IRManager::dumpLatest() const {
  if (!latest()) return;
  Serial.print("[IR-RX][DEBUG] RAW us: [");
  for (size_t i = 0; i < latest_.raw.size(); ++i) {
    if (i) Serial.print(',');
    Serial.print(latest_.raw[i]);
    if (!(i % 64)) yield();
  }
  Serial.println(']');
}
