#include "model.hpp"

#include "sha256.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <limits>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace kfc {

namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;
constexpr double kFrameIntervalUs = 25000.0;
constexpr double kDurationUs = 6400000.0;
constexpr double kClusterUs = 5000.0;

struct DeviceShape {
  const char* id;
  double offsetUs;
  double driftPpm;
  std::array<double, 4> anchorJitterUs;
  std::array<double, 2> captureStartUs;
  std::array<double, 2> captureEndUs;
};

constexpr DeviceShape kDeviceA = {
    "capture-a", -42000.0, -180.0, {620.0, -940.0, 1180.0, -510.0},
    {0.0, 3200000.0}, {2450000.0, 6400000.0}};
constexpr DeviceShape kDeviceB = {
    "capture-b", 137000.0, 540.0, {-870.0, 1310.0, -460.0, 730.0},
    {0.0, 2750000.0}, {1900000.0, 6400000.0}};

constexpr std::array<const char*, 4> kAnchorIds = {
    "clap-a", "clap-b", "clap-c", "clap-d"};
constexpr std::array<double, 4> kAnchorReferenceUs = {
    500000.0, 2600000.0, 4200000.0, 5800000.0};

constexpr std::array<Vec3, kBoneCount> kBoneTranslation = {{
    {0.0, 0.92, 0.0},
    {0.0, 0.28, 0.0},
    {0.0, 0.31, 0.01},
    {-0.19, 0.20, 0.0},
    {0.19, 0.20, 0.0},
    {-0.10, -0.06, 0.0},
    {0.10, -0.06, 0.0},
    {0.0, -0.16, -0.08},
}};

constexpr std::array<Vec3, kBoneCount> kRotationAxis = {{
    {0.0, 1.0, 0.0},
    {0.0, 0.0, 1.0},
    {0.25, 0.0, 0.97},
    {0.1, 0.9, 0.42},
    {-0.1, 0.9, 0.42},
    {0.9, 0.1, 0.42},
    {-0.9, 0.1, 0.42},
    {0.0, 0.88, -0.47},
}};

constexpr std::array<double, kBoneCount> kBaseAngleDeg = {
    0.0, 4.0, -2.0, 14.0, -14.0, 3.0, -3.0, -8.0};
constexpr std::array<double, kBoneCount> kAmplitudeDeg = {
    11.0, 18.0, 9.0, 32.0, 32.0, 24.0, 24.0, 15.0};
constexpr std::array<double, kBoneCount> kPhase = {
    0.0, 0.7, 1.1, 2.2, 2.9, 3.4, 4.1, 5.0};

double clamp(double value, double low, double high) {
  return std::max(low, std::min(high, value));
}

Quat fromAxisAngle(Vec3 axis, double radians) {
  const double length = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
  if (length <= 1e-12) return Quat{};
  const double half = radians * 0.5;
  const double s = std::sin(half) / length;
  return normalize(Quat{std::cos(half), axis.x * s, axis.y * s, axis.z * s});
}

double deviceTime(double referenceUs, const DeviceShape& shape) {
  return referenceUs + shape.offsetUs +
         referenceUs * shape.driftPpm / 1000000.0;
}

std::vector<std::uint32_t> sequenceForWindows(const DeviceShape& shape) {
  std::vector<std::uint32_t> result;
  const std::uint32_t maxSequence =
      static_cast<std::uint32_t>(std::llround(kDurationUs / kFrameIntervalUs));
  for (std::uint32_t sequence = 0; sequence <= maxSequence; ++sequence) {
    const double timeUs = static_cast<double>(sequence) * kFrameIntervalUs;
    for (std::size_t window = 0; window < shape.captureStartUs.size(); ++window) {
      if (timeUs >= shape.captureStartUs[window] - 0.5 &&
          timeUs <= shape.captureEndUs[window] + 0.5) {
        result.push_back(sequence);
        break;
      }
    }
  }
  return result;
}

