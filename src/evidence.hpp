#pragma once

#include <fstream>
#include <map>
#include <string>

#include <picojson.h>

#include "replay.hpp"

namespace bbox {

struct StateView {
  picojson::value models;
  std::string fingerprint;
};

StateView captureState(const ReplayEngine& engine);
picojson::value stateEnvelope(const StateView& state);
picojson::value outcomeJson(const ApplyOutcome& outcome);

void ensureDirectory(const std::string& path);
void writeTextFile(const std::string& path, const std::string& contents);
void writeJsonFile(const std::string& path, const picojson::value& value);

class RunEvidence {
 public:
  RunEvidence(const std::string& path, std::string run_name);

  void logStep(std::size_t step, int source_line,
               const picojson::value& input, const StateView& before,
               const ApplyOutcome& outcome, const StateView& after);
  void logRejected(std::size_t step, int source_line,
                   const picojson::value& input, const std::string& detail,
                   const StateView& before);

  const std::string& runName() const { return run_name_; }
  std::size_t acceptedCount() const { return accepted_; }
  std::size_t duplicateCount() const { return duplicate_; }
  std::size_t staleCount() const { return stale_; }
  std::size_t exceptionCount() const { return exception_; }
  std::size_t rejectedCount() const { return rejected_; }
  std::size_t postExceptionSuccesses() const { return post_exception_successes_; }
  int firstExceptionStep() const { return first_exception_step_; }
  const std::string& finalFingerprint() const { return final_fingerprint_; }

  picojson::value summaryJson() const;

 private:
  void consumeOutcome(const ApplyOutcome& outcome);

  std::ofstream log_;
  std::string run_name_;
  std::size_t step_count_ = 0;
  std::size_t accepted_ = 0;
  std::size_t duplicate_ = 0;
  std::size_t stale_ = 0;
  std::size_t exception_ = 0;
  std::size_t rejected_ = 0;
  std::size_t post_exception_successes_ = 0;
  int first_exception_step_ = -1;
  std::string final_fingerprint_;
};

}  // namespace bbox
