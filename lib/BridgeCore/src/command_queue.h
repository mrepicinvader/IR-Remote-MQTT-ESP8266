#pragma once
#include "model.h"
#include <deque>
#include <functional>

namespace Bridge {
struct CommandResult {
  std::string requestId, source, status = "queued", error;
  size_t completed = 0, total = 0;
  uint32_t updatedMs = 0;
};
void commandResultToJson(const CommandResult& result, JsonObject json);
bool validRequestId(const std::string& id);

// Only references are queued; RAW timings are loaded for one send at a time.
class CommandQueue {
 public:
  using Executor = std::function<bool(const CommandStep&, std::string&)>;
  using Clock = std::function<uint32_t()>;
  using Listener = std::function<void(const CommandResult&)>;
  bool enqueue(const std::string& id, const std::vector<CommandStep>& steps,
               const std::string& source, uint32_t now, CommandResult& result, std::string& error);
  void tick(uint32_t now, const Executor& execute, const Clock& clock);
  void failAll(const std::string& error, uint32_t now);
  bool cancel(const std::string& id, uint32_t now, std::string& error);
  size_t pending() const { return jobs_.size(); }
  const std::deque<CommandResult>& history() const { return history_; }
  const CommandResult* find(const std::string& id) const;
  void onResult(Listener listener) { listener_ = std::move(listener); }
 private:
  struct Job { std::string id; std::vector<CommandStep> steps; size_t step = 0; uint32_t due = 0; bool started = false; };
  void changed(const std::string& id, const char* status, const std::string& error, size_t completed, uint32_t now);
  std::deque<Job> jobs_;
  std::deque<CommandResult> history_;
  Listener listener_;
  uint32_t earliestSend_ = 0;
  bool hasSent_ = false;
};
}
