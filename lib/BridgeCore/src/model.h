#pragma once
#include <ArduinoJson.h>
#include <initializer_list>
#include "bridge_core.h"

namespace Bridge {
struct Button {
  std::string id;
  std::string name;
  std::string file;
  std::string entityType = "button";
  SendMode mode = SendMode::Auto;
  uint8_t repeatCount = 1;
  std::string icon;
  std::string slot;
  bool favorite = false;
  uint64_t lastTransmitted = 0;  // Uptime; deliberately not written after every TX.
};
struct CommandStep {
  std::string remote;
  std::string button;
  uint32_t delayMs = 0;  // Wait before this transmission.
};
struct Macro {
  std::string id;
  std::string name;
  std::vector<CommandStep> steps;
};
struct Remote {
  std::string id;
  std::string name;
  bool optimisticSwitch = false;
  std::string layout = "grid";
  std::vector<Button> buttons;
  std::vector<Macro> macros;
};
using Remotes = std::vector<Remote>;

bool validName(const std::string& name);
bool validId(const std::string& id);
bool validCodePath(const std::string& path);
bool validEntityType(const std::string& type);
bool validIcon(const std::string& icon);
bool validSlot(const std::string& slot);
bool validLayout(const std::string& layout);
bool validMacros(const Remotes& remotes, std::string& error);
bool macroFromJson(JsonObjectConst json, Macro& macro, std::string& error);
void macroToJson(const Macro& macro, JsonObject json);
void remoteMetaToJson(const Remote& remote, JsonObject json);
bool reorderButtons(Remote& remote, JsonArrayConst order, std::string& error);
const char* modeName(SendMode mode);
bool parseMode(const std::string& name, SendMode& result);
bool validRemoteFields(JsonObjectConst object, std::string& error);
bool validButtonFields(JsonObjectConst object, std::string& error, bool requireFile = true);
bool validJsonText(JsonVariantConst value);
bool validRequestTypes(JsonObjectConst object, std::initializer_list<const char*> strings,
                       std::initializer_list<const char*> integers,
                       std::initializer_list<const char*> booleans, std::string& error);
void buttonToJson(const Button& button, JsonObject object, bool includeFile = true);
void indexToJson(const Remotes& remotes, JsonDocument& doc);
size_t indexCapacity(const Remotes& remotes);
bool indexFromJson(JsonVariantConst json, Remotes& result, std::string& error);
void signalMetaToJson(const Signal& signal, JsonObject object);
bool signalMetaFromJson(JsonObjectConst object, Signal& signal, std::string& error);

// Stream RAW arrays without allocating one ArduinoJson node per timing.
class Reader {
 public:
  virtual ~Reader() = default;
  virtual int read() = 0;
  virtual bool canAllocate(size_t) const { return true; }
};
class Writer {
 public:
  virtual ~Writer() = default;
  virtual bool write(const char* bytes, size_t length) = 0;
  bool text(const std::string& value) { return write(value.data(), value.size()); }
};

class JsonStream {
 public:
  explicit JsonStream(Reader& reader) : reader_(reader) {}
  int peek();
  int get();
  void whitespace();
  bool expect(char character);
  bool value(std::string& result, size_t maxLength = 4096);
  bool key(std::string& result);
  bool raw(std::vector<uint16_t>& result, std::string& error);
  bool reserveRaw(std::vector<uint16_t>& result, size_t count, std::string& error);
 private:
  Reader& reader_;
  int next_ = -2;
};
bool writeSignal(const Signal& signal, Writer& writer, std::string& error);
bool readSignal(JsonStream& stream, Signal& signal, std::string& error);
bool readSignal(Reader& reader, Signal& signal, std::string& error);
}
