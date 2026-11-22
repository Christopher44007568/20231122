#include "pipeline.hpp"

#include "compressor.hpp"
#include "glfw_runtime.hpp"
#include "json.hpp"
#include "model.hpp"
#include "report.hpp"
#include "sha256.hpp"
#include "vulkan_accelerator.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace kfc {

namespace {

struct ResourcePlan {
  double budgetMb = 32.0;
  double estimatedFullMb = 0.0;
  std::string mode = "full";
  double toleranceDeg = 1.25;
  double toleranceM = 0.012;
  std::uint32_t chunkIntervals = 8;
  bool allowVulkan = true;
  double estimatedPeakMb = 0.0;
  bool budgetFloorExceeded = false;
  std::vector<std::string> degradations;
  VulkanProbe probe;
};

struct PreparedInput {
  std::string sourceDirectory;
  std::string artifactDirectory;
  std::string scenarioHash;
  std::string deviceAHash;
  std::string deviceBHash;
  std::string manifestHash;
  Scenario scenario;
  DeviceCapture captureA;
  DeviceCapture captureB;
  ClockCalibration calibration;
  FusedTimeline timeline;
};

struct BackendChoice {
  CpuIntervalEvaluator cpu;
  std::unique_ptr<VulkanAccelerator> vulkan;
  IntervalEvaluator* evaluator = &cpu;
  std::string requested = "vulkan-compute";
  std::string selected = "cpu-reference";
  std::string detail;
  std::string error;
  bool fallback = false;
  VulkanProbe probe;
};

std::string fileHash(const std::string& path) {
  return Sha256::of(readTextFile(path));
}

void writeJson(const std::string& path, const Json& value) {
  writeTextFile(path, value.dump(2) + "\n");
}

std::string relativeName(const std::string& root,
                         const std::string& path) {
  std::error_code error;
  const auto relative = std::filesystem::relative(path, root, error);
  return error ? path : relative.generic_string();
}

ResourcePlan makeResourcePlan(double budgetMb, std::size_t frameCount,
                              bool disableVulkan,
                              const VulkanProbe& probe) {
  ResourcePlan plan;
  plan.budgetMb = budgetMb;
  plan.probe = probe;
  plan.estimatedFullMb =
      20.0 + static_cast<double>(frameCount) * 272.0 * 3.0 / 1000000.0;
  if (budgetMb >= plan.estimatedFullMb) {
    plan.mode = "full";
    plan.toleranceDeg = 1.25;
    plan.toleranceM = 0.012;
    plan.chunkIntervals = 8;
    plan.estimatedPeakMb = plan.estimatedFullMb;
    plan.allowVulkan = true;
  } else if (budgetMb >= 8.0) {
    plan.mode = "reduced-memory";
    plan.toleranceDeg = 2.0;
    plan.toleranceM = 0.020;
    plan.chunkIntervals = 6;
    plan.estimatedPeakMb = 6.0;
    plan.allowVulkan = false;
    plan.degradations.push_back(
        "budget below full working-set estimate: disabled Vulkan staging");
    plan.degradations.push_back(
        "increased angular/translation tolerances and shortened chunks");
  } else {
    plan.mode = "minimal-memory";
    plan.toleranceDeg = 3.5;
    plan.toleranceM = 0.035;
    plan.chunkIntervals = 4;
    plan.estimatedPeakMb = 2.2;
    plan.allowVulkan = false;
    plan.degradations.push_back(
        "critical budget: CPU streaming with four-interval chunks");
    plan.degradations.push_back(
        "quality tolerance widened; all gap and checkpoint evidence retained");
    if (budgetMb < plan.estimatedPeakMb) {
      plan.budgetFloorExceeded = true;
      plan.degradations.push_back(
          "budget below audited floor: continued in evidence-preserving "
          "streaming mode");
    }
  }
  if (disableVulkan && plan.allowVulkan) {
    plan.allowVulkan = false;
    plan.degradations.push_back("Vulkan compute disabled by operator flag");
  } else if (plan.allowVulkan && !probe.available) {
    plan.allowVulkan = false;
    plan.degradations.push_back(
        "Vulkan probe unavailable: " +
        (probe.error.empty() ? std::string("unknown error") : probe.error));
  }
  return plan;
}

Json resourcePlanToJson(const ResourcePlan& plan) {
  Json root = Json::object();
  root["budget_mb"] = plan.budgetMb;
  root["estimated_full_path_mb"] = plan.estimatedFullMb;
  root["selected_mode"] = plan.mode;
  root["estimated_peak_mb"] = plan.estimatedPeakMb;
  root["compression_tolerance_deg"] = plan.toleranceDeg;
  root["compression_tolerance_m"] = plan.toleranceM;
  root["chunk_intervals"] = static_cast<std::uint64_t>(plan.chunkIntervals);
  root["vulkan_allowed"] = plan.allowVulkan;
  root["budget_floor_exceeded"] = plan.budgetFloorExceeded;
  Json degradations = Json::array();
  for (const std::string& reason : plan.degradations) {
    degradations.push(reason);
  }
  root["degradations"] = degradations;
  root["policy"] =
      "degrade quality and acceleration before dropping audit evidence";
  return root;
}

CompressionConfig configFromPlan(const ResourcePlan& plan) {
  CompressionConfig config;
  config.toleranceDeg = plan.toleranceDeg;
  config.toleranceM = plan.toleranceM;
  config.chunkIntervals = plan.chunkIntervals;
  config.resourceMode = plan.mode;
  return config;
}

BackendChoice chooseBackend(const ResourcePlan& plan) {
  BackendChoice choice;
  choice.probe = plan.probe;
  if (plan.allowVulkan) {
    std::string error;
    choice.vulkan = VulkanAccelerator::create(&error);
    if (choice.vulkan != nullptr) {
      choice.evaluator = choice.vulkan.get();
      choice.selected = choice.vulkan->name();
      choice.detail = choice.vulkan->detail();
      choice.probe = choice.vulkan->probe();
      return choice;
    }
    choice.error = error;
    choice.fallback = true;
  }
  choice.selected = choice.cpu.name();
  choice.detail = choice.cpu.detail();
  return choice;
}

Json backendToJson(const BackendChoice& backend) {
  Json root = Json::object();
  root["requested"] = backend.requested;
  root["selected"] = backend.selected;
  root["detail"] = backend.detail;
  root["fallback"] = backend.fallback;
  if (!backend.error.empty()) root["error"] = backend.error;
  Json probe = Json::object();
  probe["available"] = backend.probe.available;
  probe["device_name"] = backend.probe.deviceName;
  probe["api_version"] = static_cast<std::uint64_t>(backend.probe.apiVersion);
  probe["driver_version"] =
      static_cast<std::uint64_t>(backend.probe.driverVersion);
  probe["local_heap_bytes"] = backend.probe.localHeapBytes;
  probe["host_visible_heap_bytes"] = backend.probe.hostVisibleHeapBytes;
  if (!backend.probe.error.empty()) probe["error"] = backend.probe.error;
  root["vulkan_probe"] = probe;
  root["evidence_policy"] =
      "backend choice is recorded before the first compression work unit";
  return root;
}

void verifyInputManifest(const std::string& inputDir) {
  const std::string manifestPath = pathJoin(inputDir, "input-manifest.json");
  if (!fileExists(manifestPath)) {
    throw std::runtime_error("missing input-manifest.json in " + inputDir);
  }
  std::string error;
  const Json manifest = Json::parse(readTextFile(manifestPath), &error);
  if (!error.empty()) throw std::runtime_error("input manifest parse: " + error);
  if (manifest.at("schema").asString() != "kfc.input-manifest/v1") {
    throw std::runtime_error("unsupported input manifest schema");
  }
  for (const Json& file : manifest.at("files").asArray()) {
    const std::string name = file.at("name").asString();
    const std::string path = pathJoin(inputDir, name);
    if (!fileExists(path)) {
      throw std::runtime_error("input manifest references missing file: " + name);
    }
    if (fileHash(path) != file.at("sha256").asString()) {
      throw std::runtime_error("input hash mismatch: " + name);
    }
  }
}

PreparedInput prepareInput(const std::string& inputDir,
                           const std::string& artifactDir) {
  ensureDirectory(inputDir);
  if (!fileExists(pathJoin(inputDir, "scenario.json"))) {
    generateScenario(inputDir);
  } else {
    verifyInputManifest(inputDir);
  }
  PreparedInput prepared;
  prepared.sourceDirectory =
      std::filesystem::absolute(inputDir).lexically_normal().string();
  prepared.artifactDirectory = artifactDir;
  const std::string scenarioPath = pathJoin(inputDir, "scenario.json");
  prepared.scenario = loadScenario(scenarioPath);
  const std::string aPath =
      pathJoin(inputDir, prepared.scenario.deviceAFile);
  const std::string bPath =
      pathJoin(inputDir, prepared.scenario.deviceBFile);
  prepared.captureA = loadCapture(aPath);
  prepared.captureB = loadCapture(bPath);
  if (prepared.captureA.deviceId != "capture-a" ||
      prepared.captureB.deviceId != "capture-b") {
    throw std::runtime_error("capture device ids do not match scenario roles");
  }
  if (prepared.captureA.bones != prepared.captureB.bones ||
      prepared.captureA.bones != prepared.scenario.bones) {
    throw std::runtime_error("device bone order mismatch");
  }
  prepared.scenarioHash = fileHash(scenarioPath);
  prepared.deviceAHash = fileHash(aPath);
  prepared.deviceBHash = fileHash(bPath);
  prepared.manifestHash =
      fileHash(pathJoin(inputDir, "input-manifest.json"));
  prepared.calibration =
      calibrateClocks(prepared.captureA, prepared.captureB);
  prepared.timeline =
      fuseCaptures(prepared.captureA, prepared.captureB, prepared.calibration);
  return prepared;
}

Json inputSummary(const PreparedInput& prepared) {
  Json root = Json::object();
  root["scenario_id"] = prepared.scenario.scenarioId;
  root["description"] = prepared.scenario.description;
  root["source_directory"] = prepared.sourceDirectory;
  root["artifact_directory"] = prepared.artifactDirectory;
  Json bones = Json::array();
  for (const std::string& bone : prepared.scenario.bones) bones.push(bone);
  root["bone_order"] = bones;
  root["frame_interval_us"] = prepared.scenario.frameIntervalUs;
  root["duration_us"] = prepared.scenario.durationUs;
  Json hashes = Json::object();
  hashes["scenario"] = prepared.scenarioHash;
  hashes["device_a_capture"] = prepared.deviceAHash;
  hashes["device_b_capture"] = prepared.deviceBHash;
  hashes["input_manifest"] = prepared.manifestHash;
  root["hashes"] = hashes;
  Json devices = Json::array();
  Json a = Json::object();
  a["device_id"] = prepared.captureA.deviceId;
  a["file"] = relativeName(prepared.artifactDirectory,
                           pathJoin(prepared.artifactDirectory,
                                    prepared.scenario.deviceAFile));
  a["sample_count"] =
      static_cast<std::uint64_t>(prepared.captureA.samples.size());
  a["sync_anchor_count"] =
      static_cast<std::uint64_t>(prepared.captureA.anchors.size());
  a["nominal_offset_us"] = prepared.captureA.clockOffsetUs;
  a["nominal_drift_ppm"] = prepared.captureA.clockDriftPpm;
  devices.push(a);
  Json b = Json::object();
  b["device_id"] = prepared.captureB.deviceId;
  b["file"] = relativeName(prepared.artifactDirectory,
                           pathJoin(prepared.artifactDirectory,
                                    prepared.scenario.deviceBFile));
  b["sample_count"] =
      static_cast<std::uint64_t>(prepared.captureB.samples.size());
  b["sync_anchor_count"] =
      static_cast<std::uint64_t>(prepared.captureB.anchors.size());
  b["nominal_offset_us"] = prepared.captureB.clockOffsetUs;
  b["nominal_drift_ppm"] = prepared.captureB.clockDriftPpm;
  devices.push(b);
  root["devices"] = devices;
  return root;
}

Json missingSegmentSummary(const PreparedInput& prepared) {
  Json root = Json::object();
  root["expected_start_us"] = prepared.scenario.intendedGapStartUs;
  root["expected_end_us"] = prepared.scenario.intendedGapEndUs;
  root["expected_duration_us"] =
      prepared.scenario.intendedGapEndUs - prepared.scenario.intendedGapStartUs;
  root["detection_basis"] =
      "fused timeline delta greater than 3.5 frame intervals";
  root["observed_gaps"] = gapsToJson(prepared.timeline);
  bool confirmed = !prepared.timeline.gaps.empty();
  if (confirmed) {
    const MissingSegment& gap = prepared.timeline.gaps.front();
    confirmed =
        std::fabs(gap.observedFromUs - prepared.scenario.intendedGapStartUs) <=
            20000.0 &&
        std::fabs(gap.observedToUs - prepared.scenario.intendedGapEndUs) <=
            20000.0;
  }
  root["status"] = confirmed ? "CONFIRMED_SINGLE_UNION_GAP"
                             : "UNEXPECTED_GAP_PATTERN";
  root["compression_boundary_policy"] =
      "split at gap; first/last frames of each continuous segment are retained";
  return root;
}

Json timelineSummary(const PreparedInput& prepared) {
  Json root = Json::object();
  root["device_a_sample_count"] =
      static_cast<std::uint64_t>(prepared.captureA.samples.size());
  root["device_b_sample_count"] =
      static_cast<std::uint64_t>(prepared.captureB.samples.size());
  root["fused_frame_count"] =
      static_cast<std::uint64_t>(prepared.timeline.frames.size());
  root["segment_count"] =
      static_cast<std::uint64_t>(prepared.timeline.segments.size());
  root["gap_count"] =
      static_cast<std::uint64_t>(prepared.timeline.gaps.size());
  Json coverage = Json::object();
  coverage["both_devices"] =
      static_cast<std::uint64_t>(prepared.timeline.fromBoth);
  coverage["device_a_only"] =
      static_cast<std::uint64_t>(prepared.timeline.fromDeviceA);
  coverage["device_b_only"] =
      static_cast<std::uint64_t>(prepared.timeline.fromDeviceB);
  root["source_coverage"] = coverage;
  Json agreement = Json::object();
  agreement["max_angular_delta_deg"] =
      prepared.timeline.agreement.maxAngularDeltaDeg;
  agreement["max_translation_delta_m"] =
      prepared.timeline.agreement.maxTranslationDeltaM;
  agreement["overlapping_clusters"] = static_cast<std::uint64_t>(
      prepared.timeline.agreement.overlappingClusters);
  root["cross_device_agreement"] = agreement;
  Json segments = Json::array();
  for (const auto& segment : prepared.timeline.segments) {
    Json entry = Json::object();
    entry["first_frame"] = static_cast<std::uint64_t>(segment.first);
    entry["past_last_frame"] = static_cast<std::uint64_t>(segment.second);
    entry["frame_count"] =
        static_cast<std::uint64_t>(segment.second - segment.first);
    segments.push(entry);
  }
  root["segments"] = segments;
  root["gaps"] = gapsToJson(prepared.timeline);
  return root;
}

std::string selectedIndicesHash(const Json& array) {
  std::string text;
  for (const Json& index : array.asArray()) {
    text += std::to_string(index.asInt());
    text.push_back(',');
  }
  return Sha256::of(text);
}

Json compareResults(const Json& left, const Json& right,
                    const std::string& leftLabel,
                    const std::string& rightLabel) {
  const std::string leftHash = Sha256::of(left.dump(2));
  const std::string rightHash = Sha256::of(right.dump(2));
  const bool keyframesEqual =
      left.at("keyframes").dump(2) == right.at("keyframes").dump(2);
  const bool countsEqual =
      left.at("source_frame_count").asInt() ==
          right.at("source_frame_count").asInt() &&
      left.at("gap_count").asInt() == right.at("gap_count").asInt();
  const bool workEqual =
      left.at("state").at("work_units").asInt() ==
          right.at("state").at("work_units").asInt() &&
      left.at("state").at("chunks_completed").asInt() ==
          right.at("state").at("chunks_completed").asInt();
  const double leftAngle =
      left.at("metrics").at("max_angular_error_deg").asDouble();
  const double rightAngle =
      right.at("metrics").at("max_angular_error_deg").asDouble();
  const double leftTranslation =
      left.at("metrics").at("max_translation_error_m").asDouble();
  const double rightTranslation =
      right.at("metrics").at("max_translation_error_m").asDouble();
  const bool metricsEqual =
      std::fabs(leftAngle - rightAngle) <= 1e-12 &&
      std::fabs(leftTranslation - rightTranslation) <= 1e-12;
  const bool complete = left.at("complete").asBool() &&
                        right.at("complete").asBool();
  const bool match = keyframesEqual && countsEqual && workEqual &&
                     metricsEqual && complete && leftHash == rightHash;

  Json root = Json::object();
  root["result_a_label"] = leftLabel;
  root["result_b_label"] = rightLabel;
  root["verdict"] = match ? "MATCH" : "MISMATCH";
  Json hashes = Json::object();
  hashes["result_a"] = leftHash;
  hashes["result_b"] = rightHash;
  root["hashes"] = hashes;
  Json checks = Json::object();
  checks["both_complete"] = complete;
  checks["result_hashes_equal"] = leftHash == rightHash;
  checks["keyframes_equal"] = keyframesEqual;
  checks["source_and_gap_counts_equal"] = countsEqual;
  checks["work_counters_equal"] = workEqual;
  checks["metrics_equal"] = metricsEqual;
  root["checks"] = checks;
  Json metrics = Json::object();
  metrics["max_angular_error_delta_deg"] =
      std::fabs(leftAngle - rightAngle);
  metrics["max_translation_error_delta_m"] =
      std::fabs(leftTranslation - rightTranslation);
  metrics["keyframe_count_a"] = left.at("keyframe_count").asInt();
  metrics["keyframe_count_b"] = right.at("keyframe_count").asInt();
  root["differences"] = metrics;
  return root;
}

Json compareCheckpointPrefix(const Json& checkpointState,
                             const Json& baselineResult) {
  const Json& checkpointIndices = checkpointState.at("selected_indices");
  const Json& baselineKeyframes = baselineResult.at("keyframes");
  bool prefix = checkpointIndices.size() <= baselineKeyframes.size();
  std::size_t mismatchAt = 0;
  if (prefix) {
    for (std::size_t i = 0; i < checkpointIndices.size(); ++i) {
      if (checkpointIndices.asArray()[i].asInt() !=
          baselineKeyframes.asArray()[i].at("source_index").asInt()) {
        prefix = false;
        mismatchAt = i;
        break;
      }
    }
  } else {
    mismatchAt = baselineKeyframes.size();
  }
  Json root = Json::object();
  root["checkpoint_keyframe_count"] =
      static_cast<std::uint64_t>(checkpointIndices.size());
  root["baseline_keyframe_count"] =
      static_cast<std::uint64_t>(baselineKeyframes.size());
  root["prefix_matches"] = prefix;
  if (!prefix) root["first_mismatch_index"] = static_cast<std::uint64_t>(mismatchAt);
  root["meaning"] =
      "work completed before the pause is a byte-for-byte prefix of the "
      "uninterrupted result";
  return root;
}

Json compareDegraded(const Json& fullResult, const Json& degradedResult,
                     const std::string& fullMode,
                     const std::string& degradedMode,
                     double toleranceDeg, double toleranceM,
                     bool toleranceAvailable) {
  const bool complete = degradedResult.at("complete").asBool();
  const bool gapCountEqual =
      fullResult.at("gap_count").asInt() ==
      degradedResult.at("gap_count").asInt();
  const std::int64_t fullKeys = fullResult.at("keyframe_count").asInt();
  const std::int64_t degradedKeys =
      degradedResult.at("keyframe_count").asInt();
  const double fullAngle =
      fullResult.at("metrics").at("max_angular_error_deg").asDouble();
  const double degradedAngle =
      degradedResult.at("metrics").at("max_angular_error_deg").asDouble();
  const double fullTranslation =
      fullResult.at("metrics").at("max_translation_error_m").asDouble();
  const double degradedTranslation =
      degradedResult.at("metrics").at("max_translation_error_m").asDouble();
  const bool errorsWithinTolerance =
      !toleranceAvailable ||
      (degradedAngle <= toleranceDeg + 1e-6 &&
       degradedTranslation <= toleranceM + 1e-6);
  const bool acceptable =
      complete && gapCountEqual && degradedKeys > 0 && errorsWithinTolerance;
  const bool identical = acceptable && fullKeys == degradedKeys &&
                         std::fabs(degradedAngle - fullAngle) <= 1e-12 &&
                         std::fabs(degradedTranslation - fullTranslation) <=
                             1e-12;
  Json root = Json::object();
  root["full_mode"] = fullMode;
  root["degraded_mode"] = degradedMode;
  root["verdict"] = identical
                        ? "IDENTICAL"
                        : (acceptable ? "EXPECTED_DEGRADATION" : "UNEXPECTED");
  root["degraded_complete"] = complete;
  root["gap_count_preserved"] = gapCountEqual;
  root["keyframe_count_full"] = fullKeys;
  root["keyframe_count_degraded"] = degradedKeys;
  root["max_angular_error_delta_deg"] = degradedAngle - fullAngle;
  root["max_translation_error_delta_m"] =
      degradedTranslation - fullTranslation;
  root["quality_tolerance_deg"] = toleranceDeg;
  root["quality_tolerance_m"] = toleranceM;
  root["errors_within_degraded_tolerance"] = errorsWithinTolerance;
  root["evidence_retained"] =
      "input hashes, gap boundary, checkpoint state and review checks remain";
  return root;
}

std::vector<std::string> fieldOrder(const Json& object) {
  std::vector<std::string> order;
  for (const auto& item : object.asObject()) order.push_back(item.first);
  return order;
}

Json stringArray(const std::vector<std::string>& values) {
  Json result = Json::array();
  for (const std::string& value : values) result.push(value);
  return result;
}

void writeRecordWithSelfCheck(const std::string& path, Json record) {
  record["field_order_preserved"] = false;
  std::vector<std::string> order = fieldOrder(record);
  order.push_back("field_order");
  record["field_order"] = stringArray(order);
  const std::string first = record.dump(2) + "\n";
  std::string error;
  const Json parsed = Json::parse(first, &error);
  if (!error.empty()) throw std::runtime_error("record self parse: " + error);
  const std::string second = parsed.dump(2) + "\n";
  if (first != second || fieldOrder(parsed) != fieldOrder(record)) {
    throw std::runtime_error("ordered JSON round-trip self-check failed");
  }
  if (parsed.contains("field_order_preserved")) {
    Json verified = parsed;
    verified["field_order_preserved"] = true;
    const std::string finalText = verified.dump(2) + "\n";
    const Json finalParsed = Json::parse(finalText, &error);
    if (!error.empty() ||
        fieldOrder(finalParsed) != fieldOrder(verified)) {
      throw std::runtime_error("final ordered JSON self-check failed");
    }
    writeTextFile(path, finalText);
  } else {
    throw std::runtime_error("record lost field_order_preserved during parse");
  }
}

Json checkpointEvidence(const CompressionState& state,
                        const std::string& path,
                        const std::string& sha256) {
  Json root = Json::object();
  root["status"] = "SAVED";
  root["path"] = path;
  root["sha256"] = sha256;
  root["paused_after_chunks"] = state.chunksCompleted;
  root["work_units_at_pause"] = state.workUnits;
  root["selected_prefix_count"] = static_cast<std::uint64_t>(
      state.selectedIndices.size());
  root["selected_prefix_hash"] =
      selectedIndicesHash(compressionStateToJson(state).at("selected_indices"));
  root["state"] = compressionStateToJson(state);
  root["resume_guarantee"] =
      "state contains next segment/chunk plus the exact selected prefix";
  return root;
}

Json makeCheckpoint(const PreparedInput& prepared, const ResourcePlan& plan,
                    const CompressionConfig& config,
                    const BackendChoice& backend,
                    const CompressionState& state,
                    const std::string& recordId) {
  Json root = Json::object();
  root["schema"] = "kfc.checkpoint/v1";
  root["record_id"] = recordId;
  root["created_at_utc"] = isoUtcNow();
  root["status"] = "PAUSED";
  root["input"] = inputSummary(prepared);
  root["clock_calibration"] = calibrationToJson(prepared.calibration);
  root["missing_segment"] = missingSegmentSummary(prepared);
  root["timeline"] = timelineSummary(prepared);
  root["resource_plan"] = resourcePlanToJson(plan);
  root["compression_config"] = compressionConfigToJson(config);
  root["backend"] = backendToJson(backend);
  root["state_schema_version"] = static_cast<std::uint64_t>(state.schemaVersion);
  root["state"] = compressionStateToJson(state);
  Json resume = Json::object();
  resume["expected_next_segment"] =
      static_cast<std::uint64_t>(state.nextSegment);
  resume["expected_next_chunk"] =
      static_cast<std::uint64_t>(state.nextChunk);
  resume["input_hashes_must_match"] = true;
  resume["backend_must_match"] = true;
  resume["command_template"] =
      "skeleton-kf resume --checkpoint checkpoint.json --out <artifact-dir>";
  root["resume_contract"] = resume;
  return root;
}

Json resultSummary(const CompressionResult& result, const std::string& path,
                   const std::string& sha256,
                   const std::vector<std::string>& boneNames) {
  Json root = Json::object();
  root["path"] = path;
  root["sha256"] = sha256;
  root["complete"] = result.complete;
  root["source_frame_count"] =
      static_cast<std::uint64_t>(result.sourceFrameCount);
  root["keyframe_count"] =
      static_cast<std::uint64_t>(result.keyframeCount);
  root["segment_count"] =
      static_cast<std::uint64_t>(result.segmentCount);
  root["gap_count"] = static_cast<std::uint64_t>(result.gapCount);
  Json metrics = Json::object();
  metrics["max_angular_error_deg"] = result.maxAngularErrorDeg;
  metrics["max_translation_error_m"] = result.maxTranslationErrorM;
  metrics["mean_angular_error_deg"] = result.meanAngularErrorDeg;
  metrics["mean_translation_error_m"] = result.meanTranslationErrorM;
  root["metrics"] = metrics;
  Json work = Json::object();
  work["chunks_completed"] = result.state.chunksCompleted;
  work["interval_evaluations"] = result.state.workUnits;
  work["forced_steps"] = result.state.forcedSteps;
  root["work"] = work;
  Json indices = Json::array();
  for (const KeyframeRecord& keyframe : result.keyframes) {
    indices.push(static_cast<std::uint64_t>(keyframe.sourceIndex));
  }
  root["selected_indices_hash"] = selectedIndicesHash(indices);
  Json bones = Json::array();
  for (const std::string& bone : boneNames) bones.push(bone);
  root["bone_order"] = bones;
  return root;
}

Json makeRecord(const PreparedInput& prepared, const ResourcePlan& plan,
                const BackendChoice& backend, const GlfwRuntime* glfw,
                const std::string& status, const CompressionConfig& config,
                const CompressionState& state, const CompressionResult* result,
                const std::string& resultPath, const std::string& resultHash,
                const Json& checkpoint, const Json& baselineComparison,
                const Json& degradedComparison, bool resumeLoaded,
                const std::string& recordRoot, bool openWindow) {
  Json record = Json::object();
  record["schema"] = "kfc.review-record/v1";
  record["record_id"] =
      prepared.scenario.scenarioId + "-" +
      prepared.manifestHash.substr(0, 12);
  record["created_at_utc"] = isoUtcNow();
  record["status"] = status;
  record["task"] = Json::object();
  record["task"]["title"] =
      "Pausable skeletal-animation keyframe compression across two skewed "
      "device clocks";
  record["task"]["missing_segment_policy"] =
      "treat the union coverage hole as a hard compression boundary";
  record["task"]["resume_goal"] =
      "restore exact pipeline state and match the uninterrupted baseline";

  Json stack = Json::object();
  stack["language"] = "C++17";
  stack["compute"] = "Vulkan compute with CPU reference fallback";
  stack["windowing"] = "GLFW";
  if (glfw != nullptr) {
    Json window = Json::object();
    window["library"] = "GLFW";
    window["version"] = glfw->version();
    window["platform"] = glfw->platform();
    window["headless"] = !openWindow;
    window["window_created"] = glfw->hasWindow();
    stack["glfw_runtime"] = window;
  }
  record["technical_stack"] = stack;
  Json inputJson = inputSummary(prepared);
  inputJson["source_directory"] =
      relativeName(recordRoot, prepared.sourceDirectory);
  inputJson["artifact_directory"] =
      relativeName(recordRoot, prepared.artifactDirectory);
  record["input"] = inputJson;
  record["clock_calibration"] = calibrationToJson(prepared.calibration);
  record["missing_segment"] = missingSegmentSummary(prepared);
  record["timeline"] = timelineSummary(prepared);
  record["resource_plan"] = resourcePlanToJson(plan);
  record["backend"] = backendToJson(backend);

  Json pipeline = Json::object();
  pipeline["compression_config"] = compressionConfigToJson(config);
  pipeline["state"] = compressionStateToJson(state);
  Json checkpointPolicy = Json::object();
  checkpointPolicy["checkpoint_unit"] = "completed chunk";
  checkpointPolicy["state_fields"] = stringArray(
      {"next_segment", "next_chunk", "chunks_completed", "work_units",
       "selected_indices"});
  checkpointPolicy["resume_validation"] =
      "input hashes, backend identity and state schema are checked";
  pipeline["checkpoint_policy"] = checkpointPolicy;
  record["pipeline"] = pipeline;
  record["checkpoint_evidence"] = checkpoint;
  record["resume_loaded"] = resumeLoaded;

  if (result != nullptr && !resultPath.empty()) {
    record["result"] = resultSummary(*result, resultPath, resultHash,
                                     prepared.scenario.bones);
  } else {
    Json pending = Json::object();
    pending["status"] = "PENDING_RESUME";
    pending["message"] = "no final keyframe file exists yet";
    record["result"] = pending;
  }
  record["baseline_comparison"] = baselineComparison;
  record["degraded_comparison"] = degradedComparison;

  Json evidence = Json::array();
  auto pushEvidence = [&](const std::string& id, const std::string& kind,
                          const std::string& path,
                          const std::string& hash,
                          const std::string& claim) {
    Json entry = Json::object();
    entry["id"] = id;
    entry["kind"] = kind;
    entry["path"] = path;
    entry["sha256"] = hash;
    entry["claim"] = claim;
    evidence.push(entry);
  };
  pushEvidence("input-manifest", "file",
               relativeName(recordRoot,
                            pathJoin(prepared.sourceDirectory,
                                     "input-manifest.json")),
               prepared.manifestHash,
               "both captures and the scenario are byte-identified");
  const std::string calibrationHash =
      Sha256::of(calibrationToJson(prepared.calibration).dump(2));
  pushEvidence("clock-calibration", "derived", "", calibrationHash,
               "affine clock mapping and residual bounds used for fusion");
  const std::string gapHash =
      Sha256::of(missingSegmentSummary(prepared).dump(2));
  pushEvidence("missing-segment", "derived", "", gapHash,
               "hard gap boundaries are recorded before compression");
  if (checkpoint.contains("path") && checkpoint.at("path").isString()) {
    pushEvidence("checkpoint", "file", checkpoint.at("path").asString(),
                 checkpoint.at("sha256").asString(),
                 "state exactly at the pause boundary");
  }
  if (!resultPath.empty()) {
    pushEvidence("final-keyframes", "file", resultPath, resultHash,
                 "final compressed keyframe sequence");
  }
  if (baselineComparison.contains("path")) {
    pushEvidence("baseline-keyframes", "file",
                 baselineComparison.at("path").asString(),
                 baselineComparison.at("sha256").asString(),
                 "uninterrupted baseline used for resume equality");
  }
  if (degradedComparison.contains("path")) {
    pushEvidence("degraded-keyframes", "file",
                 degradedComparison.at("path").asString(),
                 degradedComparison.at("sha256").asString(),
                 "low-resource run retained as comparison evidence");
  }
  record["evidence"] = evidence;

  Json reproduce = Json::object();
  Json commands = Json::array();
  commands.push("skeleton-kf verify --record review-record.json");
  commands.push("skeleton-kf reload-check --file review-record.json");
  commands.push("skeleton-kf replay --record review-record.json");
  commands.push("skeleton-kf inspect --file review-record.json");
  reproduce["commands"] = commands;
  reproduce["build"] =
      "cmake -S . -B build -G Ninja && cmake --build build";
  reproduce["note"] =
      "commands are run from the artifact directory; all referenced paths are "
      "relative";
  record["reproduce"] = reproduce;

  Json contract = Json::object();
  contract["hash_algorithm"] = "SHA-256 over exact UTF-8 file bytes";
  contract["ordered_fields"] =
      "JSON objects preserve insertion order on parse and dump";
  contract["independent_review"] =
      "verify checks hashes, schemas, field order, selected indices and "
      "baseline equality";
  Json checks = Json::array();
  for (const char* check :
       {"input_hashes", "checkpoint_hash", "result_hash", "field_order",
        "baseline_prefix", "baseline_result"}) {
    checks.push(check);
  }
  contract["checks"] = checks;
  record["review_contract"] = contract;
  return record;
}

Json readJsonFile(const std::string& path) {
  std::string error;
  Json value = Json::parse(readTextFile(path), &error);
  if (!error.empty()) {
    throw std::runtime_error("JSON parse failed for " + path + ": " + error);
  }
  return value;
}

Json defaultComparison(const std::string& reason) {
  Json root = Json::object();
  root["status"] = "NOT_PROVIDED";
  root["reason"] = reason;
  return root;
}

Json attachBaseline(const Json& current, const std::string& recordRoot,
                    const std::string& baselinePath,
                    const Json& checkpointState) {
  if (baselinePath.empty()) {
    return defaultComparison("baseline result was not requested");
  }
  if (!fileExists(baselinePath)) {
    throw std::runtime_error("baseline result not found: " + baselinePath);
  }
  const Json baseline = readJsonFile(baselinePath);
  const Json compared =
      compareResults(current, baseline, "resumed/final", "uninterrupted");
  Json root = Json::object();
  root["path"] = relativeName(recordRoot, baselinePath);
  root["sha256"] = fileHash(baselinePath);
  for (const auto& item : compared.asObject()) root[item.first] = item.second;
  if (!checkpointState.isNull() &&
      checkpointState.contains("selected_indices")) {
    root["checkpoint_prefix"] =
        compareCheckpointPrefix(checkpointState, baseline);
  }
  return root;
}

Json attachDegraded(const Json& current, const std::string& recordRoot,
                    const std::string& degradedPath,
                    const std::string& degradedMode) {
  if (degradedPath.empty()) {
    return defaultComparison("degraded result was not requested");
  }
  if (!fileExists(degradedPath)) {
    throw std::runtime_error("degraded result not found: " + degradedPath);
  }
  const Json degraded = readJsonFile(degradedPath);
  std::string mode = degradedMode;
  double toleranceDeg = 0.0;
  double toleranceM = 0.0;
  bool toleranceAvailable = false;
  const std::filesystem::path adjacentRecord =
      std::filesystem::path(degradedPath).parent_path() / "review-record.json";
  if (fileExists(adjacentRecord.string())) {
    const Json degradedRecord = readJsonFile(adjacentRecord.string());
    if (degradedRecord.contains("resource_plan")) {
      const Json& plan = degradedRecord.at("resource_plan");
      mode = plan.at("selected_mode").asString();
      toleranceDeg = plan.at("compression_tolerance_deg").asDouble();
      toleranceM = plan.at("compression_tolerance_m").asDouble();
      toleranceAvailable = true;
    }
  }
  const Json compared =
      compareDegraded(current, degraded, "full", mode, toleranceDeg,
                      toleranceM, toleranceAvailable);
  Json root = Json::object();
  root["path"] = relativeName(recordRoot, degradedPath);
  root["sha256"] = fileHash(degradedPath);
  for (const auto& item : compared.asObject()) root[item.first] = item.second;
  return root;
}

std::pair<std::string, std::string> writeCompressedResult(
    const std::string& outputDir, const CompressionResult& result,
    const std::vector<std::string>& bones,
    const std::string& fileName) {
  const std::string path = pathJoin(outputDir, fileName);
  const Json json = compressionResultToJson(result, bones);
  writeJson(path, json);
  return {relativeName(outputDir, path), fileHash(path)};
}

void writeManifest(const std::string& outputDir) {
  Json root = Json::object();
  root["schema"] = "kfc.artifact-manifest/v1";
  root["hash_algorithm"] = "SHA-256";
  Json files = Json::array();
  std::vector<std::filesystem::path> paths;
  const std::filesystem::path base(outputDir);
  for (const auto& entry : std::filesystem::recursive_directory_iterator(base)) {
    if (!entry.is_regular_file()) continue;
    if (entry.path().filename() == "manifest.json") continue;
    if (entry.path().extension() == ".tmp") continue;
    paths.push_back(entry.path());
  }
  std::sort(paths.begin(), paths.end());
  for (const auto& path : paths) {
    Json entry = Json::object();
    entry["path"] =
        std::filesystem::relative(path, base).generic_string();
    entry["sha256"] = fileHash(path.string());
    entry["bytes"] = static_cast<std::uint64_t>(
        std::filesystem::file_size(path));
    const std::string name = path.filename().string();
    if (name == "review-record.json") {
      entry["role"] = "human_and_machine_review_record";
    } else if (name == "final-compressed.json") {
      entry["role"] = "final_keyframes";
    } else if (name == "checkpoint.json") {
      entry["role"] = "intermediate_pause_state";
    } else if (name == "review-report.md") {
      entry["role"] = "human_review_narrative";
    } else if (name == "baseline-compressed.json") {
      entry["role"] = "uninterrupted_baseline";
    } else if (name == "degraded-compressed.json") {
      entry["role"] = "degraded_resource_result";
    } else if (name == "verification.json") {
      entry["role"] = "machine_verification_result";
    } else if (name == "reload-check.json") {
      entry["role"] = "ordered_reload_proof";
    } else {
      entry["role"] = "supporting_artifact";
    }
    files.push(entry);
  }
  root["files"] = files;
  writeJson(pathJoin(outputDir, "manifest.json"), root);
}

void writeFinalArtifacts(const std::string& outputDir, Json record) {
  writeRecordWithSelfCheck(pathJoin(outputDir, "review-record.json"), record);
  const Json verifiedRecord =
      readJsonFile(pathJoin(outputDir, "review-record.json"));
  writeTextFile(pathJoin(outputDir, "review-report.md"),
                makeReviewMarkdown(verifiedRecord));
  writeManifest(outputDir);
}

std::string checkpointRelativePath(const std::string& outputDir,
                                   const std::string& checkpointPath) {
  return relativeName(outputDir, checkpointPath);
}

Json makeCheckpointEvidenceForFile(const CompressionState& state,
                                   const std::string& outputDir,
                                   const std::string& checkpointPath) {
  return checkpointEvidence(state,
                            checkpointRelativePath(outputDir, checkpointPath),
                            fileHash(checkpointPath));
}

Json replayRecordInternal(const std::string& recordPath, bool* matches,
                          std::string* replayHash);

std::string normalizedPath(const std::string& path) {
  return std::filesystem::absolute(path).lexically_normal().string();
}

void stageInputFiles(const std::string& source, const std::string& target) {
  ensureDirectory(target);
  if (!source.empty() && normalizedPath(source) != normalizedPath(target)) {
    if (!fileExists(pathJoin(source, "scenario.json"))) {
      generateScenario(source);
    }
    const Scenario scenario = loadScenario(pathJoin(source, "scenario.json"));
    const std::vector<std::string> files = {
        "scenario.json", "input-manifest.json", scenario.deviceAFile,
        scenario.deviceBFile};
    for (const std::string& name : files) {
      std::filesystem::copy_file(
          pathJoin(source, name), pathJoin(target, name),
          std::filesystem::copy_options::overwrite_existing);
    }
    return;
  }
  if (!fileExists(pathJoin(target, "scenario.json"))) {
    generateScenario(target);
  }
}

Json noCheckpointEvidence() {
  Json root = Json::object();
  root["status"] = "NOT_REQUESTED";
  root["reason"] = "run completed without a pause";
  return root;
}

Json checkpointObjectFromFile(const std::string& path,
                              const std::string& outputDir) {
  const Json checkpoint = readJsonFile(path);
  Json evidence =
      checkpointEvidence(compressionStateFromJson(checkpoint.at("state")),
                         checkpointRelativePath(outputDir, path),
                         fileHash(path));
  return evidence;
}

CompressionConfig configFromJson(const Json& value) {
  CompressionConfig config;
  config.resourceMode = value.at("resource_mode").asString();
  config.toleranceDeg = value.at("angular_tolerance_deg").asDouble();
  config.toleranceM = value.at("translation_tolerance_m").asDouble();
  config.chunkIntervals = static_cast<std::uint32_t>(
      value.at("chunk_intervals").asInt());
  return config;
}

ResourcePlan planFromJson(const Json& value, const VulkanProbe& probe) {
  ResourcePlan plan;
  plan.budgetMb = value.at("budget_mb").asDouble();
  plan.estimatedFullMb = value.at("estimated_full_path_mb").asDouble();
  plan.mode = value.at("selected_mode").asString();
  plan.estimatedPeakMb = value.at("estimated_peak_mb").asDouble();
  plan.toleranceDeg = value.at("compression_tolerance_deg").asDouble();
  plan.toleranceM = value.at("compression_tolerance_m").asDouble();
  plan.chunkIntervals = static_cast<std::uint32_t>(
      value.at("chunk_intervals").asInt());
  plan.allowVulkan = value.at("vulkan_allowed").asBool();
  plan.budgetFloorExceeded =
      value.contains("budget_floor_exceeded")
          ? value.at("budget_floor_exceeded").asBool()
          : false;
  plan.probe = probe;
  if (value.contains("degradations")) {
    for (const Json& reason : value.at("degradations").asArray()) {
      plan.degradations.push_back(reason.asString());
    }
  }
  return plan;
}

std::string resolveFromOutput(const std::string& outputDir,
                              const std::string& path) {
  if (path.empty()) return path;
  const std::filesystem::path candidate(path);
  if (candidate.is_absolute()) return candidate.lexically_normal().string();
  if (fileExists(path)) {
    return std::filesystem::absolute(path).lexically_normal().string();
  }
  return pathJoin(outputDir, path);
}

int executeFreshRun(const RunOptions& options) {
  if (options.outputDir.empty()) {
    throw std::runtime_error("--out is required");
  }
  ensureDirectory(options.outputDir);
  const std::string stagedInput = pathJoin(options.outputDir, "inputs");
  stageInputFiles(options.inputDir, stagedInput);
  PreparedInput prepared = prepareInput(stagedInput, stagedInput);

  const VulkanProbe probe = VulkanAccelerator::probeOnly();
  ResourcePlan plan = makeResourcePlan(
      options.budgetMb, prepared.timeline.frames.size(),
      options.disableVulkan, probe);
  BackendChoice backend = chooseBackend(plan);
  if (backend.fallback) {
    plan.degradations.push_back(
        "Vulkan compute path failed; continued with CPU reference backend");
  }
  const CompressionConfig config = configFromPlan(plan);

  GlfwRuntime glfw;
  std::string glfwError;
  if (!glfw.initialize(options.openWindow, &glfwError)) {
    throw std::runtime_error("GLFW initialization failed: " + glfwError);
  }
  glfw.setStatus("Skeletal keyframe compression - " + plan.mode +
                 " - preparing");

  Compressor compressor(config, *backend.evaluator);
  CompressionState state;
  const RunOutcome outcome =
      compressor.advance(state, prepared.timeline, options.pauseAfterChunks);
  const std::string recordRoot = normalizedPath(options.outputDir);

  if (outcome.paused) {
    const std::string checkpointPath = pathJoin(options.outputDir, "checkpoint.json");
    const Json checkpoint = makeCheckpoint(
        prepared, plan, config, backend, outcome.state,
        prepared.scenario.scenarioId + "-" +
            prepared.manifestHash.substr(0, 12));
    writeRecordWithSelfCheck(checkpointPath, checkpoint);
    const Json evidence =
        makeCheckpointEvidenceForFile(outcome.state, options.outputDir,
                                      checkpointPath);
    Json record = makeRecord(
        prepared, plan, backend, &glfw, "PAUSED_AWAITING_RESUME", config,
        outcome.state, nullptr, "", "",
        evidence, defaultComparison("resume has not completed"),
        defaultComparison("resume has not completed"), false, recordRoot,
        options.openWindow);
    glfw.setStatus("Paused after " +
                   std::to_string(outcome.state.chunksCompleted) +
                   " chunks - checkpoint saved");
    writeFinalArtifacts(options.outputDir, record);
    glfw.poll();
    std::cout << "{\"status\":\"PAUSED\",\"chunks\":"
              << outcome.state.chunksCompleted << ",\"checkpoint\":\""
              << evidence.at("path").asString() << "\"}\n";
    return 0;
  }

  const CompressionResult result =
      compressor.finish(outcome.state, prepared.timeline);
  const auto written = writeCompressedResult(
      options.outputDir, result, prepared.scenario.bones,
      "final-compressed.json");
  const Json resultJson = readJsonFile(pathJoin(options.outputDir, written.first));
  const Json checkpointData = noCheckpointEvidence();
  const std::string baseline =
      resolveFromOutput(options.outputDir, options.baselinePath);
  const std::string degraded =
      resolveFromOutput(options.outputDir, options.degradedPath);
  Json baselineComparison =
      attachBaseline(resultJson, recordRoot, baseline, Json(nullptr));
  Json degradedComparison =
      attachDegraded(resultJson, recordRoot, degraded, "degraded");
  Json record = makeRecord(
      prepared, plan, backend, &glfw, "COMPLETE", config, outcome.state,
      &result, written.first, written.second, checkpointData,
      baselineComparison, degradedComparison, false, recordRoot,
      options.openWindow);
  glfw.setStatus("Complete - " + std::to_string(result.keyframeCount) +
                 " keyframes");
  writeFinalArtifacts(options.outputDir, record);
  glfw.poll();
  std::cout << "{\"status\":\"COMPLETE\",\"mode\":\"" << plan.mode
            << "\",\"backend\":\"" << backend.selected
            << "\",\"keyframes\":" << result.keyframeCount
            << ",\"source_frames\":" << result.sourceFrameCount << "}\n";
  return 0;
}

void removeDemoOutputs(const std::string& outputDir) {
  for (const char* name :
       {"inputs", "baseline", "degraded", "checkpoint.json",
        "final-compressed.json", "baseline-compressed.json",
        "degraded-compressed.json", "review-record.json",
        "review-report.md", "manifest.json", "verification.json",
        "reload-check.json", "review-record.roundtrip.json",
        "checkpoint.roundtrip.json", "final-compressed.roundtrip.json"}) {
    std::error_code error;
    std::filesystem::remove_all(pathJoin(outputDir, name), error);
  }
}

int executeResume(const std::string& checkpointPath,
                  const std::string& outputDir,
                  std::uint64_t additionalPauseChunks, bool openWindow,
                  const std::string& baselinePath,
                  const std::string& degradedPath) {
  const Json checkpoint = readJsonFile(checkpointPath);
  if (checkpoint.at("schema").asString() != "kfc.checkpoint/v1") {
    throw std::runtime_error("unsupported checkpoint schema");
  }
  if (checkpoint.at("status").asString() != "PAUSED") {
    throw std::runtime_error("checkpoint is not in PAUSED state");
  }
  const Json& inputBlock = checkpoint.at("input");
  std::string inputDir = inputBlock.at("source_directory").asString();
  if (!fileExists(pathJoin(inputDir, "scenario.json"))) {
    const std::string localInput =
        pathJoin(std::filesystem::path(checkpointPath).parent_path().string(),
                 "inputs");
    if (fileExists(pathJoin(localInput, "scenario.json"))) {
      inputDir = localInput;
    }
  }
  PreparedInput prepared = prepareInput(inputDir, inputDir);
  const Json& expectedHashes = inputBlock.at("hashes");
  if (prepared.scenarioHash != expectedHashes.at("scenario").asString() ||
      prepared.deviceAHash !=
          expectedHashes.at("device_a_capture").asString() ||
      prepared.deviceBHash !=
          expectedHashes.at("device_b_capture").asString() ||
      prepared.manifestHash !=
          expectedHashes.at("input_manifest").asString()) {
    throw std::runtime_error(
        "checkpoint input hashes do not match current inputs");
  }
  const CompressionConfig config =
      configFromJson(checkpoint.at("compression_config"));
  const VulkanProbe probe = VulkanAccelerator::probeOnly();
  ResourcePlan plan =
      planFromJson(checkpoint.at("resource_plan"), probe);
  BackendChoice backend;
  backend.probe = probe;
  const std::string expectedBackend =
      checkpoint.at("backend").at("selected").asString();
  backend.requested = checkpoint.at("backend").at("requested").asString();
  if (expectedBackend == "vulkan-compute") {
    std::string error;
    backend.vulkan = VulkanAccelerator::create(&error);
    if (backend.vulkan == nullptr) {
      throw std::runtime_error(
          "checkpoint requires Vulkan backend but it is unavailable: " + error);
    }
    backend.evaluator = backend.vulkan.get();
    backend.selected = backend.vulkan->name();
    backend.detail = backend.vulkan->detail();
    backend.probe = backend.vulkan->probe();
  } else {
    backend.evaluator = &backend.cpu;
    backend.selected = backend.cpu.name();
    backend.detail = backend.cpu.detail();
  }

  CompressionState state =
      compressionStateFromJson(checkpoint.at("state"));
  const Json pausedState = checkpoint.at("state");
  const CompressionState startingState = state;
  Json checkpointData =
      checkpointObjectFromFile(checkpointPath, outputDir);
  checkpointData["resume_loaded"] = true;
  checkpointData["resume_loaded_at_utc"] = isoUtcNow();
  checkpointData["resume_started_chunks"] = startingState.chunksCompleted;
  checkpointData["resume_started_work_units"] = startingState.workUnits;

  GlfwRuntime glfw;
  std::string glfwError;
  if (!glfw.initialize(openWindow, &glfwError)) {
    throw std::runtime_error("GLFW initialization failed: " + glfwError);
  }
  glfw.setStatus("Resumed from checkpoint - " + plan.mode);
  Compressor compressor(config, *backend.evaluator);
  const std::uint64_t pauseTarget =
      additionalPauseChunks == 0
          ? 0
          : state.chunksCompleted + additionalPauseChunks;
  const RunOutcome outcome =
      compressor.advance(state, prepared.timeline, pauseTarget);
  const std::string recordRoot = normalizedPath(outputDir);

  if (outcome.paused) {
    const std::string newCheckpoint =
        pathJoin(outputDir, "checkpoint.json");
    const Json checkpointJson = makeCheckpoint(
        prepared, plan, config, backend, outcome.state,
        checkpoint.at("record_id").asString());
    writeRecordWithSelfCheck(newCheckpoint, checkpointJson);
    const Json evidence =
        makeCheckpointEvidenceForFile(outcome.state, outputDir, newCheckpoint);
    Json record = makeRecord(
        prepared, plan, backend, &glfw, "PAUSED_AWAITING_RESUME", config,
        outcome.state, nullptr, "", "", evidence,
        defaultComparison("resume paused again before finalization"),
        defaultComparison("resume paused again before finalization"), true,
        recordRoot, openWindow);
    writeFinalArtifacts(outputDir, record);
    glfw.poll();
    std::cout << "{\"status\":\"PAUSED\",\"chunks\":"
              << outcome.state.chunksCompleted << "}\n";
    return 0;
  }

  const CompressionResult result =
      compressor.finish(outcome.state, prepared.timeline);
  const auto written = writeCompressedResult(
      outputDir, result, prepared.scenario.bones, "final-compressed.json");
  const Json resultJson = readJsonFile(pathJoin(outputDir, written.first));
  const std::string baseline =
      resolveFromOutput(outputDir, baselinePath);
  const std::string degraded =
      resolveFromOutput(outputDir, degradedPath);
  Json baselineComparison =
      attachBaseline(resultJson, recordRoot, baseline, pausedState);
  if (baselineComparison.contains("verdict")) {
    checkpointData["resume_result_matches_baseline"] =
        baselineComparison.at("verdict").asString() == "MATCH";
  }
  Json degradedComparison =
      attachDegraded(resultJson, recordRoot, degraded, "degraded");
  Json record = makeRecord(
      prepared, plan, backend, &glfw, "COMPLETE", config, outcome.state,
      &result, written.first, written.second, checkpointData,
      baselineComparison, degradedComparison, true, recordRoot, openWindow);
  glfw.setStatus("Resume complete - " +
                 std::to_string(result.keyframeCount) + " keyframes");
  writeFinalArtifacts(outputDir, record);
  glfw.poll();
  const std::string verdict =
      baselineComparison.get("verdict") != nullptr
          ? baselineComparison.at("verdict").asString()
          : "NOT_COMPARED";
  std::cout << "{\"status\":\"COMPLETE\",\"resumed\":true,\"keyframes\":"
            << result.keyframeCount << ",\"verdict\":\"" << verdict
            << "\"}\n";
  return 0;
}

std::string recordRootFor(const std::string& recordPath) {
  return normalizedPath(
      std::filesystem::path(recordPath).parent_path().string());
}

std::string resolveRecordPath(const std::string& recordRoot,
                              const std::string& relative) {
  const std::filesystem::path candidate(relative);
  if (candidate.is_absolute()) return candidate.lexically_normal().string();
  return (std::filesystem::path(recordRoot) / candidate)
      .lexically_normal()
      .string();
}

Json verifyRecordInternal(const std::string& recordPath, bool strictReplay,
                          std::string* replayHash) {
  const Json record = readJsonFile(recordPath);
  const std::string root = recordRootFor(recordPath);
  Json checks = Json::array();
  auto addCheck = [&](const std::string& id, bool ok,
                      const std::string& detail) {
    Json entry = Json::object();
    entry["id"] = id;
    entry["ok"] = ok;
    entry["detail"] = detail;
    checks.push(entry);
    return ok;
  };
  bool allOk = true;
  allOk &= addCheck("schema",
                    record.at("schema").asString() ==
                        "kfc.review-record/v1",
                    "record schema is kfc.review-record/v1");
  const std::string status = record.at("status").asString();
  allOk &= addCheck("status", status == "COMPLETE" || status == "PAUSED",
                    "status=" + status);
  const std::string original = readTextFile(recordPath);
  std::string parseError;
  const Json parsed = Json::parse(original, &parseError);
  const std::string roundTrip = parsed.dump(2) + "\n";
  allOk &= addCheck("ordered_json_round_trip", parseError.empty() &&
                                                original == roundTrip,
                    "parse/dump is byte-stable and preserves object order");
  const Json& declaredOrder = record.at("field_order");
  const std::vector<std::string> actualOrder = fieldOrder(parsed);
  bool orderMatches = declaredOrder.size() == actualOrder.size();
  if (orderMatches) {
    for (std::size_t i = 0; i < actualOrder.size(); ++i) {
      if (declaredOrder.asArray()[i].asString() != actualOrder[i]) {
        orderMatches = false;
        break;
      }
    }
  }
  allOk &= addCheck("declared_field_order", orderMatches,
                    "record.field_order matches actual insertion order");
  allOk &= addCheck("field_order_preserved",
                    record.at("field_order_preserved").asBool(),
                    "self-check flag is true");

  const Json& input = record.at("input");
  std::string inputDir =
      resolveRecordPath(root, input.at("source_directory").asString());
  if (!fileExists(pathJoin(inputDir, "scenario.json"))) {
    inputDir = pathJoin(root, input.at("artifact_directory").asString());
  }
  bool inputOk = true;
  std::string inputDetail;
  try {
    const std::string scenarioPath = pathJoin(inputDir, "scenario.json");
    const std::string aPath =
        pathJoin(inputDir, input.at("devices").asArray()[0].at("file").asString());
    const std::string bPath =
        pathJoin(inputDir, input.at("devices").asArray()[1].at("file").asString());
    inputOk =
        fileHash(scenarioPath) ==
            input.at("hashes").at("scenario").asString() &&
        fileHash(aPath) ==
            input.at("hashes").at("device_a_capture").asString() &&
        fileHash(bPath) ==
            input.at("hashes").at("device_b_capture").asString() &&
        fileHash(pathJoin(inputDir, "input-manifest.json")) ==
            input.at("hashes").at("input_manifest").asString();
    inputDetail = "scenario, two captures and manifest hashes match";
  } catch (const std::exception& exception) {
    inputOk = false;
    inputDetail = exception.what();
  }
  allOk &= addCheck("input_hashes", inputOk, inputDetail);

  bool evidenceOk = true;
  std::string evidenceDetail = "all file evidence hashes match";
  for (const Json& evidence : record.at("evidence").asArray()) {
    if (evidence.at("kind").asString() != "file") continue;
    const std::string path =
        resolveRecordPath(root, evidence.at("path").asString());
    try {
      if (fileHash(path) != evidence.at("sha256").asString()) {
        evidenceOk = false;
        evidenceDetail = "hash mismatch: " + evidence.at("path").asString();
        break;
      }
    } catch (const std::exception& exception) {
      evidenceOk = false;
      evidenceDetail = exception.what();
      break;
    }
  }
  allOk &= addCheck("evidence_hashes", evidenceOk, evidenceDetail);

  const Json& checkpointEvidenceJson = record.at("checkpoint_evidence");
  if (checkpointEvidenceJson.contains("path")) {
    bool checkpointOk = false;
    std::string checkpointDetail = "checkpoint could not be validated";
    try {
      const std::string checkpointPath =
          resolveRecordPath(root, checkpointEvidenceJson.at("path").asString());
      const std::string checkpointHash = fileHash(checkpointPath);
      const Json checkpointJson = readJsonFile(checkpointPath);
      const CompressionState savedState =
          compressionStateFromJson(checkpointJson.at("state"));
      checkpointOk =
          checkpointHash == checkpointEvidenceJson.at("sha256").asString() &&
          checkpointJson.at("schema").asString() == "kfc.checkpoint/v1" &&
          selectedIndicesHash(
              compressionStateToJson(savedState).at("selected_indices")) ==
              checkpointEvidenceJson.at("selected_prefix_hash").asString();
      checkpointDetail =
          "checkpoint hash and selected-prefix hash match the saved state";
    } catch (const std::exception& exception) {
      checkpointDetail = exception.what();
    }
    allOk &= addCheck("checkpoint_integrity", checkpointOk, checkpointDetail);
  }

  const std::string manifestPath = pathJoin(root, "manifest.json");
  if (fileExists(manifestPath)) {
    bool manifestOk = true;
    std::string manifestDetail = "all manifest hashes match";
    try {
      const Json manifest = readJsonFile(manifestPath);
      for (const Json& entry : manifest.at("files").asArray()) {
        const std::string path =
            resolveRecordPath(root, entry.at("path").asString());
        if (fileHash(path) != entry.at("sha256").asString()) {
          manifestOk = false;
          manifestDetail =
              "manifest hash mismatch: " + entry.at("path").asString();
          break;
        }
      }
    } catch (const std::exception& exception) {
      manifestOk = false;
      manifestDetail = exception.what();
    }
    allOk &= addCheck("artifact_manifest", manifestOk, manifestDetail);
  }

  const Json& resultSummaryJson = record.at("result");
  if (status == "COMPLETE") {
    bool resultOk = true;
    std::string resultDetail = "result hash and internal counts match";
    try {
      const std::string resultPath =
          resolveRecordPath(root, resultSummaryJson.at("path").asString());
      if (fileHash(resultPath) !=
          resultSummaryJson.at("sha256").asString()) {
        resultOk = false;
        resultDetail = "result file hash mismatch";
      } else {
        const Json result = readJsonFile(resultPath);
        const Json& keyframes = result.at("keyframes");
        bool increasing = true;
        Json indices = Json::array();
        for (std::size_t i = 0; i < keyframes.size(); ++i) {
          const std::int64_t index =
              keyframes.asArray()[i].at("source_index").asInt();
          indices.push(index);
          if (index < 0 ||
              index >= resultSummaryJson.at("source_frame_count").asInt()) {
            increasing = false;
          }
          if (i > 0 &&
              index <= keyframes.asArray()[i - 1].at("source_index").asInt()) {
            increasing = false;
          }
        }
        resultOk = increasing &&
                   result.at("keyframe_count").asInt() ==
                       static_cast<std::int64_t>(keyframes.size()) &&
                   result.at("keyframe_count").asInt() ==
                       resultSummaryJson.at("keyframe_count").asInt() &&
                   result.at("gap_count").asInt() ==
                       resultSummaryJson.at("gap_count").asInt() &&
                   selectedIndicesHash(indices) ==
                       resultSummaryJson.at("selected_indices_hash").asString() &&
                   result.at("state").at("selected_indices").dump(2) ==
                       indices.dump(2);
        if (!resultOk) {
          resultDetail =
              "result sequence, counts or selected-index hash mismatch";
        }
      }
    } catch (const std::exception& exception) {
      resultOk = false;
      resultDetail = exception.what();
    }
    allOk &= addCheck("result_integrity", resultOk, resultDetail);

    const Json& compressionConfig =
        record.at("pipeline").at("compression_config");
    const double angularTolerance =
        compressionConfig.at("angular_tolerance_deg").asDouble();
    const double translationTolerance =
        compressionConfig.at("translation_tolerance_m").asDouble();
    const bool forced =
        resultSummaryJson.at("work").at("forced_steps").asInt() != 0;
    const double maxAngular =
        resultSummaryJson.at("metrics").at("max_angular_error_deg").asDouble();
    const double maxTranslation =
        resultSummaryJson.at("metrics").at("max_translation_error_m").asDouble();
    const bool qualityOk =
        forced ||
        (maxAngular <= angularTolerance + 1e-6 &&
         maxTranslation <= translationTolerance + 1e-6);
    allOk &= addCheck(
        "quality_bounds", qualityOk,
        forced ? "forced steps are recorded; tolerance may be exceeded by design"
               : "result errors are within the recorded compression tolerance");
  }

  const Json& baseline = record.at("baseline_comparison");
  if (status == "COMPLETE" && baseline.contains("path")) {
    bool baselineOk = false;
    std::string detail = "baseline comparison could not be read";
    try {
      const std::string currentPath =
          resolveRecordPath(root, resultSummaryJson.at("path").asString());
      const std::string baselinePath =
          resolveRecordPath(root, baseline.at("path").asString());
      const Json compared =
          compareResults(readJsonFile(currentPath), readJsonFile(baselinePath),
                         "current", "baseline");
      baselineOk = compared.at("verdict").asString() == "MATCH" &&
                   baseline.at("verdict").asString() == "MATCH";
      detail = "current and baseline result hashes and keyframes match";
    } catch (const std::exception& exception) {
      detail = exception.what();
    }
    allOk &= addCheck("baseline_result", baselineOk, detail);
  }

  if (strictReplay && status == "COMPLETE") {
    bool replayMatches = false;
    try {
      const Json replay =
          replayRecordInternal(recordPath, &replayMatches, replayHash);
      allOk &= addCheck(
          "strict_replay", replayMatches,
          "replayed hash " + replay.at("replayed_result_sha256").asString());
    } catch (const std::exception& exception) {
      allOk &= addCheck("strict_replay", false, exception.what());
    }
  }

  Json rootJson = Json::object();
  rootJson["verified"] = allOk;
  rootJson["record"] = normalizedPath(recordPath);
  rootJson["status"] = status;
  rootJson["field_order_preserved"] = true;
  rootJson["checks"] = checks;
  if (replayHash != nullptr && !replayHash->empty()) {
    rootJson["replay_hash"] = *replayHash;
  }
  return rootJson;
}

Json replayRecordInternal(const std::string& recordPath, bool* matches,
                          std::string* replayHash) {
  const Json record = readJsonFile(recordPath);
  const std::string root = recordRootFor(recordPath);
  if (record.at("status").asString() != "COMPLETE") {
    throw std::runtime_error("replay requires a COMPLETE record");
  }
  const Json& input = record.at("input");
  std::string inputDir =
      resolveRecordPath(root, input.at("source_directory").asString());
  if (!fileExists(pathJoin(inputDir, "scenario.json"))) {
    inputDir = pathJoin(root, input.at("artifact_directory").asString());
  }
  PreparedInput prepared = prepareInput(inputDir, inputDir);
  if (prepared.scenarioHash != input.at("hashes").at("scenario").asString() ||
      prepared.deviceAHash !=
          input.at("hashes").at("device_a_capture").asString() ||
      prepared.deviceBHash !=
          input.at("hashes").at("device_b_capture").asString() ||
      prepared.manifestHash !=
          input.at("hashes").at("input_manifest").asString()) {
    throw std::runtime_error("replay input hashes do not match record");
  }
  const CompressionConfig config =
      configFromJson(record.at("pipeline").at("compression_config"));
  const VulkanProbe probe = VulkanAccelerator::probeOnly();
  const ResourcePlan plan =
      planFromJson(record.at("resource_plan"), probe);
  BackendChoice backend;
  backend.probe = probe;
  const std::string expected =
      record.at("backend").at("selected").asString();
  backend.requested = record.at("backend").at("requested").asString();
  if (expected == "vulkan-compute") {
    std::string error;
    backend.vulkan = VulkanAccelerator::create(&error);
    if (backend.vulkan == nullptr) {
      throw std::runtime_error("replay cannot create required Vulkan backend: " +
                               error);
    }
    backend.evaluator = backend.vulkan.get();
    backend.selected = backend.vulkan->name();
    backend.detail = backend.vulkan->detail();
  } else {
    backend.evaluator = &backend.cpu;
    backend.selected = backend.cpu.name();
    backend.detail = backend.cpu.detail();
  }
  Compressor compressor(config, *backend.evaluator);
  CompressionState state;
  const RunOutcome outcome =
      compressor.advance(state, prepared.timeline, 0);
  if (!outcome.complete) throw std::runtime_error("replay did not complete");
  const CompressionResult result =
      compressor.finish(outcome.state, prepared.timeline);
  const Json replayJson =
      compressionResultToJson(result, prepared.scenario.bones);
  const std::string replayText = replayJson.dump(2) + "\n";
  *replayHash = Sha256::of(replayText);
  const std::string expectedHash =
      record.at("result").at("sha256").asString();
  *matches = *replayHash == expectedHash;
  Json output = Json::object();
  output["record"] = normalizedPath(recordPath);
  output["expected_result_sha256"] = expectedHash;
  output["replayed_result_sha256"] = *replayHash;
  output["keyframe_count"] = static_cast<std::uint64_t>(result.keyframeCount);
  output["verdict"] = *matches ? "MATCH" : "MISMATCH";
  output["backend"] = backend.selected;
  return output;
}

Json reloadCheck(const std::string& filePath, std::string* roundtripPath) {
  const std::string original = readTextFile(filePath);
  std::string error;
  const Json first = Json::parse(original, &error);
  if (!error.empty()) throw std::runtime_error("parse failed: " + error);
  const std::string serialized = first.dump(2) + "\n";
  const Json second = Json::parse(serialized, &error);
  if (!error.empty()) throw std::runtime_error("reparse failed: " + error);
  const bool stable = serialized == second.dump(2) + "\n";
  const bool orderPreserved = fieldOrder(first) == fieldOrder(second);
  const std::filesystem::path path(filePath);
  *roundtripPath =
      (path.parent_path() / (path.stem().string() + ".roundtrip.json"))
          .string();
  writeTextFile(*roundtripPath, serialized);
  Json output = Json::object();
  output["file"] = normalizedPath(filePath);
  output["roundtrip_file"] = normalizedPath(*roundtripPath);
  output["field_order_preserved"] = orderPreserved;
  output["serialization_stable"] = stable;
  output["top_level_fields"] = static_cast<std::uint64_t>(first.size());
  output["verdict"] = (stable && orderPreserved) ? "MATCH" : "MISMATCH";
  return output;
}

}  // namespace