DeviceCapture makeCapture(const DeviceShape& shape,
                          const std::vector<std::string>& bones) {
  DeviceCapture capture;
  capture.schema = "kfc.device-capture/v1";
  capture.deviceId = shape.id;
  capture.localTimebase = "device_monotonic_us";
  capture.clockOffsetUs = shape.offsetUs;
  capture.clockDriftPpm = shape.driftPpm;
  capture.animationId = "moonlit-turn-6400ms";
  capture.frameIntervalUs = kFrameIntervalUs;
  capture.durationUs = kDurationUs;
  capture.bones = bones;
  capture.frameSequences = sequenceForWindows(shape);
  for (const std::uint32_t sequence : capture.frameSequences) {
    const double referenceUs = static_cast<double>(sequence) * kFrameIntervalUs;
    PoseFrame frame = analyticPose(referenceUs, shape.id[8] == 'a' ? 'A' : 'B');
    frame.sequence = sequence;
    frame.timeUs = std::round(deviceTime(referenceUs, shape));
    capture.samples.push_back(frame);
  }
  for (std::size_t i = 0; i < kAnchorIds.size(); ++i) {
    SyncAnchor anchor;
    anchor.id = kAnchorIds[i];
    anchor.referenceUs = kAnchorReferenceUs[i];
    anchor.deviceUs =
        std::round(deviceTime(kAnchorReferenceUs[i], shape) + shape.anchorJitterUs[i]);
    capture.anchors.push_back(anchor);
  }
  for (std::size_t i = 0; i < shape.captureStartUs.size(); ++i) {
    CaptureWindow window;
    window.startUs = shape.captureStartUs[i];
    window.endUs = shape.captureEndUs[i];
    capture.windows.push_back(window);
  }
  return capture;
}

Json vec3Json(const Vec3& value) {
  Json array = Json::array();
  array.push(value.x);
  array.push(value.y);
  array.push(value.z);
  return array;
}

Json quatJson(const Quat& value) {
  Json array = Json::array();
  array.push(value.w);
  array.push(value.x);
  array.push(value.y);
  array.push(value.z);
  return array;
}

Vec3 readVec3(const Json& value) {
  const auto& array = value.asArray();
  if (array.size() != 3) throw std::runtime_error("Vec3 must have 3 values");
  return Vec3{array[0].asDouble(), array[1].asDouble(), array[2].asDouble()};
}

Quat readQuat(const Json& value) {
  const auto& array = value.asArray();
  if (array.size() != 4) throw std::runtime_error("Quat must have 4 values");
  return Quat{array[0].asDouble(), array[1].asDouble(), array[2].asDouble(),
              array[3].asDouble()};
}

}  // namespace

Quat normalize(Quat value) {
  const double length = std::sqrt(value.w * value.w + value.x * value.x +
                                  value.y * value.y + value.z * value.z);
  if (length <= 1e-15) return Quat{};
  return Quat{value.w / length, value.x / length, value.y / length,
              value.z / length};
}

