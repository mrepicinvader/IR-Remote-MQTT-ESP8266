#include "system_status.h"
#include <Arduino.h>
uint64_t uptimeMillis() {
  static uint32_t previous=0;
  static uint64_t total=0;
  const uint32_t current=millis();
  total+=static_cast<uint32_t>(current-previous); previous=current;
  return total;
}
