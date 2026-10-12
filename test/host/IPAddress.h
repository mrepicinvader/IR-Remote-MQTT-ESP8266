#pragma once
#include "Arduino.h"
#include <array>
class IPAddress {
 public:
  IPAddress()=default;
  IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):address_{{a,b,c,d}} {}
  IPAddress(const uint8_t* bytes) { for (size_t i=0;i<4;++i) address_[i]=bytes[i]; }
  uint8_t operator[](size_t i) const { return address_[i]; }
 private: std::array<uint8_t,4> address_{};
};