int commandDemo(const DemoOptions& options) {
  if (options.outputDir.empty()) {
    throw std::runtime_error("--out is required for demo");
  }
  if (options.pauseAfterChunks == 0) {
    throw std::runtime_error(
        "demo requires --pause-after-chunks greater than zero");
  }
  ensureDirectory(options.outputDir);
  removeDemoOutputs(options.outputDir);
  const std::string stagedInput = pathJoin(options.outputDir, "inputs");
  stageInputFiles("", stagedInput);

  RunOptions baseline;
  baseline.inputDir = stagedInput;
  baseline.outputDir = pathJoin(options.outputDir, "baseline");
  baseline.budgetMb = options.budgetMb;
  baseline.disableVulkan = options.disableVulkan;
  executeFreshRun(baseline);

  const double degradedBudget = options.budgetMb >= 8.0 ? 6.0 : 2.0;
  RunOptions degraded;
  degraded.inputDir = stagedInput;
  degraded.outputDir = pathJoin(options.outputDir, "degraded");
  degraded.budgetMb = degradedBudget;
  degraded.disableVulkan = true;
  executeFreshRun(degraded);

  RunOptions paused;
  paused.inputDir = stagedInput;
  paused.outputDir = options.outputDir;
  paused.budgetMb = options.budgetMb;
  paused.pauseAfterChunks = options.pauseAfterChunks;
  paused.openWindow = options.openWindow;
  paused.disableVulkan = options.disableVulkan;
  executeFreshRun(paused);

  const std::string checkpointPath =
      pathJoin(options.outputDir, "checkpoint.json");
  if (!fileExists(checkpointPath)) {
    throw std::runtime_error("demo did not produce a checkpoint");
  }
  const std::string baselinePath = pathJoin(
      pathJoin(options.outputDir, "baseline"), "final-compressed.json");
  const std::string degradedPath = pathJoin(
      pathJoin(options.outputDir, "degraded"), "final-compressed.json");
  executeResume(checkpointPath, options.outputDir, 0, options.openWindow,
                baselinePath, degradedPath);
  const Json finalRecord =
      readJsonFile(pathJoin(options.outputDir, "review-record.json"));
  if (finalRecord.at("baseline_comparison").at("verdict").asString() !=
      "MATCH") {
    throw std::runtime_error("demo baseline comparison is not MATCH");
  }
  if (!finalRecord.at("baseline_comparison")
           .at("checkpoint_prefix")
           .at("prefix_matches")
           .asBool()) {
    throw std::runtime_error("demo checkpoint prefix does not match baseline");
  }
  const std::string degradedVerdict =
      finalRecord.at("degraded_comparison").at("verdict").asString();
  if (degradedVerdict != "EXPECTED_DEGRADATION" &&
      degradedVerdict != "IDENTICAL") {
    throw std::runtime_error("demo degraded comparison is unexpected");
  }
  std::string replayHash;
  const Json verification =
      verifyRecordInternal(pathJoin(options.outputDir, "review-record.json"),
                           true, &replayHash);
  writeJson(pathJoin(options.outputDir, "verification.json"), verification);
  std::string roundtripPath;
  const Json reload =
      reloadCheck(pathJoin(options.outputDir, "review-record.json"),
                  &roundtripPath);
  writeJson(pathJoin(options.outputDir, "reload-check.json"), reload);
  writeManifest(options.outputDir);
  if (!verification.at("verified").asBool() ||
      reload.at("verdict").asString() != "MATCH") {
    throw std::runtime_error("demo final self-verification failed");
  }
  std::cout << "{\"status\":\"DEMO_COMPLETE\",\"record\":\""
            << pathJoin(options.outputDir, "review-record.json") << "\"}\n";
  return 0;
}

