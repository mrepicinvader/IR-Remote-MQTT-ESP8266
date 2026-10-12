#pragma once
// Host adapter for the unchanged firmware MQTT/Discovery sources and PubSubClient.
// This does not simulate ESP8266 GPIO, radio, heap or flash.
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
using boolean=bool;
using byte=uint8_t;
unsigned long millis();
void yield();
#define pgm_read_byte_near(address) (*reinterpret_cast<const uint8_t*>(address))
class Print {
 public:
  virtual ~Print()=default;
  virtual size_t write(uint8_t)=0;
  virtual size_t write(const uint8_t* data,size_t size) {
    size_t sent=0; while (sent<size && write(data[sent])) ++sent; return sent;
  }
};
class HostESP {
 public: uint32_t getFreeHeap() const { return 32000; } // Fixture telemetry only.
};
extern HostESP ESP;
