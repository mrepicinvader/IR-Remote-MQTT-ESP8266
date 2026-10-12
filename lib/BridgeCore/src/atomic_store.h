#pragma once
#include <functional>
#include <string>

namespace Bridge {
class FileOps {
 public:
  virtual ~FileOps() = default;
  virtual bool read(const std::string& path, std::string& content, size_t maxBytes) = 0;
  virtual bool write(const std::string& path, const std::string& content) = 0;
  virtual bool replace(const std::string& from, const std::string& to) = 0;
  virtual bool exists(const std::string& path) = 0;
  virtual bool remove(const std::string& path) = 0;
};
using Validator = std::function<bool(const std::string&, std::string&)>;
bool atomicWrite(FileOps& files, const std::string& path, const std::string& content,
                 const Validator& validate, std::string& error);
}
