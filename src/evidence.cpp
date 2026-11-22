#include "evidence.hpp"

#include <filesystem>
#include <stdexcept>
#include <utility>

#include "sha256.hpp"

namespace bbox {

StateView captureState(const ReplayEngine& engine) {
  return {engine.stateJson(), engine.fingerprint()};
}

picojson::value stateEnvelope(const StateView& state) {
  picojson::object object;
  object["fingerprint"] = picojson::value(state.fingerprint);
  object["models"] = state.models;
  return picojson::value(object);
}

picojson::value outcomeJson(const ApplyOutcome& outcome) {
  picojson::object object;
  object["kind"] = picojson::value(applyKindName(outcome.kind));
  object["detail"] = picojson::value(outcome.detail);
  object["state_changed"] = picojson::value(outcome.state_changed);
  return picojson::value(object);
}

void ensureDirectory(const std::string& path) {
  if (path.empty()) {
    return;
  }
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) {
    throw std::runtime_error("cannot create directory " + path + ": " +
                             error.message());
  }
}

void writeTextFile(const std::string& path, const std::string& contents) {
  const std::filesystem::path output_path(path);
  ensureDirectory(output_path.parent_path().string());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot write " + path);
  }
  output << contents;
  if (!output) {
    throw std::runtime_error("failed while writing " + path);
  }
}

void writeJsonFile(const std::string& path, const picojson::value& value) {
  writeTextFile(path, prettyJson(value));
}

RunEvidence::RunEvidence(const std::string& path, std::string run_name)
    : log_(path, std::ios::binary | std::ios::trunc),
      run_name_(std::move(run_name)) {
  if (!log_) {
    throw std::runtime_error("cannot open step log " + path);
  }
}

void RunEvidence::consumeOutcome(const ApplyOutcome& outcome) {
  if (exception_ > 0 && outcome.kind != ApplyKind::Exception) {
    ++post_exception_successes_;
  }
  switch (outcome.kind) {
    case ApplyKind::Accepted:
      ++accepted_;
      break;
    case ApplyKind::Duplicate:
      ++duplicate_;
      break;
    case ApplyKind::Stale:
      ++stale_;
      break;
    case ApplyKind::Exception:
      if (exception_ == 0) {
        first_exception_step_ = static_cast<int>(step_count_);
      }
      ++exception_;
      break;
  }
}

void RunEvidence::logStep(std::size_t step, int source_line,
                          const picojson::value& input,
                          const StateView& before,
                          const ApplyOutcome& outcome,
                          const StateView& after) {
  ++step_count_;
  consumeOutcome(outcome);
  final_fingerprint_ = after.fingerprint;

  picojson::object record;
  record["run"] = picojson::value(run_name_);
  record["step"] = picojson::value(static_cast<double>(step));
  record["source_line"] = picojson::value(static_cast<double>(source_line));
  record["input"] = input;
  record["state_before"] = stateEnvelope(before);
  record["result"] = outcomeJson(outcome);
  record["state_after"] = stateEnvelope(after);
  log_ << compactJson(picojson::value(record)) << '\n';
  log_.flush();
  if (!log_) {
    throw std::runtime_error("failed to append step evidence");
  }
}

void RunEvidence::logRejected(std::size_t step, int source_line,
                              const picojson::value& input,
                              const std::string& detail,
                              const StateView& before) {
  ++step_count_;
  ++rejected_;
  final_fingerprint_ = before.fingerprint;

  picojson::object result;
  result["kind"] = picojson::value("rejected");
  result["detail"] = picojson::value(detail);
  result["state_changed"] = picojson::value(false);

  picojson::object record;
  record["run"] = picojson::value(run_name_);
  record["step"] = picojson::value(static_cast<double>(step));
  record["source_line"] = picojson::value(static_cast<double>(source_line));
  record["input"] = input;
  record["state_before"] = stateEnvelope(before);
  record["result"] = picojson::value(result);
  record["state_after"] = stateEnvelope(before);
  log_ << compactJson(picojson::value(record)) << '\n';
  log_.flush();
  if (!log_) {
    throw std::runtime_error("failed to append rejected-record evidence");
  }
}

picojson::value RunEvidence::summaryJson() const {
  picojson::object counters;
  counters["steps"] = picojson::value(static_cast<double>(step_count_));
  counters["accepted"] = picojson::value(static_cast<double>(accepted_));
  counters["duplicate"] = picojson::value(static_cast<double>(duplicate_));
  counters["stale"] = picojson::value(static_cast<double>(stale_));
  counters["exception"] = picojson::value(static_cast<double>(exception_));
  counters["rejected"] = picojson::value(static_cast<double>(rejected_));

  picojson::object object;
  object["run"] = picojson::value(run_name_);
  object["counters"] = picojson::value(counters);
  object["first_exception_step"] =
      picojson::value(static_cast<double>(first_exception_step_));
  object["steps_after_exception"] =
      picojson::value(static_cast<double>(post_exception_successes_));
  object["final_fingerprint"] = picojson::value(final_fingerprint_);
  return picojson::value(object);
}

}  // namespace bbox