int commandRun(const RunOptions& options) {
  return executeFreshRun(options);
}

int commandResume(const std::string& checkpointPath,
                  const std::string& outputDir,
                  std::uint64_t additionalPauseChunks, bool openWindow,
                  const std::string& baselinePath,
                  const std::string& degradedPath) {
  std::string out = outputDir;
  if (out.empty()) {
    out = std::filesystem::path(checkpointPath).parent_path().string();
  }
  return executeResume(checkpointPath, out, additionalPauseChunks, openWindow,
                       baselinePath, degradedPath);
}

int commandVerify(const std::string& recordPath, bool strictReplay) {
  std::string replayHash;
  const Json result =
      verifyRecordInternal(recordPath, strictReplay, &replayHash);
  std::cout << result.dump(2) << "\n";
  return result.at("verified").asBool() ? 0 : 1;
}

int commandReplay(const std::string& recordPath) {
  bool matches = false;
  std::string replayHash;
  const Json result =
      replayRecordInternal(recordPath, &matches, &replayHash);
  std::cout << result.dump(2) << "\n";
  return matches ? 0 : 1;
}

int commandReloadCheck(const std::string& filePath) {
  std::string roundtripPath;
  const Json result = reloadCheck(filePath, &roundtripPath);
  std::cout << result.dump(2) << "\n";
  return result.at("verdict").asString() == "MATCH" ? 0 : 1;
}

