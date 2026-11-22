#pragma once
#include <string>

namespace pcd {

// On-disk layout of a correction workspace:
// <root>/workspace.json        manifest (created UTC, original hash)
// <root>/original/cloud.bin    pristine, read-only copy of the material
// <root>/original/cloud.sha256 hash of the pristine copy
// <root>/params.txt            extreme parameter sets
// <root>/runs/<id>/            one duplicated run object per param set
struct Workspace {
  std::string root;

  std::string manifest_path() const { return root + "/workspace.json"; }
  std::string original_bin() const { return root + "/original/cloud.bin"; }
  std::string original_sha() const { return root + "/original/cloud.sha256"; }
  std::string params_path() const { return root + "/params.txt"; }
  std::string runs_dir() const { return root + "/runs"; }
  std::string run_dir(const std::string& id) const {
    return runs_dir() + "/" + id;
  }
  std::string run_json(const std::string& id) const {
    return run_dir(id) + "/run.json";
  }
  std::string run_working(const std::string& id) const {
    return run_dir(id) + "/working.bin";
  }
  std::string run_checkpoint(const std::string& id) const {
    return run_dir(id) + "/checkpoint.json";
  }
  std::string run_journal(const std::string& id) const {
    return run_dir(id) + "/edits.journal";
  }
  std::string run_output(const std::string& id) const {
    return run_dir(id) + "/output.bin";
  }
  std::string run_evidence(const std::string& id) const {
    return run_dir(id) + "/evidence";
  }
};

// Create a workspace: preserves an unmodified, read-only copy of the
// original material plus its SHA-256, and seeds extreme parameter sets.
void ws_init(const Workspace& ws, const std::string& source_cloud);

// Verify the pristine copy still matches its recorded hash.
bool ws_original_intact(const Workspace& ws, std::string* actual_hash);

// Read a scalar field out of a flat JSON object (naive but sufficient
// for the manifests this tool writes itself).
std::string json_get(const std::string& json, const std::string& key);

} // namespace pcd
