#pragma once
#include "runtime_config.h"
#include "model.h"
#include "atomic_store.h"
class Storage {
 public:
  RuntimeConfig& config() { return configuration; }
  const Bridge::Remotes& remotes() const { return database; }
  bool mounted() const { return true; }
  bool ready() const { return true; }
  void onChanged(void (*handler)()) { changed=handler; }
  void notify() { if (changed) changed(); }
  Bridge::Remote* findRemote(const std::string& name) {
    for (auto& r : database) if (r.id==name || r.name==name) return &r;
    return nullptr;
  }
  Bridge::Macro* findMacro(const std::string& remote,const std::string& name) {
    auto* r=findRemote(remote); if (!r) return nullptr;
    for (auto& macro : r->macros) if (macro.id==name || macro.name==name) return &macro;
    return nullptr;
  }
  static std::string newId() { static unsigned id=100; char text[9]; snprintf(text,sizeof(text),"%08x",++id); return text; }
  Bridge::Button* findButton(const std::string& remote,const std::string& name) {
    for (auto& r : database) if (r.id==remote || r.name==remote)
      for (auto& b : r.buttons) if (b.id==name || b.name==name) return &b;
    return nullptr;
  }
  RuntimeConfig configuration;
  Bridge::Remotes database;
  std::string ledgerDirectory;
 private: void (*changed)()=nullptr;
};
extern Storage storage;
// Actual host files used to exercise the production DiscoveryManager ledger.
class LittleFileOps : public Bridge::FileOps {
 public:
  bool read(const std::string&,std::string&,size_t) override;
  bool write(const std::string&,const std::string&) override;
  bool replace(const std::string&,const std::string&) override;
  bool exists(const std::string&) override;
  bool remove(const std::string&) override;
};
