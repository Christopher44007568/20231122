#pragma once

#include "model.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kfc {

struct IntervalError {
  double maxAngularDeg = 0.0;
  double maxTranslationM = 0.0;
};

class IntervalEvaluator {
public:
  virtual ~IntervalEvaluator() = default;
  virtual IntervalError evaluate(const std::vector<PoseFrame>& frames,
                                 std::size_t startIndex,
                                 std::size_t endIndex) = 0;
  virtual const std::string& name() const = 0;
  virtual const std::string& detail() const = 0;
};

class CpuIntervalEvaluator final : public IntervalEvaluator {
public:
  IntervalError evaluate(const std::vector<PoseFrame>& frames,
                         std::size_t startIndex,
                         std::size_t endIndex) override;
  const std::string& name() const override;
  const std::string& detail() const override;
};

struct CompressionConfig {
  double toleranceDeg = 1.25;
  double toleranceM = 0.012;
  std::uint32_t chunkIntervals = 8;
  std::string resourceMode = "full";
};

struct CompressionState {
  std::uint32_t schemaVersion = 1;
  std::size_t nextSegment = 0;
  std::size_t nextChunk = 0;
  std::uint64_t chunksCompleted = 0;
  std::uint64_t workUnits = 0;
  std::uint64_t forcedSteps = 0;
  std::vector<std::uint32_t> selectedIndices;
  double accumulatedMaxAngularDeg = 0.0;
  double accumulatedMaxTranslationM = 0.0;
};

struct KeyframeRecord {
  std::uint32_t sourceIndex = 0;
  double timeUs = 0.0;
  std::array<BonePose, kBoneCount> bones{};
};

struct CompressionResult {
  CompressionState state;
  std::vector<KeyframeRecord> keyframes;
  std::vector<IntervalError> intervals;
  std::uint32_t sourceFrameCount = 0;
  std::uint32_t keyframeCount = 0;
  std::uint32_t segmentCount = 0;
  std::uint32_t gapCount = 0;
  double maxAngularErrorDeg = 0.0;
  double maxTranslationErrorM = 0.0;
  double meanAngularErrorDeg = 0.0;
  double meanTranslationErrorM = 0.0;
  bool complete = false;
};

struct RunOutcome {
  CompressionState state;
  bool paused = false;
  bool complete = false;
};

class Compressor {
public:
  Compressor(const CompressionConfig& config, IntervalEvaluator& evaluator);

  RunOutcome advance(CompressionState& state,
                     const FusedTimeline& timeline,
                     std::uint64_t pauseAfterTotalChunks = 0);

  CompressionResult finish(const CompressionState& state,
                           const FusedTimeline& timeline);

private:
  CompressionConfig config_;
  IntervalEvaluator& evaluator_;
};

Json compressionConfigToJson(const CompressionConfig& config);
Json compressionStateToJson(const CompressionState& state);
CompressionState compressionStateFromJson(const Json& value);
Json keyframeToJson(const KeyframeRecord& keyframe,
                    const std::vector<std::string>& boneNames);
Json compressionResultToJson(const CompressionResult& result,
                             const std::vector<std::string>& boneNames);

}  // namespace kfc
