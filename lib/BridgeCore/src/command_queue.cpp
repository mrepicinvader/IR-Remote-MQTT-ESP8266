#include "command_queue.h"
#include <algorithm>
#include <cctype>

namespace Bridge {
bool validRequestId(const std::string& id) {
  return !id.empty() && id.size() <= 48 && std::all_of(id.begin(), id.end(), [](unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
  });
}
void commandResultToJson(const CommandResult& result, JsonObject json) {
  json["request_id"] = result.requestId; json["source"] = result.source; json["status"] = result.status;
  json["success"] = result.status != "failed";
  json["completed_steps"] = result.completed; json["total_steps"] = result.total; json["updated_ms"] = result.updatedMs;
  if (!result.error.empty()) json["error"] = result.error;
}
const CommandResult* CommandQueue::find(const std::string& id) const {
  for (const auto& result : history_) if (result.requestId == id) return &result;
  return nullptr;
}
bool CommandQueue::enqueue(const std::string& id, const std::vector<CommandStep>& steps,
                           const std::string& source, uint32_t now, CommandResult& result, std::string& error) {
  if (!validRequestId(id)) { error = "request_id permits 1..48 letters, digits, dots, hyphens and underscores"; return false; }
  if (find(id)) { error = "request_id already used in this session's recent history"; return false; }
  if (jobs_.size() >= Config::MAX_QUEUED_COMMANDS) { error = "Send queue full"; return false; }
  if (steps.empty() || steps.size() > Config::MAX_MACRO_STEPS) { error = "Invalid command step count"; return false; }
  uint32_t delays = 0;
  for (const auto& step : steps) {
    if (!validId(step.remote) || !validId(step.button) || step.delayMs > Config::MAX_STEP_DELAY_MS ||
        (delays += step.delayMs) > Config::MAX_MACRO_DELAY_MS) { error = "Invalid command step/delay"; return false; }
  }
  if (history_.size() >= Config::COMMAND_HISTORY_SIZE) {
    const auto old = std::find_if(history_.begin(), history_.end(), [](const CommandResult& r) { return r.status != "queued"; });
    if (old == history_.end()) { error = "Command history full"; return false; }
    history_.erase(old);
  }
  Job job; job.id = id; job.steps = steps;
  jobs_.push_back(std::move(job));
  result = CommandResult{}; result.requestId = id; result.source = source; result.total = steps.size(); result.updatedMs = now;
  history_.push_back(result); error.clear();
  if (listener_) listener_(result);
  return true;
}
void CommandQueue::changed(const std::string& id, const char* status, const std::string& error, size_t completed, uint32_t now) {
  for (auto& result : history_) if (result.requestId == id) {
    result.status = status; result.error = error; result.completed = completed; result.updatedMs = now;
    if (listener_) listener_(result);
    return;
  }
}
void CommandQueue::tick(uint32_t now, const Executor& execute, const Clock& clock) {
  if (hasSent_ && static_cast<uint32_t>(now-(earliestSend_-Config::REPEAT_GAP_US/1000))>=Config::REPEAT_GAP_US/1000) hasSent_=false;
  if (jobs_.empty()) return;
  auto& job = jobs_.front();
  if (!job.started) { job.started = true; job.due = now + job.steps.front().delayMs; }
  if (static_cast<int32_t>(now - job.due) < 0) return;
  if (hasSent_) return;
  std::string error;
  if (!execute(job.steps[job.step], error)) {
    changed(job.id, "failed", error.empty() ? "IR transmission failed" : error, job.step, clock());
    jobs_.pop_front(); return;
  }
  ++job.step;
  const auto after = clock();
  hasSent_ = true; earliestSend_ = after + Config::REPEAT_GAP_US/1000;
  if (job.step == job.steps.size()) { changed(job.id, "sent", "", job.step, after); jobs_.pop_front(); }
  else { job.due = after + job.steps[job.step].delayMs; changed(job.id, "queued", "", job.step, after); }
}
void CommandQueue::failAll(const std::string& error, uint32_t now) {
  while (!jobs_.empty()) {
    changed(jobs_.front().id, "failed", error, jobs_.front().step, now); jobs_.pop_front();
  }
}
bool CommandQueue::cancel(const std::string& id, uint32_t now, std::string& error) {
  const auto job = std::find_if(jobs_.begin(), jobs_.end(), [&](const Job& item) { return item.id == id; });
  if (job == jobs_.end()) { error = "No queued command with this request_id"; return false; }
  changed(job->id, "failed", "Cancelled", job->step, now); jobs_.erase(job); error.clear(); return true;
}
}
