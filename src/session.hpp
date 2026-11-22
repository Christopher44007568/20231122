#pragma once

#include "compare.hpp"
#include "params.hpp"
#include "scene.hpp"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct RunEvidence {
  bool valid = false;
  BiasCandidate bias;
  CompareResult compare;
  double score = 0.0;
  double elapsedMs = 0.0;
};

struct ObjectRun {
  std::uint32_t id = 0;
  std::string name;
  RunEvidence extreme;
  RunEvidence corrected;
  bool hasCorrected = false;
  std::string evidenceDir;
  bool evidenceComplete = false;
};

struct SessionState {
  std::string sessionId;
  std::string createdAt;
  std::string updatedAt;
  int selectedIndex = 4;
  int presetIndex = 1;
  int viewMode = 2;
  ScreenPoint cursor{640, 360};
  bool resumed = false;
  std::string note;
  std::vector<ObjectRun> runs;
};

class SessionStore {
 public:
  bool initialize(const std::filesystem::path& root, bool fresh,
                  const Scene& scene, std::string& error);
  bool save(std::string& error);

  SessionState& state() { return state_; }
  const SessionState& state() const { return state_; }
  const std::filesystem::path& root() const { return root_; }

  std::filesystem::path objectEvidenceDir(int index) const;
  std::filesystem::path baselineEvidencePath() const;
  bool evidenceFilesPresent(const ObjectRun& run) const;

 private:
  std::filesystem::path root_;
  SessionState state_;
  bool load(std::string& error);
};

std::string utcNow();

