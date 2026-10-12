#pragma once
#include <ESP8266WebServer.h>
#include "storage.h"
#include "firmware_check.h"
#include <memory>

class WebServer {
 public:
  void begin();
  void loop();
  void statusToJson(JsonObject object) const;
 private:
  bool authorized();
  bool body(JsonDocument& document);
  bool fields(JsonObjectConst object,std::initializer_list<const char*> strings,
              std::initializer_list<const char*> integers={},std::initializer_list<const char*> booleans={});
  void reply(JsonDocument& document, int status = 200);
  void result(bool success, const std::string& error, int failureStatus = 400);
  void asset(const char* path, const char* mime);
  void remotes(const std::string& single = "");
  void latest();
  void buttonDetails();
  void notFound();
  void backupExport();
  void backupUpload();
  void backupImport();
  void enqueueCommand();
  void firmwareUpload();
  void firmwareComplete();
  void abortFirmware(const std::string& error);
  ESP8266WebServer server_{80};
  uint32_t restartAt_ = 0;
  bool restarting_ = false;
  File uploadFile_;
  bool uploadAllowed_ = false;
  size_t uploadBytes_ = 0;
  std::string uploadError_;
  std::unique_ptr<Bridge::FirmwareCheck> firmwareCheck_;
  bool firmwareAllowed_ = false, firmwareStarted_ = false, firmwareEnded_ = false;
  bool firmwarePartSeen_ = false;
  size_t firmwareExpected_ = 0;
  std::string firmwareError_;
};
extern WebServer webServer;
