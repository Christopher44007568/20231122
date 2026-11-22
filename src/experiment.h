#pragma once
#include "renderer.h"
#include "trace.h"
#include <string>
#include <vector>

struct CheckResult {
  std::string name;
  bool pass = false;
  bool expectedNegative = false; // check passes when a mismatch is detected
  std::string detail;
};

struct ExperimentReport {
  std::vector<CheckResult> checks;        // automated conclusions
  std::vector<std::string> evidence;      // key evidence files
  std::vector<std::string> humanItems;    // items requiring human confirmation
  std::vector<std::string> nextSteps;     // executable follow-ups (on failure)
  bool allPass() const;
};

struct ExperimentOptions {
  std::string artifactsDir;
  SceneParams params;
};

// Runs the full experiment: record -> snapshot -> restore -> replay ->
// reorder -> duplicate-submit. Writes trace.jsonl, snapshot.state,
// hashes.json, PNG evidence, and report.md into artifactsDir.
// Returns 0 when all automated checks pass, 1 otherwise, 2 on hard error.
int runExperiment(const ExperimentOptions& opts);

// Serializes/deserializes SceneParams as flat key=value lines.
std::string serializeParams(const SceneParams& p);
bool deserializeParams(const std::string& text, SceneParams& out);