Quat multiply(Quat a, Quat b) {
  return Quat{
      a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
      a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
      a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
      a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}

double dot(Quat a, Quat b) {
  return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
}

Quat slerp(Quat a, Quat b, double t) {
  double cosine = dot(a, b);
  if (cosine < 0.0) {
    b = Quat{-b.w, -b.x, -b.y, -b.z};
    cosine = -cosine;
  }
  cosine = clamp(cosine, -1.0, 1.0);
  if (cosine > 0.9995) {
    return normalize(Quat{a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t,
                          a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t});
  }
  const double theta = std::acos(cosine);
  const double sine = std::sin(theta);
  const double wa = std::sin((1.0 - t) * theta) / sine;
  const double wb = std::sin(t * theta) / sine;
  return normalize(Quat{a.w * wa + b.w * wb, a.x * wa + b.x * wb,
                        a.y * wa + b.y * wb, a.z * wa + b.z * wb});
}

double quaternionAngleDeg(Quat a, Quat b) {
  const double absolute = std::fabs(dot(normalize(a), normalize(b)));
  return 2.0 * std::acos(clamp(absolute, 0.0, 1.0)) * 180.0 / kPi;
}

double vecLength(const Vec3& value) {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

std::vector<std::string> boneNames() {
  return {"root", "spine", "head", "upper_arm_l", "upper_arm_r",
          "thigh_l", "thigh_r", "tail"};
}

PoseFrame analyticPose(double timeUs, char device) {
  PoseFrame frame;
  frame.timeUs = timeUs;
  frame.device = device;
  const double seconds = timeUs / 1000000.0;
  const double primary = 2.0 * kPi * seconds / 1.45;
  const double secondary = 2.0 * kPi * seconds / 0.73;
  for (std::size_t index = 0; index < kBoneCount; ++index) {
    const double phase = kPhase[index];
    const double base = kBaseAngleDeg[index] * kPi / 180.0;
    const double primaryRadians =
        kAmplitudeDeg[index] * kPi / 180.0 * std::sin(primary + phase);
    const double secondaryRadians =
        (0.20 * kAmplitudeDeg[index]) * kPi / 180.0 *
        std::sin(secondary + phase * 1.7);
    Vec3 axis = kRotationAxis[index];
    const double axisTilt = 0.08 * std::sin(primary * 0.5 + phase);
    axis.x += axisTilt;
    axis.z += axisTilt * 0.5;
    frame.bones[index].rotation =
        fromAxisAngle(axis, base + primaryRadians + secondaryRadians);
    Vec3 translation = kBoneTranslation[index];
    const double bob = 0.018 * std::sin(primary + phase * 0.6);
    const double sway = 0.012 * std::sin(secondary + phase);
    translation.x += sway;
    translation.y += bob;
    translation.z += 0.009 * std::cos(primary * 0.8 + phase);
    frame.bones[index].translation = translation;
  }
  return frame;
}

Json bonesToJson(const std::array<BonePose, kBoneCount>& bones) {
  Json result = Json::array();
  for (const BonePose& bone : bones) {
    Json entry = Json::object();
    entry["r"] = quatJson(bone.rotation);
    entry["t"] = vec3Json(bone.translation);
    result.push(entry);
  }
  return result;
}

Json poseToJson(const PoseFrame& frame) {
  Json result = Json::object();
  result["time_us"] = frame.timeUs;
  result["device"] = std::string(1, frame.device);
  result["sequence"] = static_cast<std::uint64_t>(frame.sequence);
  result["bones"] = bonesToJson(frame.bones);
  return result;
}

Json captureToJson(const DeviceCapture& capture) {
  Json root = Json::object();
  root["schema"] = capture.schema;
  root["device_id"] = capture.deviceId;
  root["animation_id"] = capture.animationId;
  Json clock = Json::object();
  clock["timebase"] = capture.localTimebase;
  clock["nominal_offset_us"] = capture.clockOffsetUs;
  clock["nominal_drift_ppm"] = capture.clockDriftPpm;
  clock["fit_method"] = "least-squares affine fit over shared motion anchors";
  root["clock"] = clock;

  Json animation = Json::object();
  animation["frame_interval_us"] = capture.frameIntervalUs;
  animation["duration_us"] = capture.durationUs;
  Json bones = Json::array();
  for (const std::string& bone : capture.bones) bones.push(bone);
  animation["bone_order"] = bones;
  root["animation"] = animation;

  Json windows = Json::array();
  for (const CaptureWindow& window : capture.windows) {
    Json entry = Json::object();
    entry["from_us"] = window.startUs;
    entry["to_us"] = window.endUs;
    windows.push(entry);
  }
  root["capture_windows"] = windows;

  Json anchors = Json::array();
  for (const SyncAnchor& anchor : capture.anchors) {
    Json entry = Json::object();
    entry["anchor_id"] = anchor.id;
    entry["reference_us"] = anchor.referenceUs;
    entry["device_us"] = anchor.deviceUs;
    entry["kind"] = "shared_motion_marker";
    anchors.push(entry);
  }
  root["sync_anchors"] = anchors;

  Json samples = Json::array();
  for (const PoseFrame& frame : capture.samples) {
    Json entry = Json::object();
    entry["seq"] = static_cast<std::uint64_t>(frame.sequence);
    entry["device_us"] = frame.timeUs;
    entry["pose"] = bonesToJson(frame.bones);
    samples.push(entry);
  }
  root["samples"] = samples;
  return root;
}

Json scenarioToJson(const Scenario& scenario) {
  Json root = Json::object();
  root["schema"] = scenario.schema;
  root["scenario_id"] = scenario.scenarioId;
  root["description"] = scenario.description;
  root["technical_stack"] = Json::array();
  root["technical_stack"].push("C++17");
  root["technical_stack"].push("Vulkan compute");
  root["technical_stack"].push("GLFW");

  Json animation = Json::object();
  animation["animation_id"] = scenario.animationId;
  animation["frame_interval_us"] = scenario.frameIntervalUs;
  animation["duration_us"] = scenario.durationUs;
  Json bones = Json::array();
  for (const std::string& bone : scenario.bones) bones.push(bone);
  animation["bone_order"] = bones;
  root["animation"] = animation;

  Json devices = Json::array();
  Json deviceA = Json::object();
  deviceA["device_id"] = "capture-a";
  deviceA["role"] = "primary_pose_source";
  deviceA["capture_file"] = scenario.deviceAFile;
  devices.push(deviceA);
  Json deviceB = Json::object();
  deviceB["device_id"] = "capture-b";
  deviceB["role"] = "fill_and_cross_check";
  deviceB["capture_file"] = scenario.deviceBFile;
  devices.push(deviceB);
  root["devices"] = devices;

  Json gap = Json::object();
  gap["expected_from_us"] = scenario.intendedGapStartUs;
  gap["expected_to_us"] = scenario.intendedGapEndUs;
  gap["cause"] =
      "device A and device B capture windows do not overlap after clock mapping";
  gap["handling"] =
      "split compression at the observed union gap; never interpolate across it";
  root["missing_segment_expectation"] = gap;
  return root;
}

void generateScenario(const std::string& inputDir, std::uint32_t seed) {
  ensureDirectory(inputDir);
  const std::vector<std::string> bones = boneNames();
  const DeviceCapture captureA = makeCapture(kDeviceA, bones);
  const DeviceCapture captureB = makeCapture(kDeviceB, bones);

  Scenario scenario;
  scenario.schema = "kfc.scenario/v1";
  scenario.scenarioId = "two-device-clock-skew-20231122";
  scenario.description =
      "Deterministic two-device skeletal capture with an affine clock mismatch "
      "and a 300 ms union coverage hole.";
  scenario.animationId = captureA.animationId;
  scenario.frameIntervalUs = kFrameIntervalUs;
  scenario.durationUs = kDurationUs;
  scenario.bones = bones;
  scenario.deviceAFile = "device-a-capture.json";
  scenario.deviceBFile = "device-b-capture.json";
  scenario.intendedGapStartUs = 2450000.0;
  scenario.intendedGapEndUs = 2750000.0;

  Json seedMarker = Json::object();
  seedMarker["seed"] = static_cast<std::uint64_t>(seed);
  seedMarker["generator"] = "analytic-pose-v1";
  seedMarker["pose_units"] = "quaternion wxyz + translation metres";
  Json scenarioJson = scenarioToJson(scenario);
  scenarioJson["determinism"] = seedMarker;

  writeTextFile(pathJoin(inputDir, scenario.deviceAFile),
                captureToJson(captureA).dump(2) + "\n");
  writeTextFile(pathJoin(inputDir, scenario.deviceBFile),
                captureToJson(captureB).dump(2) + "\n");
  writeTextFile(pathJoin(inputDir, "scenario.json"),
                scenarioJson.dump(2) + "\n");

  Json manifest = Json::object();
  manifest["schema"] = "kfc.input-manifest/v1";
  Json files = Json::array();
  for (const std::string& name :
       {scenario.deviceAFile, scenario.deviceBFile, std::string("scenario.json")}) {
    Json entry = Json::object();
    entry["name"] = name;
    entry["sha256"] = Sha256::of(readTextFile(pathJoin(inputDir, name)));
    files.push(entry);
  }
  manifest["files"] = files;
  manifest["seed"] = static_cast<std::uint64_t>(seed);
  writeTextFile(pathJoin(inputDir, "input-manifest.json"),
                manifest.dump(2) + "\n");
}

Scenario loadScenario(const std::string& path) {
  std::string error;
  const Json root = Json::parse(readTextFile(path), &error);
  if (!error.empty()) throw std::runtime_error("scenario parse error: " + error);
  if (root.at("schema").asString() != "kfc.scenario/v1") {
    throw std::runtime_error("unsupported scenario schema");
  }
  Scenario scenario;
  scenario.schema = root.at("schema").asString();
  scenario.scenarioId = root.at("scenario_id").asString();
  scenario.description = root.at("description").asString();
  const Json& animation = root.at("animation");
  scenario.animationId = animation.at("animation_id").asString();
  scenario.frameIntervalUs = animation.at("frame_interval_us").asDouble();
  scenario.durationUs = animation.at("duration_us").asDouble();
  for (const Json& bone : animation.at("bone_order").asArray()) {
    scenario.bones.push_back(bone.asString());
  }
  const auto& devices = root.at("devices").asArray();
  if (devices.size() != 2) throw std::runtime_error("scenario needs two devices");
  scenario.deviceAFile = devices[0].at("capture_file").asString();
  scenario.deviceBFile = devices[1].at("capture_file").asString();
  const Json& gap = root.at("missing_segment_expectation");
  scenario.intendedGapStartUs = gap.at("expected_from_us").asDouble();
  scenario.intendedGapEndUs = gap.at("expected_to_us").asDouble();
  return scenario;
}

DeviceCapture loadCapture(const std::string& path) {
  std::string error;
  const Json root = Json::parse(readTextFile(path), &error);
  if (!error.empty()) throw std::runtime_error("capture parse error: " + error);
  DeviceCapture capture;
  capture.schema = root.at("schema").asString();
  if (capture.schema != "kfc.device-capture/v1") {
    throw std::runtime_error("unsupported capture schema: " + capture.schema);
  }
  capture.deviceId = root.at("device_id").asString();
  capture.animationId = root.at("animation_id").asString();
  const Json& clock = root.at("clock");
  capture.localTimebase = clock.at("timebase").asString();
  capture.clockOffsetUs = clock.at("nominal_offset_us").asDouble();
  capture.clockDriftPpm = clock.at("nominal_drift_ppm").asDouble();
  const Json& animation = root.at("animation");
  capture.frameIntervalUs = animation.at("frame_interval_us").asDouble();
  capture.durationUs = animation.at("duration_us").asDouble();
  for (const Json& bone : animation.at("bone_order").asArray()) {
    capture.bones.push_back(bone.asString());
  }
  if (capture.bones.size() != kBoneCount) {
    throw std::runtime_error("capture bone count mismatch");
  }
  for (const Json& window : root.at("capture_windows").asArray()) {
    CaptureWindow parsed;
    parsed.startUs = window.at("from_us").asDouble();
    parsed.endUs = window.at("to_us").asDouble();
    capture.windows.push_back(parsed);
  }
  for (const Json& anchor : root.at("sync_anchors").asArray()) {
    SyncAnchor parsed;
    parsed.id = anchor.at("anchor_id").asString();
    parsed.referenceUs = anchor.at("reference_us").asDouble();
    parsed.deviceUs = anchor.at("device_us").asDouble();
    capture.anchors.push_back(parsed);
  }
  for (const Json& sample : root.at("samples").asArray()) {
    PoseFrame frame;
    frame.sequence = static_cast<std::uint32_t>(sample.at("seq").asInt());
    frame.timeUs = sample.at("device_us").asDouble();
    frame.device = capture.deviceId == "capture-a" ? 'A' : 'B';
    const auto& pose = sample.at("pose").asArray();
    if (pose.size() != kBoneCount) {
      throw std::runtime_error("capture pose bone count mismatch");
    }
    for (std::size_t index = 0; index < kBoneCount; ++index) {
      frame.bones[index].rotation = readQuat(pose[index].at("r"));
      frame.bones[index].translation = readVec3(pose[index].at("t"));
    }
    capture.samples.push_back(frame);
    capture.frameSequences.push_back(frame.sequence);
  }
  if (capture.samples.empty()) throw std::runtime_error("capture has no samples");
  return capture;
}

ClockFit calibrateClock(const DeviceCapture& capture) {
  if (capture.anchors.size() < 2) {
    throw std::runtime_error("at least two sync anchors are required");
  }
  ClockFit fit;
  fit.anchorCount = capture.anchors.size();
  double meanDevice = 0.0;
  double meanReference = 0.0;
  for (const SyncAnchor& anchor : capture.anchors) {
    meanDevice += anchor.deviceUs;
    meanReference += anchor.referenceUs;
  }
  meanDevice /= static_cast<double>(capture.anchors.size());
  meanReference /= static_cast<double>(capture.anchors.size());
  double covariance = 0.0;
  double variance = 0.0;
  for (const SyncAnchor& anchor : capture.anchors) {
    const double dx = anchor.deviceUs - meanDevice;
    covariance += dx * (anchor.referenceUs - meanReference);
    variance += dx * dx;
  }
  if (variance <= 1e-9) throw std::runtime_error("sync anchors are degenerate");
  fit.slope = covariance / variance;
  fit.interceptUs = meanReference - fit.slope * meanDevice;
  double squareSum = 0.0;
  for (const SyncAnchor& anchor : capture.anchors) {
    const double predicted = fit.slope * anchor.deviceUs + fit.interceptUs;
    const double residual = anchor.referenceUs - predicted;
    fit.maxResidualUs = std::max(fit.maxResidualUs, std::fabs(residual));
    squareSum += residual * residual;
  }
  fit.rmsResidualUs =
      std::sqrt(squareSum / static_cast<double>(capture.anchors.size()));
  fit.offsetAtReferenceZeroUs = -fit.interceptUs / fit.slope;
  fit.driftPpmEstimate = (1.0 / fit.slope - 1.0) * 1000000.0;
  return fit;
}

ClockCalibration calibrateClocks(const DeviceCapture& a, const DeviceCapture& b) {
  ClockCalibration calibration;
  calibration.deviceA = calibrateClock(a);
  calibration.deviceB = calibrateClock(b);
  for (const SyncAnchor& anchor : a.anchors) {
    calibration.anchorIds.push_back(anchor.id);
  }
  return calibration;
}

double mapDeviceTime(const ClockFit& fit, double deviceUs) {
  return fit.slope * deviceUs + fit.interceptUs;
}

Json calibrationToJson(const ClockCalibration& calibration) {
  auto fitJson = [](const ClockFit& fit) {
    Json entry = Json::object();
    entry["mapping"] = "reference_us = slope * device_us + intercept_us";
    entry["anchor_count"] = static_cast<std::uint64_t>(fit.anchorCount);
    entry["slope"] = fit.slope;
    entry["intercept_us"] = fit.interceptUs;
    entry["device_us_at_reference_zero"] = fit.offsetAtReferenceZeroUs;
    entry["estimated_drift_ppm"] = fit.driftPpmEstimate;
    entry["rms_residual_us"] = fit.rmsResidualUs;
    entry["max_abs_residual_us"] = fit.maxResidualUs;
    return entry;
  };
  Json root = Json::object();
  root["method"] = "least-squares affine fit over shared motion anchors";
  Json anchors = Json::array();
  for (const std::string& id : calibration.anchorIds) anchors.push(id);
  root["anchor_ids"] = anchors;
  root["device_a"] = fitJson(calibration.deviceA);
  root["device_b"] = fitJson(calibration.deviceB);
  root["time_units"] = "microseconds";
  return root;
}

FusedTimeline fuseCaptures(const DeviceCapture& a, const DeviceCapture& b,
                           const ClockCalibration& calibration) {
  struct Entry {
    double mappedUs = 0.0;
    PoseFrame frame;
  };
  std::vector<Entry> entries;
  entries.reserve(a.samples.size() + b.samples.size());
  for (const PoseFrame& frame : a.samples) {
    entries.push_back(Entry{mapDeviceTime(calibration.deviceA, frame.timeUs), frame});
  }
  for (const PoseFrame& frame : b.samples) {
    entries.push_back(Entry{mapDeviceTime(calibration.deviceB, frame.timeUs), frame});
  }
  std::stable_sort(entries.begin(), entries.end(),
                   [](const Entry& left, const Entry& right) {
                     if (left.mappedUs != right.mappedUs) {
                       return left.mappedUs < right.mappedUs;
                     }
                     return left.frame.device == 'A' && right.frame.device != 'A';
                   });

  struct Cluster {
    std::vector<Entry> members;
    double startUs = 0.0;
  };
  std::vector<Cluster> clusters;
  for (const Entry& entry : entries) {
    if (clusters.empty() ||
        entry.mappedUs - clusters.back().startUs > kClusterUs) {
      Cluster cluster;
      cluster.startUs = entry.mappedUs;
      cluster.members.push_back(entry);
      clusters.push_back(std::move(cluster));
    } else {
      clusters.back().members.push_back(entry);
    }
  }

  FusedTimeline timeline;
  for (const Cluster& cluster : clusters) {
    const Entry* primary = nullptr;
    const Entry* secondary = nullptr;
    double sumTime = 0.0;
    for (const Entry& member : cluster.members) {
      sumTime += member.mappedUs;
      if (member.frame.device == 'A') {
        if (primary == nullptr) primary = &member;
      } else if (secondary == nullptr) {
        secondary = &member;
      }
    }
    if (primary == nullptr) primary = &cluster.members.front();
    if (secondary == nullptr && cluster.members.size() > 1) {
      secondary = &cluster.members[1];
    }
    PoseFrame fused = primary->frame;
    fused.timeUs = sumTime / static_cast<double>(cluster.members.size());
    timeline.frames.push_back(fused);
    if (secondary != nullptr) {
      ++timeline.agreement.overlappingClusters;
      ++timeline.fromBoth;
      for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
        timeline.agreement.maxAngularDeltaDeg =
            std::max(timeline.agreement.maxAngularDeltaDeg,
                     quaternionAngleDeg(primary->frame.bones[bone].rotation,
                                        secondary->frame.bones[bone].rotation));
        const Vec3 delta{
            primary->frame.bones[bone].translation.x -
                secondary->frame.bones[bone].translation.x,
            primary->frame.bones[bone].translation.y -
                secondary->frame.bones[bone].translation.y,
            primary->frame.bones[bone].translation.z -
                secondary->frame.bones[bone].translation.z};
        timeline.agreement.maxTranslationDeltaM =
            std::max(timeline.agreement.maxTranslationDeltaM, vecLength(delta));
      }
    } else if (fused.device == 'A') {
      ++timeline.fromDeviceA;
    } else {
      ++timeline.fromDeviceB;
    }
  }

  const double gapThresholdUs = a.frameIntervalUs * 3.5;
  std::size_t segmentStart = 0;
  for (std::size_t index = 1; index < timeline.frames.size(); ++index) {
    const double deltaUs =
        timeline.frames[index].timeUs - timeline.frames[index - 1].timeUs;
    if (deltaUs <= gapThresholdUs) continue;
    MissingSegment gap;
    gap.observedFromUs = timeline.frames[index - 1].timeUs;
    gap.observedToUs = timeline.frames[index].timeUs;
    gap.durationUs = deltaUs;
    gap.missingNominalFrames = static_cast<std::uint32_t>(
        std::max(0.0, std::round(deltaUs / a.frameIntervalUs) - 1.0));
    gap.precedingFrame = static_cast<std::uint32_t>(index - 1);
    gap.followingFrame = static_cast<std::uint32_t>(index);
    timeline.gaps.push_back(gap);
    timeline.segments.push_back({segmentStart, index});
    segmentStart = index;
  }
  if (segmentStart < timeline.frames.size()) {
    timeline.segments.push_back({segmentStart, timeline.frames.size()});
  }
  return timeline;
}

Json gapsToJson(const FusedTimeline& timeline) {
  Json array = Json::array();
  for (const MissingSegment& gap : timeline.gaps) {
    Json entry = Json::object();
    entry["observed_from_us"] = gap.observedFromUs;
    entry["observed_to_us"] = gap.observedToUs;
    entry["duration_us"] = gap.durationUs;
    entry["missing_nominal_frames"] = static_cast<std::uint64_t>(
        gap.missingNominalFrames);
    entry["preceding_fused_frame"] =
        static_cast<std::uint64_t>(gap.precedingFrame);
    entry["following_fused_frame"] =
        static_cast<std::uint64_t>(gap.followingFrame);
    entry["scope"] = "union_of_calibrated_device_coverage";
    entry["policy"] = "hard boundary; compression must not interpolate across";
    array.push(entry);
  }
  return array;
}

std::string isoUtcNow() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
  std::tm utc{};
#if defined(_WIN32)
  gmtime_s(&utc, &timestamp);
#else
  gmtime_r(&timestamp, &utc);
#endif
  std::ostringstream out;
  out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

std::string formatDouble(double value, int precision) {
  std::ostringstream out;
  out << std::fixed << std::setprecision(precision) << value;
  return out.str();
}

}  // namespace kfc
