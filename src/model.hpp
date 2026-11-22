#pragma once

#include "json.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kfc {

constexpr std::size_t kBoneCount = 8;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Quat {
  double w = 1.0;
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct BonePose {
  Quat rotation;
  Vec3 translation;
};

struct PoseFrame {
  double timeUs = 0.0;
  std::array<BonePose, kBoneCount> bones{};
  char device = 'A';
  std::uint32_t sequence = 0;
};

struct SyncAnchor {
  std::string id;
  double referenceUs = 0.0;
  double deviceUs = 0.0;
};

struct CaptureWindow {
  double startUs = 0.0;
  double endUs = 0.0;
};

struct DeviceCapture {
  std::string schema;
  std::string deviceId;
  std::string localTimebase;
  double clockOffsetUs = 0.0;
  double clockDriftPpm = 0.0;
  std::string animationId;
  double frameIntervalUs = 0.0;
  double durationUs = 0.0;
  std::vector<std::string> bones;
  std::vector<std::uint32_t> frameSequences;
  std::vector<PoseFrame> samples;
  std::vector<SyncAnchor> anchors;
  std::vector<CaptureWindow> windows;
};

struct Scenario {
  std::string schema;
  std::string scenarioId;
  std::string description;
  std::string animationId;
  double frameIntervalUs = 0.0;
  double durationUs = 0.0;
  std::vector<std::string> bones;
  std::string deviceAFile;
  std::string deviceBFile;
  double intendedGapStartUs = 0.0;
  double intendedGapEndUs = 0.0;
};

struct ClockFit {
  std::size_t anchorCount = 0;
  double slope = 1.0;
  double interceptUs = 0.0;
  double rmsResidualUs = 0.0;
  double maxResidualUs = 0.0;
  double offsetAtReferenceZeroUs = 0.0;
  double driftPpmEstimate = 0.0;
};

struct ClockCalibration {
  ClockFit deviceA;
  ClockFit deviceB;
  std::vector<std::string> anchorIds;
};

struct PoseAgreement {
  double maxAngularDeltaDeg = 0.0;
  double maxTranslationDeltaM = 0.0;
  std::size_t overlappingClusters = 0;
};

struct MissingSegment {
  double observedFromUs = 0.0;
  double observedToUs = 0.0;
  double durationUs = 0.0;
  std::uint32_t missingNominalFrames = 0;
  std::uint32_t precedingFrame = 0;
  std::uint32_t followingFrame = 0;
};

struct FusedTimeline {
  std::vector<PoseFrame> frames;
  std::vector<MissingSegment> gaps;
  std::vector<std::pair<std::size_t, std::size_t>> segments;
  PoseAgreement agreement;
  std::size_t fromDeviceA = 0;
  std::size_t fromDeviceB = 0;
  std::size_t fromBoth = 0;
};

Quat normalize(Quat value);
Quat multiply(Quat a, Quat b);
double dot(Quat a, Quat b);
Quat slerp(Quat a, Quat b, double t);
double quaternionAngleDeg(Quat a, Quat b);
double vecLength(const Vec3& value);

std::vector<std::string> boneNames();
PoseFrame analyticPose(double timeUs, char device = 'A');
void generateScenario(const std::string& inputDir, std::uint32_t seed = 20231122U);

Scenario loadScenario(const std::string& path);
DeviceCapture loadCapture(const std::string& path);
ClockFit calibrateClock(const DeviceCapture& capture);
ClockCalibration calibrateClocks(const DeviceCapture& a, const DeviceCapture& b);
double mapDeviceTime(const ClockFit& fit, double deviceUs);
FusedTimeline fuseCaptures(const DeviceCapture& a, const DeviceCapture& b,
                           const ClockCalibration& calibration);

Json captureToJson(const DeviceCapture& capture);
Json scenarioToJson(const Scenario& scenario);
Json calibrationToJson(const ClockCalibration& calibration);
Json gapsToJson(const FusedTimeline& timeline);
Json poseToJson(const PoseFrame& frame);
Json bonesToJson(const std::array<BonePose, kBoneCount>& bones);

std::string isoUtcNow();
std::string formatDouble(double value, int precision = 6);

}  // namespace kfc
