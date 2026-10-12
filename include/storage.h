#pragma once
#include <LittleFS.h>
#include "model.h"
#include "atomic_store.h"

#include "runtime_config.h"

class FileReader : public Bridge::Reader {
 public:
  explicit FileReader(File& file) : file_(file) {}
  int read() override { if (!(++count_ % 512)) yield(); return file_.read(); }
  bool canAllocate(size_t bytes) const override {
    return ESP.getMaxFreeBlockSize()>=bytes+1024 && ESP.getFreeHeap()>=bytes+8192;
  }
 private:
  File& file_;
  size_t count_ = 0;
};
class FileWriter : public Bridge::Writer {
 public:
  explicit FileWriter(File& file) : file_(file) {}
  bool write(const char* bytes, size_t length) override {
    if (!(++count_ % 128)) yield();
    return file_.write(reinterpret_cast<const uint8_t*>(bytes), length) == length;
  }
 private:
  File& file_;
  size_t count_ = 0;
};
class LittleFileOps : public Bridge::FileOps {
 public:
  bool read(const std::string& path, std::string& content, size_t maxBytes) override;
  bool write(const std::string& path, const std::string& content) override;
  bool replace(const std::string& from, const std::string& to) override;
  bool exists(const std::string& path) override;
  bool remove(const std::string& path) override;
};

class Storage {
 public:
  bool begin();
  bool mounted() const { return mounted_; }
  bool ready() const { return ready_; }
  const std::string& error() const { return error_; }
  const Bridge::Remotes& remotes() const { return remotes_; }
  RuntimeConfig& config() { return config_; }
  const RuntimeConfig& config() const { return config_; }
  Bridge::Remote* findRemote(const std::string& nameOrId);
  Bridge::Button* findButton(const std::string& remote, const std::string& button);
  Bridge::Macro* findMacro(const std::string& remote, const std::string& macro);
  size_t buttonCount() const;
  bool createRemote(const std::string& name, std::string& error, const std::string& layout = "grid");
  bool renameRemote(const std::string& remote, const std::string& name, bool optimistic, std::string& error, const std::string& layout = "");
  bool deleteRemote(const std::string& remote, std::string& error);
  bool deleteButton(const std::string& remote, const std::string& button, std::string& error);
  bool saveMacro(const std::string& remote, JsonObjectConst request, std::string& error);
  bool deleteMacro(const std::string& remote, const std::string& macro, std::string& error);
  bool updateLayout(const std::string& remote, JsonObjectConst request, std::string& error);
  bool saveButton(const std::string& remote, const std::string& name, const Bridge::Signal& signal,
                  const Bridge::Button* settings, const std::string& replaceId, std::string& error);
  bool updateButton(const std::string& remote, const std::string& button, const std::string& name,
                    const std::string& entityType, Bridge::SendMode mode, uint8_t repeats,
                    uint32_t frequency, const std::string& icon, const std::string& slot, bool favorite, std::string& error);
  bool loadSignal(const Bridge::Button& button, Bridge::Signal& signal, std::string& error);
  bool saveConfig(const RuntimeConfig& config, std::string& error);
  bool commitIndex(Bridge::Remotes next, std::string& error);
  bool writeCode(const Bridge::Signal& signal, std::string& path, std::string& error);
  bool exportBackup(const char* path, std::string& error);
  bool importBackup(const char* path, std::string& error);
  void onChanged(void (*handler)()) { changed_ = handler; }
  static void configToJson(const RuntimeConfig& config, JsonObject json, bool passwords);
  static bool configFromJson(JsonObjectConst json, RuntimeConfig& config, std::string& error, bool requireWifi = false);
  static std::string newId();
 private:
  bool validateIndex(const std::string& content, Bridge::Remotes& next, std::string& error);
  void cleanupCodes();
  Bridge::Remotes remotes_;
  RuntimeConfig config_;
  LittleFileOps files_;
  bool mounted_ = false;
  bool ready_ = false;
  std::string error_;
  void (*changed_)() = nullptr;
};
extern Storage storage;
