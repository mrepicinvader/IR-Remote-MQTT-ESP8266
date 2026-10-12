#include "ir_codec.h"
#include <IRutils.h>
#include <ir_Panasonic.h>

namespace Bridge {
namespace {
uint32_t frequencyFor(decode_type_t protocol) {
  switch (protocol) {
    case SONY: return 40000;
    case RC5: case RC5X: case RC6: case RCMM: return 36000;
    case DISH: return 57600;
    case PANASONIC: return kPanasonicFreq;
    default: return Config::DEFAULT_FREQUENCY_HZ;
  }
}
}
bool captureResult(const decode_results& result, Signal& signal, std::string& error) {
  signal=Signal{};
  signal.overflow=result.overflow; signal.repeat=result.repeat;
  signal.protocol=result.decode_type==UNKNOWN?"RAW":typeToString(result.decode_type).c_str();
  signal.bits=result.bits; signal.frequency=frequencyFor(result.decode_type);
  signal.frequencySource=result.decode_type==UNKNOWN?"default":"protocol_default";
  if (hasACState(result.decode_type)) {
    const size_t bytes=(result.bits+7U)/8U;
    if (bytes<=sizeof(result.state)) signal.state.assign(result.state,result.state+bytes);
    else signal.overflow=true;
  } else if (result.decode_type!=UNKNOWN) {
    signal.value=result.value; signal.address=result.address; signal.command=result.command;
  }
  if (signal.overflow) return validSignal(signal,error);
  const size_t count=getCorrectedRawLength(&result);
  if (!result.rawbuf || count>Config::MAX_RAW_SAMPLES) { error="Invalid/oversized receive buffer"; return false; }
  signal.raw.reserve(count);
  for (uint16_t i=1;i<result.rawlen;++i) appendTiming(signal.raw,static_cast<uint32_t>(result.rawbuf[i])*kRawTick);
  signal.decodedReliable=result.decode_type==NEC && !result.repeat && canonicalNec(signal);
  return validSignal(signal,error);
}
}
