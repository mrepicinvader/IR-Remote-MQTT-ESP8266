#pragma once
#include <IRrecv.h>
#include "bridge_core.h"
namespace Bridge {
bool captureResult(const decode_results& result, Signal& signal, std::string& error);
}
