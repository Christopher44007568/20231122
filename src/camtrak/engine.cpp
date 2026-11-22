#include "camtrak/engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "camtrak/interpolate.hpp"
#include "camtrak/model.hpp"
#include "camtrak/sha256.hpp"

namespace camtrak {

std::string Engine::canonicalConfigHash(const CorrectionConfig& config) {
  return sha256Hex(toJson(config).dump());
}

namespace {

// Validation samples are original captured samples that fall inside the gap
// on the reference timeline. They let a reviewer quantify interpolation
// error whenever some real data was retained inside the missing interval.
Metrics computeMetrics(const std::vector<Sample>& reference_samples,
                       const std::vector<GapSegment>& corrected) {
  Metrics metrics;
  if (reference_samples.empty() || corrected.empty()) return metrics;

  double sum_sq_position = 0.0;
  double sum_sq_angle = 0.0;
  for (const GapSegment& segment : corrected) {
    auto nearest = std::min_element(
        reference_samples.begin(), reference_samples.end(),
        [&](const Sample& a, const Sample& b) {
          return std::abs(a.t_device - segment.t) <
                 std::abs(b.t_device - segment.t);
        });
    if (std::abs(nearest->t_device - segment.t) > 1e-9) continue;

    const double position_error =
        norm(nearest->pose.position - segment.pose.position);
    const double angle_error =
        angleBetween(nearest->pose.orientation, segment.pose.orientation);
    metrics.max_position_error =
        std::max(metrics.max_position_error, position_error);
    metrics.max_angle_error_rad =
        std::max(metrics.max_angle_error_rad, angle_error);
    sum_sq_position += position_error * position_error;
    sum_sq_angle += angle_error * angle_error;
    ++metrics.compared_samples;
  }
  if (metrics.compared_samples > 0) {
    const double count = static_cast<double>(metrics.compared_samples);
    metrics.rmse_position = std::sqrt(sum_sq_position / count);
    metrics.rmse_angle_rad = std::sqrt(sum_sq_angle / count);
  }
  return metrics;
}

}  // namespace

EngineReport Engine::runObject(const CorrectionConfig& config) {
  if (config.gap_t1 <= config.gap_t0) {
    throw std::runtime_error("gap_t1 must be greater than gap_t0");
  }
  const std::string config_hash = canonicalConfigHash(config);

  std::string run_id;
  bool resumed = false;
  std::string started_at;
  auto session = store_.loadSession();
  auto found = session.find(config.object_id);
  if (found != session.end() && found->second.status == "running" &&
      found->second.config_hash == config_hash) {
    run_id = found->second.run_id;
    resumed = true;
    started_at = found->second.started_at;
  } else {
    run_id = makeRunId(config.object_id);
    started_at = utcNowIso8601();
  }

  RunState state;
  state.run_id = run_id;
  state.object_id = config.object_id;
  state.status = "running";
  state.started_at = started_at;
  state.updated_at = utcNowIso8601();
  state.config_hash = config_hash;
  state.config = config;
  store_.saveSessionState(state);

  RunResult result;
  try {
    const Trajectory raw = store_.loadTrajectory(config.object_id);
    const Trajectory reference = store_.toReferenceTimeline(raw);
    result.corrected = fillGap(reference.samples, config);

    std::vector<Sample> inside_gap;
    for (const Sample& sample : reference.samples) {
      if (sample.t_device > config.gap_t0 &&
          sample.t_device < config.gap_t1) {
        inside_gap.push_back(sample);
      }
    }
    result.metrics = computeMetrics(inside_gap, result.corrected);
  } catch (const std::exception& error) {
    state.status = "failed";
    state.updated_at = utcNowIso8601();
    state.last_error = error.what();
    store_.saveSessionState(state);
    throw;
  }

  result.run_id = run_id;
  result.object_id = config.object_id;
  result.method = methodName(config.method);
  result.config = config;
  store_.saveRunResult(result);

  state.status = "completed";
  state.updated_at = utcNowIso8601();
  store_.saveSessionState(state);

  const RunLogEntry entry =
      store_.appendRunLog(result, "completed", config_hash);
  return EngineReport{result, entry, resumed};
}

std::vector<std::string> Engine::verifyRun(
    const std::string& run_id) const {
  std::vector<std::string> failures;
  const RunResult stored = store_.loadRunResult(run_id);

  const JsonValue manifest = store_.loadEvidenceManifest();
  const JsonValue& files = manifest.get("files");
  std::string snapshot_path;
  std::string expected_hash;
  for (std::size_t i = 0; i < files.size(); ++i) {
    if (files.at(i).get("object_id").asString() == stored.object_id) {
      snapshot_path = files.at(i).get("snapshot").asString();
      expected_hash = files.at(i).get("sha256").asString();
      break;
    }
  }
  if (snapshot_path.empty()) {
    failures.push_back("no evidence snapshot for object " +
                       stored.object_id);
    return failures;
  }
  const std::string snapshot_bytes =
      readFile(store_.root() + "/" + snapshot_path);
  if (sha256Hex(snapshot_bytes) != expected_hash) {
    failures.push_back("snapshot digest mismatch for " + stored.object_id);
  }

  JsonParseError parse_error;
  const JsonValue snapshot_json =
      parseJson(snapshot_bytes, &parse_error);
  if (snapshot_json.isNull()) {
    failures.push_back("snapshot is not valid JSON: " +
                       parse_error.message);
    return failures;
  }
  Trajectory trajectory = trajectoryFromJson(snapshot_json);
  sortAndValidate(trajectory);
  trajectory = store_.toReferenceTimeline(trajectory);

  RunResult recomputed;
  recomputed.run_id = stored.run_id;
  recomputed.object_id = stored.object_id;
  recomputed.method = stored.method;
  recomputed.config = stored.config;
  recomputed.corrected = fillGap(trajectory.samples, stored.config);

  std::vector<Sample> inside_gap;
  for (const Sample& sample : trajectory.samples) {
    if (sample.t_device > stored.config.gap_t0 &&
        sample.t_device < stored.config.gap_t1) {
      inside_gap.push_back(sample);
    }
  }
  recomputed.metrics =
      computeMetrics(inside_gap, recomputed.corrected);

  if (toJson(recomputed).dump() != toJson(stored).dump()) {
    failures.push_back(
        "recomputed run does not match the recorded run file: " + run_id);
  }

  const auto log_entries = store_.readRunLog();
  bool logged = false;
  for (const RunLogEntry& entry : log_entries) {
    if (entry.run_id == run_id &&
        entry.result_hash == sha256Hex(toJson(stored).dump())) {
      logged = true;
      break;
    }
  }
  if (!logged) {
    failures.push_back("run not found in hash-chained run log: " + run_id);
  }
  return failures;
}

Engine::Comparison Engine::compare(const std::string& run_id_a,
                                   const std::string& run_id_b) const {
  const RunResult a = store_.loadRunResult(run_id_a);
  const RunResult b = store_.loadRunResult(run_id_b);
  if (a.object_id != b.object_id) {
    throw std::runtime_error(
        "can only compare runs of the same object; got " + a.object_id +
        " and " + b.object_id);
  }
  Comparison comparison;
  comparison.run_a = run_id_a;
  comparison.run_b = run_id_b;
  const std::size_t count =
      std::min(a.corrected.size(), b.corrected.size());
  comparison.common_samples = count;
  double sum_sq = 0.0;
  for (std::size_t i = 0; i < count; ++i) {
    const double position_delta =
        norm(a.corrected[i].pose.position - b.corrected[i].pose.position);
    const double angle_delta = angleBetween(a.corrected[i].pose.orientation,
                                            b.corrected[i].pose.orientation);
    comparison.max_position_delta =
        std::max(comparison.max_position_delta, position_delta);
    comparison.max_angle_delta_rad =
        std::max(comparison.max_angle_delta_rad, angle_delta);
    sum_sq += position_delta * position_delta;
  }
  if (count > 0) {
    comparison.rmse_position_delta =
        std::sqrt(sum_sq / static_cast<double>(count));
  }
  return comparison;
}

}  // namespace camtrak
