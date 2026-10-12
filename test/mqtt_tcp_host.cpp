#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "mqtt_manager.h"
#include "discovery_manager.h"
#include "storage.h"
#include "wifi_manager.h"
#include "learning_manager.h"
#include "backup.h"
#include "command_manager.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <deque>
#include <chrono>
#include <cstdio>
Storage storage;
WifiManager wifiManager;
LearningManager learningManager;
namespace {
unsigned sendCalls=0;
std::string hostPath(const std::string& name) {
  if (name!="/data/discovery.json" && name!="/data/discovery.json.tmp") return {};
  return storage.ledgerDirectory+"/"+name.substr(6);
}
}
bool LittleFileOps::read(const std::string& name,std::string& text,size_t max) {
  const auto path=hostPath(name); if (path.empty()) return false;
  std::ifstream file(path,std::ios::binary); if (!file) return false;
  text.assign(std::istreambuf_iterator<char>(file),{}); return text.size()<=max;
}
bool LittleFileOps::write(const std::string& name,const std::string& text) {
  const auto path=hostPath(name); if (path.empty()) return false;
  std::ofstream file(path,std::ios::binary|std::ios::trunc); file.write(text.data(),text.size()); file.close(); return !file.fail();
}
bool LittleFileOps::replace(const std::string& from,const std::string& to) {
  const auto a=hostPath(from),b=hostPath(to); if (a.empty() || b.empty()) return false;
#ifdef _WIN32
  return MoveFileExA(a.c_str(),b.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
  return std::rename(a.c_str(),b.c_str())==0;
#endif
}
bool LittleFileOps::exists(const std::string& name) { const auto path=hostPath(name); return !path.empty() && std::ifstream(path).good(); }
bool LittleFileOps::remove(const std::string& name) { const auto path=hostPath(name); return !path.empty() && std::remove(path.c_str())==0; }
bool sendLearnedCode(const std::string& remote,const std::string& button,std::string& error) {
  // Spy at the physical transmit boundary; command dispatch remains production code.
  if (learningManager.session().active() || !storage.findButton(remote,button)) { error="Button unavailable"; return false; }
  ++sendCalls; return true;
}
int main(int argc,char** argv) {
  if (argc!=3) return 2;
  storage.config().mqttHost="127.0.0.1"; storage.config().mqttPort=static_cast<uint16_t>(std::stoi(argv[1]));
  storage.ledgerDirectory=argv[2];
  Bridge::Remote remote; remote.id="00000001"; remote.name="Ventilator";
  Bridge::Button button; button.id="00000002"; button.name="Power"; button.file="/data/codes/c00000010.json";
  remote.buttons.push_back(button);
  Bridge::Macro scene; scene.id="00000003"; scene.name="Double"; scene.steps={{remote.id,button.id,0},{remote.id,button.id,150}};
  remote.macros.push_back(scene); storage.database.push_back(remote);
  mqttManager.begin(); commandManager.begin(); discoveryManager.begin();
  std::mutex mutex; std::deque<std::string> commands;
  std::thread input([&] {
    std::string line; while (std::getline(std::cin,line)) { { std::lock_guard<std::mutex> lock(mutex); commands.push_back(line); } if (line=="quit") return; }
  });
  bool quit=false;
  while (!quit) {
    std::deque<std::string> pending; { std::lock_guard<std::mutex> lock(mutex); pending.swap(commands); }
    for (const auto& command : pending) {
      if (command=="quit") quit=true;
      else if (command=="status") {
        StaticJsonDocument<768> doc; doc["connected"]=mqttManager.connected(); doc["reconnect_count"]=mqttManager.reconnectCount();
        doc["send_calls"]=sendCalls; doc["learn"]=learningManager.session().status(); doc["discovery"]=discoveryManager.status();
        doc["button_count"]=storage.database[0].buttons.size(); std::string text; serializeJson(doc,text); std::cout<<text<<std::endl;
      } else if (command=="rename") { storage.database[0].buttons[0].name="An/Aus"; storage.notify(); }
      else if (command=="delete") { storage.database[0].macros.clear(); Bridge::eraseButton(storage.database,"00000001","00000002"); storage.notify(); }
      else if (command=="wifi0") wifiManager.online=false;
      else if (command=="wifi1") wifiManager.online=true;
      else if (command=="learn") { DynamicJsonDocument doc(128); doc["confirmations"]=3; std::string error; learningManager.start(doc.as<JsonObjectConst>(),error); }
      else if (command=="cancel") learningManager.cancel();
      else if (command=="receive") {
        Bridge::Signal signal; signal.protocol="NEC"; signal.bits=32; signal.value=0x20DF10EF;
        signal.address=4; signal.command=8; signal.raw={9000,4500};
        for (int bit=31;bit>=0;--bit) { signal.raw.push_back(560); signal.raw.push_back(signal.value&(1ULL<<bit)?1690:560); }
        signal.raw.push_back(560); mqttManager.publishReceived(signal);
      }
    }
    learningManager.loop(); mqttManager.loop(); commandManager.loop(); discoveryManager.loop();
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  mqttManager.stop(); input.join(); return 0;
}
