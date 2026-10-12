#include "ir_codec.h"
#include "model.h"
#include "learning.h"
#include <IRsend_test.h>
#include <ir_Daikin.h>
#include <iostream>
#include <stdexcept>
#include <functional>

using namespace Bridge;
namespace {
void check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
class TextReader : public Reader {
 public: explicit TextReader(const std::string& data):data_(data) {}
  int read() override { return offset_<data_.size()?static_cast<unsigned char>(data_[offset_++]):-1; }
 private: const std::string& data_; size_t offset_=0;
};
class TextWriter : public Writer {
 public: std::string data;
  bool write(const char* bytes,size_t length) override { data.append(bytes,length); return true; }
};
Signal rebootRoundTrip(const Signal& original) {
  TextWriter writer; std::string error; check(writeSignal(original,writer,error),error.c_str());
  TextReader reader(writer.data); Signal restored; check(readSignal(reader,restored,error),error.c_str());
  check(restored.raw==original.raw && restored.state==original.state && restored.value==original.value,"Restart changed signal");
  return restored;
}
void receiverCapture(IRsendTest& transmitter) {
  transmitter.makeDecodeResult();
  // Receiver stops at idle timeout; it does not record the terminal idle space.
  if (transmitter.capture.rawlen%2) --transmitter.capture.rawlen;
}
void assertRawOutput(const Signal& signal) {
  IRsendTest transmitter(4); transmitter.sendRaw(signal.raw.data(),signal.raw.size(),signal.frequency);
  check(transmitter.last+1U==signal.raw.size(),"RAW replay length changed");
  for (size_t i=0;i<signal.raw.size();++i) {
    check(transmitter.output[i]==signal.raw[i],"RAW replay timing changed");
    check(transmitter.freq[i]==signal.frequency,"RAW replay carrier changed");
  }
}
}
int main() {
  int total=0,failed=0;
  auto test=[&](const char* name,const std::function<void()>& fn) {
    ++total;
    try { fn(); std::cout<<"PASS "<<name<<'\n'; }
    catch (const std::exception& e) { ++failed; std::cout<<"FAIL "<<name<<": "<<e.what()<<'\n'; }
  };
  test("Real IRremoteESP8266 NEC decode -> learn -> JSON -> RAW send -> NEC decode",[] {
    IRsendTest original(4); IRrecv receiver(14);
    original.sendNEC(0x20DF10EFULL,32,0); receiverCapture(original);
    check(receiver.decode(&original.capture) && original.capture.decode_type==NEC,"Real NEC decoder failed");
    Signal signal; std::string error; check(captureResult(original.capture,signal,error),error.c_str());
    check(signal.bits==32 && signal.value==0x20DF10EFULL && signal.raw.size()==67,"Capture incomplete");
    check(signal.decodedReliable && preferDecoded(signal,SendMode::Auto),"Safe NEC not eligible for decoded send");
    LearningSession learning; check(learning.start(1,20,0,error),"Start");
    check(learning.observe(signal,100)==LearnResult::Confirmed,"Learning failed");
    auto restored=rebootRoundTrip(signal); assertRawOutput(restored);
    IRsendTest replay(4); replay.sendRaw(restored.raw.data(),restored.raw.size(),restored.frequency); receiverCapture(replay);
    check(receiver.decode(&replay.capture) && replay.capture.value==signal.value,"Replayed NEC differs");
    check(replay.capture.address==signal.address && replay.capture.command==signal.command,"Address/command changed");
  });
  test("Unknown RAW decode -> learn -> JSON -> exact sendRaw output",[] {
    const uint16_t timings[]={3100,900,700,1700,400,800,700,900,500,1200,600};
    IRsendTest original(4); original.sendRaw(timings,11,38000); receiverCapture(original);
    IRrecv receiver(14); receiver.setUnknownThreshold(6);
    check(receiver.decode(&original.capture) && original.capture.decode_type==UNKNOWN,"Unexpected RAW decode");
    Signal signal; std::string error; check(captureResult(original.capture,signal,error),error.c_str());
    check(signal.protocol=="RAW" && !signal.decodedReliable,"RAW misclassified");
    LearningSession l; check(l.start(1,20,0,error) && l.observe(signal,100)==LearnResult::Confirmed,"RAW learning");
    assertRawOutput(rebootRoundTrip(signal));
  });
  test("Real library very long 4095 timing capture and replay",[] {
    Signal s; s.raw.assign(4095,200); IRsendTest original(4);
    original.sendRaw(s.raw.data(),s.raw.size(),38000); receiverCapture(original);
    original.capture.decode_type=UNKNOWN; original.capture.bits=0;
    Signal captured; std::string error; check(captureResult(original.capture,captured,error),error.c_str());
    check(captured.raw==s.raw,"Long signal truncated"); assertRawOutput(rebootRoundTrip(captured));
    original.capture.overflow=true; check(!captureResult(original.capture,captured,error),"Overflow accepted");
  });
  test("Full-state Daikin packet retains every state byte and RAW section",[] {
    IRDaikinESP remote(4); remote.begin(); remote.on(); remote.setTemp(23); remote.setFan(kDaikinFanAuto);
    const uint8_t* state=remote.getRaw();
    IRsendTest original(4); original.send(DAIKIN,state,kDaikinStateLength); receiverCapture(original);
    IRrecv receiver(14); check(receiver.decode(&original.capture) && original.capture.decode_type==DAIKIN,"Daikin decode failed");
    Signal captured; std::string error; check(captureResult(original.capture,captured,error),error.c_str());
    check(captured.state.size()==kDaikinStateLength,"Missing full state");
    check(!preferDecoded(captured,SendMode::Auto),"AUTO would discard captured sections");
    auto restored=rebootRoundTrip(captured); assertRawOutput(restored);
    IRsendTest decoded(4); check(decoded.send(DAIKIN,restored.state.data(),restored.state.size()),"State send overload failed");
  });
  test("Long gaps are preserved by real sendRaw zero-duration splits",[] {
    Signal s; s.raw={1000,500,1000,500,1000,500,1000}; appendTiming(s.raw,150000); s.raw.push_back(1000);
    IRsendTest output(4); output.sendRaw(s.raw.data(),s.raw.size(),38000);
    uint64_t total=0; for (size_t i=0;i<=output.last;++i) total+=output.output[i];
    check(total==durationUs(s),"Long gap duration changed");
    check(output.output[7]==65535 && output.output[8]==0 && output.output[9]==65535 &&
          output.output[10]==0 && output.output[11]==18930 && output.output[12]==1000,"Zero splits changed pulse parity");
  });
  std::cout<<total-failed<<'/'<<total<<" IR library tests passed\n";
  return failed?1:0;
}
