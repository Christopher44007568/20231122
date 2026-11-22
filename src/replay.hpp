#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <unordered_set>

#include "model.hpp"

namespace bbox {

enum class ApplyKind {
  Accepted,
  Duplicate,
  Stale,
  Exception,
};

struct ApplyOutcome {
  ApplyKind kind = ApplyKind::Exception;
  std::string detail;
  bool state_changed = false;
};

const char* applyKindName(ApplyKind kind);

class ReplayEngine {
 public:
  explicit ReplayEngine(const Snapshot& snapshot);

  ApplyOutcome apply(const Update& update);
  void restoreSeenEventIds(const std::unordered_set<std::string>& event_ids);
  const std::map<std::string, Model>& models() const { return models_; }
  const std::unordered_set<std::string>& seenEventIds() const {
    return seen_event_ids_;
  }

  std::string fingerprint() const;
  bool matches(const Snapshot& expected) const;
  picojson::value stateJson() const;

 private:
  Model& mutableModel(const std::string& id);

  std::map<std::string, Model> models_;
  std::unordered_set<std::string> seen_event_ids_;
};

}  // namespace bbox

