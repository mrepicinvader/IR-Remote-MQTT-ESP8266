#pragma once
#include <functional>
#include "model.h"

namespace Bridge {
using CodeStager = std::function<bool(const Signal&, std::string& path, std::string& error)>;
// Validates every code and stages immutable files. Caller commits the resulting
// index only if this function AND configuration validation both succeed.
bool readBackup(Reader& reader, Remotes& remotes, std::string& configuration,
                const CodeStager& stage, std::string& error);
bool validateRemotes(const Remotes& remotes, std::string& error);
bool eraseButton(Remotes& remotes, const std::string& remoteId, const std::string& buttonId);
}
