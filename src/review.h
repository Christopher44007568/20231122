#pragma once

#include <string>
#include <vector>

#include "diff.h"
#include "renderer.h"
#include "util.h"

namespace tar {

struct ReviewOptions {
  std::string out_dir = "artifacts";   // root for evidence + tmp
  int frames = 6;
  int grid = 4;
  std::vector<int> inject_fault_objs;  // applied to the v2 (extension) run
  std::string shader_dir;
};

// Full reviewable pipeline:
//   1. capture baseline run (v1, legacy TAPS 1 snapshot)
//   2. capture extension run (v2, current TAPS 2 snapshot)
//   3. re-parse both snapshots through the compat reader
//   4. correlate draw calls / resource states / framebuffers / diffs
//   5. write report.md + report.json evidence
//   6. remove the temporary staging directory
// Returns 0 on success (individual object/frame failures are recorded
// in the report, not treated as fatal).
int RunReview(const ReviewOptions& opt, Log& log);

// Runs a single capture and writes its snapshot. Used by the `capture`
// subcommand and by RunReview.
Snapshot RunCapture(const RenderConfig& cfg, int snapshot_format,
                    const std::string& snapshot_path, Log& log);

}  // namespace tar
