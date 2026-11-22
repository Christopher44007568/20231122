#include "replay.hpp"

#include <cmath>
#include <stdexcept>

#include "sha256.hpp"

namespace bbox {

const char* applyKindName(ApplyKind kind) {
  switch (kind) {
    case ApplyKind::Accepted:
      return "accepted";
    case ApplyKind::Duplicate:
      return "duplicate";
    case ApplyKind::Stale:
      return "stale";
    case ApplyKind::Exception:
      return "exception";
  }
  return "unknown";
}

ReplayEngine::ReplayEngine(const Snapshot& snapshot)
    : models_(snapshot.models) {}

void ReplayEngine::restoreSeenEventIds(
    const std::unordered_set<std::string>& event_ids) {
  seen_event_ids_ = event_ids;
}

Model& ReplayEngine::mutableModel(const std::string& id) {
  const auto it = models_.find(id);
  if (it == models_.end()) {
    throw std::invalid_argument("unknown object_id: " + id);
  }
  return it->second;
}

ApplyOutcome ReplayEngine::apply(const Update& update) {
  if (update.event_id.empty()) {
    return {ApplyKind::Exception, "event_id is empty", false};
  }
  if (update.object_id.empty()) {
    return {ApplyKind::Exception, "object_id is empty", false};
  }
  if (seen_event_ids_.count(update.event_id) != 0) {
    return {ApplyKind::Duplicate,
            "event_id already consumed: " + update.event_id, false};
  }

  try {
    if (!update.bounds.finite()) {
      throw std::invalid_argument("bounds contain a non-finite value");
    }
    if (!update.bounds.ordered()) {
      throw std::invalid_argument(
          "bounds invariant violated: min must be <= max on every axis");
    }
    if (update.version == 0) {
      throw std::invalid_argument("version must be positive");
    }

    Model& model = mutableModel(update.object_id);
    seen_event_ids_.insert(update.event_id);
    if (update.version < model.version) {
      return {ApplyKind::Stale,
              "incoming version " + std::to_string(update.version) +
                  " is older than current version " +
                  std::to_string(model.version),
              false};
    }
    if (update.version == model.version) {
      return {ApplyKind::Stale,
              "version already current; state retained", false};
    }

    model.version = update.version;
    model.bounds = update.bounds;
    model.last_event = update.event_id;
    return {ApplyKind::Accepted, "model bounds and version committed", true};
  } catch (const std::exception& exception) {
    return {ApplyKind::Exception, exception.what(), false};
  }
}

std::string ReplayEngine::fingerprint() const {
  return sha256Hex(modelsCanonicalText(models_));
}

bool ReplayEngine::matches(const Snapshot& expected) const {
  if (models_.size() != expected.models.size()) {
    return false;
  }
  for (const auto& entry : expected.models) {
    const auto it = models_.find(entry.first);
    if (it == models_.end()) {
      return false;
    }
    const Model& actual = it->second;
    const Model& wanted = entry.second;
    if (actual.version != wanted.version ||
        actual.last_event != wanted.last_event ||
        !actual.bounds.equals(wanted.bounds)) {
      return false;
    }
  }
  return true;
}

picojson::value ReplayEngine::stateJson() const {
  return modelsJson(models_);
}

}  // namespace bbox
