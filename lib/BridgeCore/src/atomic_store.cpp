#include "atomic_store.h"

namespace Bridge {
bool atomicWrite(FileOps& files, const std::string& path, const std::string& content,
                 const Validator& validate, std::string& error) {
  const std::string temp = path + ".tmp";
  if (!files.write(temp, content)) { error = "Temporary file write failed"; return false; }
  std::string verified;
  if (!files.read(temp, verified, content.size() + 1) || verified != content || !validate(verified, error)) {
    files.remove(temp);
    if (error.empty()) error = "Read-back validation failed";
    return false;
  }
  // LittleFS lfs_rename replaces the old file atomically. Never unlink the live
  // file first: that would create a gap in which a power cut loses configuration.
  if (!files.replace(temp, path)) { error = "Atomic file replacement failed"; return false; }
  error.clear(); return true;
}
}