int commandInspect(const std::string& filePath) {
  const Json root = readJsonFile(filePath);
  Json output = Json::object();
  output["file"] = normalizedPath(filePath);
  output["type"] = root.typeName();
  const std::vector<std::string> order = fieldOrder(root);
  output["field_order"] = stringArray(order);
  if (root.isObject()) {
    if (root.contains("schema")) output["schema"] = root.at("schema");
    if (root.contains("status")) output["status"] = root.at("status");
    if (root.contains("record_id")) output["record_id"] = root.at("record_id");
    if (root.contains("verdict")) output["verdict"] = root.at("verdict");
  }
  std::cout << output.dump(2) << "\n";
  return 0;
}

int commandCompare(const std::string& leftPath, const std::string& rightPath) {
  const Json left = readJsonFile(leftPath);
  const Json right = readJsonFile(rightPath);
  const Json result = compareResults(left, right, leftPath, rightPath);
  std::cout << result.dump(2) << "\n";
  return result.at("verdict").asString() == "MATCH" ? 0 : 1;
}

int commandManifest(const std::string& outputDir) {
  writeManifest(outputDir);
  const Json manifest = readJsonFile(pathJoin(outputDir, "manifest.json"));
  std::cout << "{\"status\":\"MANIFEST_WRITTEN\",\"files\":"
            << manifest.at("files").size() << "}\n";
  return 0;
}

}  // namespace kfc
