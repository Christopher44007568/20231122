#pragma once
#include <string>
#include <vector>

#include "camtrak/store.hpp"
#include "camtrak/types.hpp"

namespace camtrak {

struct EngineReport {
  RunResult result;
  RunLogEntry log_entry;
  bool resumed = false;
};

class Engine {
 public:
  explicit Engine(Store& store) : store_(store) {}

  // Runs (or resumes after an interrupted session) the correction for one
  // object. The same config always produces byte-identical corrected samples;
  // resuming simply re-executes deterministically and reuses the run id.
  EngineReport runObject(const CorrectionConfig& config);

  // Recomputes a stored run from the immutable snapshot and asserts equality
  // with the recorded result. Returns an empty failure list when reviewable.
  std::vector<std::string> verifyRun(const std::string& run_id) const;

  // Side-by-side comparison of two runs of the same object, for choosing
  // between e.g. linear and cubic corrections.
  struct Comparison {
    std::string run_a;
    std::string run_b;
    std::size_t common_samples = 0;
    double max_position_delta = 0.0;
    double rmse_position_delta = 0.0;
    double max_angle_delta_rad = 0.0;
  };
  Comparison compare(const std::string& run_id_a,
                     const std::string& run_id_b) const;

  static std::string canonicalConfigHash(const CorrectionConfig& config);

 private:
  Store& store_;
};

}  // namespace camtrak
