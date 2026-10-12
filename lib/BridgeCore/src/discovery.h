#pragma once
#include "model.h"

namespace Bridge {
struct DiscoveryContext {
  std::string deviceId, prefix, baseTopic, ip;
};
std::string discoveryTopic(const DiscoveryContext& context, const char* component, const std::string& id);
std::vector<std::string> discoveryTopics(const Remotes& remotes, const DiscoveryContext& context);
bool discoveryPayload(const Remotes& remotes, const DiscoveryContext& context,
                      const std::string& topic, std::string& payload);
std::vector<std::string> mergeDiscoveryHistory(const std::vector<std::string>& history,
                                             const std::vector<std::string>& desired);
bool validDiscoveryTopic(const std::string& topic);
}
