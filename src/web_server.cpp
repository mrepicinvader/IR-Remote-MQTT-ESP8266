#include "web_server.h"
#include "ir_manager.h"
#include "learning_manager.h"
#include "wifi_manager.h"
#include "logging.h"
#include "mqtt_manager.h"
#include "discovery_manager.h"
#include "ota_manager.h"
#include "system_status.h"
#include "command_manager.h"
#include <Updater.h>
#include <cstdlib>
#include <algorithm>

WebServer webServer;
bool WebServer::authorized() {
  const auto& c = storage.config();
  if (c.webAuth && !server_.authenticate(c.webUser.c_str(), c.webPassword.c_str())) {
    server_.requestAuthentication(BASIC_AUTH, "ESP IR Bridge"); return false;
  }
  return true;
}
bool WebServer::body(JsonDocument& document) {
  if (!authorized()) return false;
  if (commandManager.suspended() || restarting_) { result(false,"Update or restart in progress",409); return false; }
  if (!server_.header("Content-Type").startsWith("application/json")) {
    result(false,"Content-Type must be application/json",415); return false;
  }
  const String& text = server_.arg("plain");
  if (text.length() > 4096 || deserializeJson(document, text) || !document.is<JsonObject>()) {
    result(false, "Invalid JSON object or request too large"); return false;
  }
  return true;
}
bool WebServer::fields(JsonObjectConst object,std::initializer_list<const char*> strings,
                       std::initializer_list<const char*> integers,std::initializer_list<const char*> booleans) {
  std::string error;
  if (Bridge::validRequestTypes(object,strings,integers,booleans,error)) return true;
  result(false,error); return false;
}
void WebServer::reply(JsonDocument& document, int code) {
  if (document.overflowed()) { server_.send(503, "application/json", "{\"success\":false,\"error\":\"Not enough JSON memory\"}"); return; }
  String response; serializeJson(document, response);
  server_.sendHeader("Cache-Control", "no-store");
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.send(code, "application/json", response);
}
void WebServer::result(bool success, const std::string& error, int code) {
  StaticJsonDocument<512> response; response["success"] = success;
  if (!success) response["error"] = error;
  reply(response, success ? 200 : code);
}
void WebServer::asset(const char* path, const char* mime) {
  if (!authorized()) return;
  File file = LittleFS.open(path, "r");
  if (!file) { server_.send(503, "text/html; charset=utf-8", "<h1>ESP IR Bridge</h1><p>Webdateien fehlen. Bitte LittleFS mit pio run -e esp_ir_usb -t uploadfs hochladen. Vorhandene Laufzeitdaten vorher exportieren.</p>"); return; }
  server_.sendHeader("X-Content-Type-Options", "nosniff");
  server_.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; object-src 'none'; frame-ancestors 'none'; base-uri 'self'");
  server_.sendHeader("Cache-Control", "no-cache");
  server_.streamFile(file, mime); file.close();
}
void WebServer::statusToJson(JsonObject object) const {
  object["firmware_version"] = Config::VERSION; object["build_date"] = __DATE__ " " __TIME__;
  object["device_id"] = wifiManager.deviceId(); object["hostname"] = wifiManager.hostname();
  object["wifi_connected"] = wifiManager.connected(); object["ap_active"] = wifiManager.apActive();
  object["ap_ssid"] = wifiManager.apSsid(); object["ip"] = wifiManager.ip();
  object["ap_protected"] = wifiManager.apProtected(); object["ap_fallback_enabled"] = storage.config().apFallbackEnabled;
  object["rssi"] = wifiManager.connected() ? WiFi.RSSI() : 0;
  object["uptime_ms"] = uptimeMillis(); object["free_heap"] = ESP.getFreeHeap();
  object["max_free_block"] = ESP.getMaxFreeBlockSize(); object["wifi_reconnect_count"] = wifiManager.reconnectCount();
  object["mqtt_connected"] = mqttManager.connected(); object["mqtt_reconnect_count"] = mqttManager.reconnectCount();
  object["mqtt_base_topic"] = mqttManager.baseTopic();
  object["discovery_status"] = discoveryManager.status();
  object["backup_supported"] = true;
  object["ota_status"] = otaManager.status(); object["ota_progress"] = otaManager.progress();
  object["firmware_target"] = Config::FIRMWARE_TARGET;
  object["web_update_enabled"] = storage.config().otaEnabled;
  const size_t space=ESP.getFreeSketchSpace();
  const size_t updateSpace=space>4096 ? ((space-4096)&0xfffff000) : 0;
  object["firmware_max_bytes"] = updateSpace ? updateSpace-1 : 0;
  object["web_update_status"] = firmwareStarted_ ? "uploading" : firmwareError_.empty() ? "ready" : "failed";
  object["queued_commands"] = commandManager.queue().pending();
  object["flash_actual_bytes"] = ESP.getFlashChipRealSize(); object["flash_configured_bytes"] = ESP.getFlashChipSize();
  object["storage_ready"] = storage.ready(); object["storage_error"] = storage.error();
  object["remote_count"] = storage.remotes().size(); object["button_count"] = storage.buttonCount();
  FSInfo info;
  if (storage.mounted() && LittleFS.info(info)) { object["littlefs_total"] = info.totalBytes; object["littlefs_used"] = info.usedBytes; }
  object["last_ir_received_ms"] = irManager.lastReceived(); object["last_ir_transmitted_ms"] = irManager.lastTransmitted();
  object["last_send_mode"] = irManager.lastSendMode();
  object["max_remotes"] = Config::MAX_REMOTES; object["max_buttons"] = Config::MAX_BUTTONS;
  object["rx_buffer_size"] = Config::RX_BUFFER_SIZE; object["max_raw_samples"] = Config::MAX_RAW_SAMPLES;
}
void WebServer::remotes(const std::string& single) {
  if (!authorized()) return;
  DynamicJsonDocument doc(Bridge::indexCapacity(storage.remotes()));
  if (single.empty()) { Bridge::indexToJson(storage.remotes(), doc); reply(doc); return; }
  auto* r = storage.findRemote(single); if (!r) { result(false, "Remote not found", 404); return; }
  Bridge::remoteMetaToJson(*r, doc.to<JsonObject>());
  JsonArray array = doc.createNestedArray("buttons");
  for (const auto& b : r->buttons) Bridge::buttonToJson(b, array.createNestedObject(), false);
  reply(doc);
}
void WebServer::latest() {
  if (!authorized()) return;
  DynamicJsonDocument doc(3072); doc["sequence"] = irManager.sequence();
  doc["received_ms"] = irManager.lastReceived(); doc["available"] = irManager.latest() != nullptr;
  if (const auto* signal = irManager.latest()) {
    Bridge::signalMetaToJson(*signal, doc.createNestedObject("signal"));
    std::string error; doc["valid"] = Bridge::validSignal(*signal, error); doc["error"] = error;
  }
  reply(doc);
}
void WebServer::buttonDetails() {
  if (!authorized()) return;
  auto* b = storage.findButton(server_.arg("remote").c_str(), server_.arg("button").c_str());
  if (!b) { result(false, "Button not found", 404); return; }
  if (server_.arg("raw") == "1") {
    File file = LittleFS.open(b->file.c_str(), "r");
    if (!file) { result(false, "Code file unavailable", 500); return; }
    server_.streamFile(file, "application/json"); return;
  }
  Bridge::Signal signal; std::string error;
  if (!storage.loadSignal(*b, signal, error)) { result(false, error, 503); return; }
  DynamicJsonDocument doc(3072); auto object = doc.to<JsonObject>();
  Bridge::buttonToJson(*b, object, false); Bridge::signalMetaToJson(signal, object);
  object["last_transmitted_ms"] = b->lastTransmitted;
  reply(doc);
}
void WebServer::begin() {
  server_.collectHeaders("Content-Type","X-IRBridge-Import","X-IRBridge-Firmware","X-Firmware-Size","X-Firmware-MD5","X-IRBridge-OTA-Password");
  server_.on("/", HTTP_GET, [this] { asset("/index.html", "text/html; charset=utf-8"); });
  for (const char* uri : {"/monitor", "/diagnostics", "/settings"}) server_.on(uri, HTTP_GET, [this] { asset("/index.html", "text/html; charset=utf-8"); });
  server_.on("/app.js", HTTP_GET, [this] { asset("/app.js", "application/javascript"); });
  server_.on("/style.css", HTTP_GET, [this] { asset("/style.css", "text/css"); });
  server_.on("/api/status", HTTP_GET, [this] { if (!authorized()) return; DynamicJsonDocument doc(4096); statusToJson(doc.to<JsonObject>()); reply(doc); });
  server_.on("/api/remotes", HTTP_GET, [this] { remotes(); });
  server_.on("/api/remotes", HTTP_POST, [this] { DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"name","layout"})) return; std::string e; const bool ok=storage.createRemote(doc["name"] | "",e,doc["layout"] | "grid"); result(ok,e); });
  server_.on("/api/remotes", HTTP_PATCH, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","name","layout"},{},{"optimistic_switch"})) return; std::string e;
    auto* remote=storage.findRemote(doc["remote"] | "");
    if (!remote) { result(false,"Remote not found",404); return; }
    const bool ok=storage.renameRemote(doc["remote"] | "",doc["name"] | remote->name.c_str(),doc["optimistic_switch"] | remote->optimisticSwitch,e,doc["layout"] | ""); result(ok,e);
  });
  server_.on("/api/send", HTTP_POST, [this] {
    enqueueCommand();
  });
  server_.on("/api/commands", HTTP_GET, [this] {
    if (!authorized()) return;
    const std::string id=server_.arg("request_id").c_str();
    DynamicJsonDocument doc(6144);
    if (!id.empty()) {
      const auto* command=commandManager.queue().find(id);
      if (!command) { result(false,"Request not found in recent history",404); return; }
      Bridge::commandResultToJson(*command,doc.to<JsonObject>());
    } else {
      doc["pending"]=commandManager.queue().pending();
      auto commands=doc.createNestedArray("commands");
      for (const auto& command : commandManager.queue().history()) Bridge::commandResultToJson(command,commands.createNestedObject());
    }
    reply(doc);
  });
  server_.on("/api/commands/cancel", HTTP_POST, [this] {
    DynamicJsonDocument doc(512); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"request_id"})) return;
    std::string e; const bool ok=commandManager.cancel(doc["request_id"] | "",e); result(ok,e,409);
  });
  server_.on("/api/macros", HTTP_POST, [this] {
    DynamicJsonDocument doc(6144); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","id","name"})) return;
    std::string e; const bool ok=storage.saveMacro(doc["remote"] | "",doc.as<JsonObjectConst>(),e); result(ok,e);
  });
  server_.on("/api/macros", HTTP_DELETE, [this] {
    DynamicJsonDocument doc(1024); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","macro"})) return;
    std::string e; const bool ok=storage.deleteMacro(doc["remote"] | "",doc["macro"] | "",e); result(ok,e);
  });
  server_.on("/api/macros/run", HTTP_POST, [this] { enqueueCommand(); });
  server_.on("/api/remotes/layout", HTTP_PATCH, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","layout"})) return;
    std::string e; const bool ok=storage.updateLayout(doc["remote"] | "",doc.as<JsonObjectConst>(),e); result(ok,e);
  });
  server_.on("/api/send/latest", HTTP_POST, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc)) return; std::string e;
    if (learningManager.session().active() || commandManager.busy()) { result(false,"Wait for queued sends or cancel learning before replay",409); return; }
    const bool ok=irManager.replayLatest(e); result(ok,e);
  });
  server_.on("/api/learn/start", HTTP_POST, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc)) return; std::string e;
    const bool ok=learningManager.start(doc.as<JsonObjectConst>(),e); result(ok,e,409);
  });
  server_.on("/api/learn/cancel", HTTP_POST, [this] { if (!authorized()) return; learningManager.cancel(); result(true,""); });
  server_.on("/api/learn/status", HTTP_GET, [this] { if (!authorized()) return; DynamicJsonDocument doc(1024); learningManager.toJson(doc.to<JsonObject>()); reply(doc); });
  server_.on("/api/received/latest", HTTP_GET, [this] { latest(); });
  server_.on("/api/received/use", HTTP_POST, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","name"},{"sequence"})) return; std::string e;
    const bool ok=learningManager.useLatest(doc["remote"] | "",doc["name"] | "",doc["sequence"] | 0U,e); result(ok,e,409);
  });
  server_.on("/api/button", HTTP_GET, [this] { buttonDetails(); });
  server_.on("/api/button", HTTP_DELETE, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","button"})) return; std::string e;
    const bool ok=storage.deleteButton(doc["remote"] | "",doc["button"] | "",e); result(ok,e);
  });
  server_.on("/api/button", HTTP_PATCH, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc) || !fields(doc.as<JsonObjectConst>(),{"remote","button","name","entity_type","send_mode","icon","slot"},{"repeat_count","frequency"},{"favorite"})) return; std::string e;
    auto* b=storage.findButton(doc["remote"] | "",doc["button"] | "");
    if (!b) { result(false,"Button not found",404); return; }
    Bridge::SendMode mode=b->mode;
    const uint32_t repeats=doc["repeat_count"] | static_cast<uint32_t>(b->repeatCount);
    const uint32_t frequency=doc["frequency"] | 0U;
    if (doc.containsKey("frequency") && (frequency<10000 || frequency>60000)) { result(false,"Carrier must be 10000..60000 Hz"); return; }
    if (!Bridge::parseMode(doc["send_mode"] | Bridge::modeName(b->mode),mode) || repeats<1 || repeats>10) { result(false,"Invalid settings"); return; }
    const bool ok=storage.updateButton(doc["remote"] | "",doc["button"] | "",doc["name"] | b->name.c_str(),
       doc["entity_type"] | b->entityType.c_str(),mode,repeats,frequency,
       doc["icon"] | b->icon.c_str(),doc["slot"] | b->slot.c_str(),doc["favorite"] | b->favorite,e); result(ok,e);
  });
  server_.on("/api/config", HTTP_GET, [this] { if (!authorized()) return; DynamicJsonDocument doc(2048); Storage::configToJson(storage.config(),doc.to<JsonObject>(),false); reply(doc); });
  server_.on("/api/config", HTTP_POST, [this] {
    DynamicJsonDocument doc(4096); if (!body(doc)) return;
    if (commandManager.busy() || learningManager.session().active()) { result(false,"Finish queued sends and learning before restart",409); return; }
    auto config=storage.config(); std::string e;
    if (!Storage::configFromJson(doc.as<JsonObjectConst>(),config,e,true) || !storage.saveConfig(config,e)) { result(false,e); return; }
    result(true,""); restarting_=true; restartAt_=millis()+1000;
    LOGI("WEB", "Configuration saved; reboot scheduled");
  });
  server_.on("/api/diagnostics", HTTP_GET, [this] { if (!authorized()) return; DynamicJsonDocument doc(4096); statusToJson(doc.to<JsonObject>()); reply(doc); });
  server_.on("/api/ota/status", HTTP_GET, [this] { if (!authorized()) return; StaticJsonDocument<256> doc; doc["version"]=Config::VERSION; doc["status"]=otaManager.status(); doc["progress"]=otaManager.progress(); reply(doc); });
  server_.on("/api/backup/export", HTTP_GET, [this] { backupExport(); });
  server_.on("/api/backup/import", HTTP_POST, [this] { backupImport(); }, [this] { backupUpload(); });
  server_.on("/api/firmware", HTTP_POST, [this] { firmwareComplete(); }, [this] { firmwareUpload(); });
  server_.onNotFound([this] { notFound(); });
  server_.begin(); LOGI("WEB", "HTTP server listening on port 80");
}
void WebServer::enqueueCommand() {
  DynamicJsonDocument doc(1024); if (!body(doc)) return;
  Bridge::CommandResult command; std::string error;
  if (!commandManager.enqueue(doc.as<JsonObjectConst>(),"web",command,error)) { result(false,error,409); return; }
  StaticJsonDocument<768> response; Bridge::commandResultToJson(command,response.to<JsonObject>()); reply(response,202);
}
void WebServer::abortFirmware(const std::string& error) {
  firmwareError_=error;
  if (firmwareStarted_) {
    // One extra byte is reserved at begin, so end(false) can never activate an
    // aborted file, even after all declared bytes arrived. Success uses end(true).
    Update.end(false); firmwareStarted_=false;
    irManager.resume(); commandManager.resume();
  }
  firmwareCheck_.reset(); firmwareEnded_=false;
}
void WebServer::firmwareUpload() {
  auto& upload=server_.upload();
  if (upload.status==UPLOAD_FILE_START) {
    if (firmwarePartSeen_) { abortFirmware("Only one firmware file is allowed"); return; }
    firmwarePartSeen_=true;
    firmwareError_.clear(); firmwareAllowed_=false; firmwareExpected_=0;
    const auto& c=storage.config();
    if (c.webAuth && !server_.authenticate(c.webUser.c_str(),c.webPassword.c_str())) { firmwareError_="Authentication required"; return; }
    if (!c.otaEnabled || commandManager.suspended() || restarting_) { firmwareError_="Firmware updates disabled or another update is active"; return; }
    if (server_.header("X-IRBridge-Firmware")!="1") { firmwareError_="Firmware upload header required"; return; }
    if (!c.otaPassword.empty() && server_.header("X-IRBridge-OTA-Password")!=c.otaPassword.c_str()) { firmwareError_="Invalid OTA password"; return; }
    if (learningManager.session().active() || commandManager.busy()) { firmwareError_="Finish learning and queued sends before updating"; return; }
    const std::string size=server_.header("X-Firmware-Size").c_str();
    if (size.empty() || size.size()>7 || std::any_of(size.begin(),size.end(),[](char ch){return ch<'0'||ch>'9';})) { firmwareError_="Valid firmware byte count required"; return; }
    firmwareExpected_=std::strtoul(size.c_str(),nullptr,10);
    const size_t space=ESP.getFreeSketchSpace(); const size_t limit=space>4096 ? ((space-4096)&0xfffff000) : 0;
    const std::string md5=server_.header("X-Firmware-MD5").c_str();
    if (upload.name!="firmware" || !upload.filename.endsWith(".bin") || firmwareExpected_<64 || !limit || firmwareExpected_>=limit || !Bridge::validMd5(md5)) { firmwareError_="Expected a .bin file within update space and its MD5 digest"; return; }
    if (ESP.getFlashChipRealSize()!=ESP.getFlashChipSize()) { firmwareError_="Actual flash size differs from the configured layout"; return; }
    firmwareCheck_.reset(new Bridge::FirmwareCheck(firmwareExpected_,limit-1));
    irManager.discardLatest(); irManager.suspend(); commandManager.suspend("Firmware update"); mqttManager.stop();
    if (!Update.begin(firmwareExpected_+1,U_FLASH) || !Update.setMD5(md5.c_str())) {
      firmwareError_=Update.getErrorString().c_str(); irManager.resume(); commandManager.resume(); firmwareCheck_.reset(); return;
    }
    firmwareStarted_=true; firmwareAllowed_=true;
  } else if (upload.status==UPLOAD_FILE_WRITE && firmwareAllowed_ && firmwareStarted_ && firmwareError_.empty()) {
    std::string error;
    if (!firmwareCheck_->feed(upload.buf,upload.currentSize,error)) { abortFirmware(error); return; }
    if (Update.write(upload.buf,upload.currentSize)!=upload.currentSize) abortFirmware(Update.getErrorString().c_str());
  } else if (upload.status==UPLOAD_FILE_END && firmwareStarted_ && firmwareError_.empty()) {
    std::string error;
    if (!firmwareCheck_->finish(error) || upload.totalSize!=firmwareExpected_) { abortFirmware(error.empty()?"Firmware size mismatch":error); return; }
    firmwareEnded_=true;  // Activation waits for the final authenticated request handler.
  } else if (upload.status==UPLOAD_FILE_ABORTED) { abortFirmware("Firmware upload aborted"); firmwarePartSeen_=false; firmwareAllowed_=false; }
  yield();
}
void WebServer::firmwareComplete() {
  firmwarePartSeen_=false;
  if (!authorized()) { abortFirmware("Authentication required"); return; }
  if (!firmwareAllowed_ || !firmwareStarted_ || !firmwareEnded_ || !firmwareError_.empty()) {
    const std::string error=firmwareError_.empty()?"Complete multipart firmware file required":firmwareError_;
    abortFirmware(error); firmwareAllowed_=false; result(false,error,400); return;
  }
  if (!Update.end(true)) { const std::string error=Update.getErrorString().c_str(); abortFirmware(error); result(false,error,400); return; }
  firmwareStarted_=false; firmwareEnded_=false; firmwareAllowed_=false; firmwareCheck_.reset();
  result(true,""); restarting_=true; restartAt_=millis()+1500;
}
void WebServer::backupExport() {
  if (!authorized()) return;
  if (learningManager.session().active()) { result(false,"Cancel learning before backup",409); return; }
  std::string error;
  if (!storage.exportBackup("/data/export.tmp",error)) { result(false,error,503); return; }
  File file=LittleFS.open("/data/export.tmp","r");
  server_.sendHeader("Content-Disposition","attachment; filename=irbridge-backup.json");
  server_.sendHeader("Cache-Control","no-store"); server_.streamFile(file,"application/json");
  file.close(); LittleFS.remove("/data/export.tmp");
}
void WebServer::backupUpload() {
  auto& upload=server_.upload();
  if (upload.status==UPLOAD_FILE_START) {
    uploadError_.clear(); uploadBytes_=0;
    const auto& c=storage.config();
    uploadAllowed_=!c.webAuth || server_.authenticate(c.webUser.c_str(),c.webPassword.c_str());
    if (!uploadAllowed_) return;
    if (server_.header("X-IRBridge-Import")!="1") { uploadError_="X-IRBridge-Import: 1 header required"; return; }
    if (learningManager.session().active() || commandManager.busy() || commandManager.suspended() || restarting_) { uploadError_="Finish learning and queued sends before import"; return; }
    uploadFile_=LittleFS.open("/data/import.tmp","w");
    if (!uploadFile_) uploadError_="Cannot create import file";
  } else if (upload.status==UPLOAD_FILE_WRITE && uploadAllowed_ && uploadError_.empty()) {
    uploadBytes_+=upload.currentSize;
    if (uploadBytes_>Config::MAX_BACKUP_BYTES || !uploadFile_ || uploadFile_.write(upload.buf,upload.currentSize)!=upload.currentSize) {
      uploadError_="Backup too large or filesystem full"; uploadFile_.close(); LittleFS.remove("/data/import.tmp");
    }
  } else if (upload.status==UPLOAD_FILE_END) {
    if (uploadFile_) { uploadFile_.flush(); uploadFile_.close(); }
  } else if (upload.status==UPLOAD_FILE_ABORTED) {
    uploadFile_.close(); if (uploadAllowed_) LittleFS.remove("/data/import.tmp"); uploadError_="Upload aborted";
  }
  yield();
}
void WebServer::backupImport() {
  if (!authorized()) return;
  if (!uploadAllowed_ || !uploadBytes_ || !uploadError_.empty()) { result(false,uploadError_.empty()?"Multipart backup file required":uploadError_); return; }
  irManager.discardLatest();
  std::string error; const bool success=storage.importBackup("/data/import.tmp",error);
  LittleFS.remove("/data/import.tmp"); uploadAllowed_=false; uploadBytes_=0;
  result(success,error);
  if (success) { restarting_=true; restartAt_=millis()+1500; }
}
void WebServer::notFound() {
  const String prefix = "/api/remotes/";
  if (server_.uri().startsWith(prefix)) {
    const std::string name=ESP8266WebServer::urlDecode(server_.uri().substring(prefix.length())).c_str();
    if (server_.method()==HTTP_GET) { remotes(name); return; }
    if (server_.method()==HTTP_DELETE) { if (!authorized()) return; std::string e; const bool ok=storage.deleteRemote(name,e); result(ok,e); return; }
  }
  if (wifiManager.apActive() && !server_.uri().startsWith("/api/")) {
    if (!authorized()) return;
    server_.sendHeader("Location", "http://192.168.4.1/settings"); server_.send(302,"text/plain",""); return;
  }
  result(false,"Endpoint not found",404);
}
void WebServer::loop() {
  server_.handleClient();
  if (restarting_ && static_cast<int32_t>(millis()-restartAt_)>=0) { mqttManager.stop(); ESP.restart(); }
}
