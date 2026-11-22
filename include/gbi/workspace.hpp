#pragma once

#include "gbi/evidence.hpp"
#include "gbi/scene.hpp"
#include "gbi/util.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gbi {

struct SessionState {
  std::map<std::string, std::string> values;

  std::string get(const std::string& key,
                  const std::string& fallback = {}) const;
  void set(const std::string& key, const std::string& value);
  std::uint64_t number(const std::string& key,
                       std::uint64_t fallback = 0) const;
};

struct JobRecord {
  std::string job_id;
  std::string status;
  std::string operation;
  std::string anchor;
  std::string object_id;
  std::string capture_id;
  std::string revision;
  std::string note;
  std::string started_utc;
  std::int64_t started_unix_ms = 0;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::string staging_dir;
  std::string final_dir;
  std::string error;

  std::string toText() const;
  static JobRecord fromText(const std::string& text);
};

struct OriginalManifest {
  std::string source_path;
  std::string imported_path;
  std::string sha256;
  std::uint64_t size = 0;
  std::string imported_at;

  std::string toJson() const;
  static OriginalManifest fromJson(const Json& json);
};

struct RecoveryReport {
  bool had_pending = false;
  JobRecord pending;
  std::vector<std::string> actions;
};

class Workspace {
 public:
  explicit Workspace(fs::path root);

  static Workspace initialize(const fs::path& root,
                              const fs::path& original_asset,
                              bool force);
  static Workspace open(const fs::path& root);
  static Workspace ensure(const fs::path& root);

  const fs::path& root() const;
  const fs::path& originalsDir() const;
  const fs::path& stateDir() const;
  const fs::path& capturesDir() const;
  const fs::path& comparisonsDir() const;
  const fs::path& exportsDir() const;
  const fs::path& journalPath() const;
  const fs::path& sessionPath() const;
  const fs::path& originalManifestPath() const;
  const fs::path& originalScenePath() const;

  bool exists() const;
  void require() const;
  Scene loadScene(bool apply_overlays = true) const;
  void loadOverlays(Scene& scene) const;
  fs::path overlayPath(const std::string& object_id) const;
  void writeOverlay(const ObjectOverlay& overlay);
  bool clearOverlay(const std::string& object_id);
  std::optional<ObjectOverlay> readOverlay(
      const std::string& object_id) const;

  SessionState loadSession() const;
  void saveSession(const SessionState& session);
  void mutateSession(const std::function<void(SessionState&)>& mutation);
  std::uint64_t nextGeneration();

  OriginalManifest loadOriginalManifest() const;
  bool verifyOriginals(bool repair_permissions = false) const;
  void recordOriginalsVerified();

  JobRecord beginJob(const std::string& operation, const std::string& anchor,
                     const std::string& object_id,
                     const std::string& capture_id,
                     const std::string& revision, const std::string& note,
                     std::uint32_t width, std::uint32_t height);
  void updateJob(const JobRecord& job);
  void completeJob(const JobRecord& job);
  void failJob(const JobRecord& job, const std::string& error);
  std::optional<JobRecord> pendingJob() const;
  std::optional<JobRecord> loadJob(const std::string& job_id) const;
  std::vector<JobRecord> listJobs() const;

  RecoveryReport recover();
  void journal(const std::string& event, const Json& fields = Json::object());
  void recordCapture(const CaptureEvidence& capture,
                     const fs::path& capture_dir);
  std::optional<CaptureEvidence> loadCapture(
      const std::string& anchor, const std::string& capture_id = {}) const;
  std::optional<CaptureEvidence> loadCaptureAt(
      const fs::path& capture_dir) const;
  std::vector<fs::path> listCaptureDirectories() const;
  fs::path captureDirectory(const CaptureEvidence& capture) const;

  std::string latestCaptureId(const std::string& anchor,
                              const std::string& object_id = {}) const;
  fs::path lock() const;

 private:
  void writeOriginalManifest(const OriginalManifest& manifest) const;
  void ensureLayout() const;

  fs::path root_;
};

class WorkspaceLock {
 public:
  explicit WorkspaceLock(const Workspace& workspace);
  ~WorkspaceLock();
  WorkspaceLock(const WorkspaceLock&) = delete;
  WorkspaceLock& operator=(const WorkspaceLock&) = delete;

 private:
  int fd_ = -1;
};

class CaptureSession {
 public:
  explicit CaptureSession(Workspace workspace);

  CaptureEvidence capture(const std::string& anchor,
                          const std::string& object_id,
                          const std::string& revision,
                          const std::string& note,
                          std::uint32_t width, std::uint32_t height,
                          const std::string& requested_capture_id = {},
                          const std::string& requested_job_id = {});

  Workspace& workspace();

 private:
  Workspace workspace_;
};

std::string newCaptureId(const std::string& anchor);

}  // namespace gbi
