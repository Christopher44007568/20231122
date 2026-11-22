#pragma once

#include <map>
#include <string>
#include <vector>

#include "camtrak/json.hpp"
#include "camtrak/types.hpp"

namespace camtrak {

// On-disk layout (all paths relative to the project root directory):
//
//   project.json              project metadata, clock mapping, object index
//   inputs/<object>.json      original captured trajectory (user-provided)
//   evidence/snapshots/<sha>/<object>.json
//                             immutable byte-for-byte input snapshot
//   evidence/manifest.json    hashes covering project + every snapshot
//   evidence/runs.log.jsonl   append-only, hash-chained run records
//   session.json              resumable per-object run state (atomic writes)
//   runs/<run_id>.json        full result of every completed/partial run
//   exports/<batch_id>/...    independently reviewable export bundle

struct ClockMapping {
  // Reference time = device time * scale + offset, per device id.
  std::map<std::string, double> offset_seconds;
  std::map<std::string, double> scale;

  double toReference(const std::string& device_id, double device_time) const;
};

struct Project {
  std::string project_id;
  std::string created_at;
  std::string description;
  ClockMapping clocks;
  std::vector<std::string> object_ids;
  std::map<std::string, std::string> input_files;  // object -> relative path
};

struct RunState {
  std::string run_id;
  std::string object_id;
  std::string status;       // running | completed | failed
  std::string started_at;
  std::string updated_at;
  std::string config_hash;
  CorrectionConfig config;
  std::string last_error;
};

struct RunLogEntry {
  std::string run_id;
  std::string object_id;
  std::string status;
  std::string recorded_at;
  std::string config_hash;
  std::string result_hash;
  std::string prev_entry_hash;
  std::string entry_hash;
};

class Store {
 public:
  // Opens (or creates) a project rooted at `root`. Throws on malformed data.
  explicit Store(std::string root);

  const std::string& root() const { return root_; }

  // ---- project lifecycle ------------------------------------------------
  static void initialize(const std::string& root, const Project& project);
  Project loadProject() const;
  void saveProject(const Project& project) const;

  Trajectory loadTrajectory(const std::string& object_id) const;
  // Writes the input trajectory and returns its sha-256 hex digest.
  std::string saveTrajectory(const std::string& object_id,
                             const Trajectory& trajectory) const;

  // Maps a trajectory onto the reference timeline in place.
  Trajectory toReferenceTimeline(const Trajectory& trajectory) const;

  // ---- evidence ---------------------------------------------------------
  std::string snapshotInput(const std::string& object_id,
                            const std::string& input_rel_path) const;
  JsonValue loadEvidenceManifest() const;
  void rebuildEvidenceManifest();
  bool verifyEvidence(std::vector<std::string>* failures) const;

  // ---- session / runs ---------------------------------------------------
  std::map<std::string, RunState> loadSession() const;
  void saveSessionState(const RunState& state) const;
  void clearSessionState(const std::string& object_id) const;

  void saveRunResult(const RunResult& result) const;
  RunResult loadRunResult(const std::string& run_id) const;
  std::vector<std::string> listRunIds() const;

  RunLogEntry appendRunLog(const RunResult& result, const std::string& status,
                           const std::string& config_hash);
  std::vector<RunLogEntry> readRunLog() const;
  bool verifyRunLog(std::vector<std::string>* failures) const;

  // ---- export -----------------------------------------------------------
  // Creates exports/<batch_id>/ with trajectories, per-run evidence and a
  // review manifest. Returns the absolute export directory.
  std::string createExport(const std::string& batch_id,
                           const std::vector<std::string>& run_ids,
                           const std::string& reviewer_note) const;

 private:
  std::string root_;
};

// Serialization helpers (exposed for tests and the GUI/CLI).
JsonValue toJson(const Project& project);
Project projectFromJson(const JsonValue& value);
JsonValue toJson(const Trajectory& trajectory);
Trajectory trajectoryFromJson(const JsonValue& value);
JsonValue toJson(const CorrectionConfig& config);
CorrectionConfig configFromJson(const JsonValue& value);
JsonValue toJson(const RunResult& result);
RunResult resultFromJson(const JsonValue& value);
JsonValue toJson(const Metrics& metrics);
JsonValue toJson(const RunState& state);
RunState stateFromJson(const JsonValue& value);

std::string readFile(const std::string& path);
void writeFileAtomic(const std::string& path, const std::string& contents);
std::string utcNowIso8601();
std::string makeRunId(const std::string& object_id);

}  // namespace camtrak
