#include "compressor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace kfc {

namespace {

Vec3 lerp(const Vec3& a, const Vec3& b, double t) {
  return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t};
}

std::uint64_t chunkCount(std::size_t segmentSize,
                         std::uint32_t chunkIntervals) {
  if (segmentSize <= 1) return 1;
  return (static_cast<std::uint64_t>(segmentSize - 1) +
          static_cast<std::uint64_t>(chunkIntervals) - 1U) /
         static_cast<std::uint64_t>(chunkIntervals);
}

bool withinTolerance(const IntervalError& error,
                     const CompressionConfig& config) {
  return error.maxAngularDeg <= config.toleranceDeg + 1e-9 &&
         error.maxTranslationM <= config.toleranceM + 1e-9;
}

Json keyframesJson(const std::vector<KeyframeRecord>& keyframes,
                   const std::vector<std::string>& boneNames) {
  Json array = Json::array();
  for (const KeyframeRecord& keyframe : keyframes) {
    array.push(keyframeToJson(keyframe, boneNames));
  }
  return array;
}

}  // namespace

IntervalError CpuIntervalEvaluator::evaluate(
    const std::vector<PoseFrame>& frames, std::size_t startIndex,
    std::size_t endIndex) {
  IntervalError result;
  if (startIndex >= endIndex || endIndex >= frames.size()) return result;
  const PoseFrame& start = frames[startIndex];
  const PoseFrame& end = frames[endIndex];
  const double duration = end.timeUs - start.timeUs;
  if (std::fabs(duration) <= 1e-9) return result;
  for (std::size_t frameIndex = startIndex + 1; frameIndex < endIndex;
       ++frameIndex) {
    const double t =
        (frames[frameIndex].timeUs - start.timeUs) / duration;
    for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
      const Quat interpolatedRotation =
          slerp(start.bones[bone].rotation, end.bones[bone].rotation, t);
      result.maxAngularDeg =
          std::max(result.maxAngularDeg,
                   quaternionAngleDeg(frames[frameIndex].bones[bone].rotation,
                                      interpolatedRotation));
      const Vec3 interpolatedTranslation =
          lerp(start.bones[bone].translation,
               end.bones[bone].translation, t);
      const Vec3 delta{
          frames[frameIndex].bones[bone].translation.x -
              interpolatedTranslation.x,
          frames[frameIndex].bones[bone].translation.y -
              interpolatedTranslation.y,
          frames[frameIndex].bones[bone].translation.z -
              interpolatedTranslation.z};
      result.maxTranslationM =
          std::max(result.maxTranslationM, vecLength(delta));
    }
  }
  return result;
}

const std::string& CpuIntervalEvaluator::name() const {
  static const std::string value = "cpu-reference";
  return value;
}

const std::string& CpuIntervalEvaluator::detail() const {
  static const std::string value =
      "double-precision slerp and translation interpolation; deterministic "
      "fallback path";
  return value;
}

Compressor::Compressor(const CompressionConfig& config,
                       IntervalEvaluator& evaluator)
    : config_(config), evaluator_(evaluator) {}

RunOutcome Compressor::advance(CompressionState& state,
                               const FusedTimeline& timeline,
                               std::uint64_t pauseAfterTotalChunks) {
  RunOutcome outcome;
  while (state.nextSegment < timeline.segments.size()) {
    const auto segment = timeline.segments[state.nextSegment];
    const std::size_t segmentSize = segment.second - segment.first;
    if (segmentSize == 0) {
      ++state.nextSegment;
      state.nextChunk = 0;
      continue;
    }
    const std::uint64_t chunks =
        chunkCount(segmentSize, config_.chunkIntervals);
    if (state.nextChunk >= chunks) {
      ++state.nextSegment;
      state.nextChunk = 0;
      continue;
    }

    const std::size_t begin =
        segment.first + static_cast<std::size_t>(state.nextChunk) *
                            static_cast<std::size_t>(config_.chunkIntervals);
    const std::size_t end =
        std::min(begin + static_cast<std::size_t>(config_.chunkIntervals),
                 segment.second - 1);

    if (state.selectedIndices.empty() ||
        state.selectedIndices.back() != static_cast<std::uint32_t>(begin)) {
      state.selectedIndices.push_back(static_cast<std::uint32_t>(begin));
    }
    if (end <= begin) {
      if (state.selectedIndices.back() != static_cast<std::uint32_t>(end)) {
        state.selectedIndices.push_back(static_cast<std::uint32_t>(end));
      }
    } else {
      std::size_t anchor = begin;
      while (anchor < end) {
        std::size_t best = anchor;
        for (std::size_t candidate = anchor + 1; candidate <= end; ++candidate) {
          const IntervalError error = evaluator_.evaluate(
              timeline.frames, anchor, candidate);
          ++state.workUnits;
          state.accumulatedMaxAngularDeg =
              std::max(state.accumulatedMaxAngularDeg, error.maxAngularDeg);
          state.accumulatedMaxTranslationM = std::max(
              state.accumulatedMaxTranslationM, error.maxTranslationM);
          if (withinTolerance(error, config_)) best = candidate;
        }
        if (best == anchor) {
          best = anchor + 1;
          ++state.forcedSteps;
        }
        state.selectedIndices.push_back(static_cast<std::uint32_t>(best));
        anchor = best;
      }
    }

    ++state.chunksCompleted;
    ++state.nextChunk;
    if (state.nextChunk >= chunks) {
      ++state.nextSegment;
      state.nextChunk = 0;
    }

    const bool allDone = state.nextSegment >= timeline.segments.size();
    if (!allDone && pauseAfterTotalChunks > 0 &&
        state.chunksCompleted >= pauseAfterTotalChunks) {
      outcome.state = state;
      outcome.paused = true;
      outcome.complete = false;
      return outcome;
    }
  }
  outcome.state = state;
  outcome.paused = false;
  outcome.complete = true;
  return outcome;
}

CompressionResult Compressor::finish(const CompressionState& state,
                                     const FusedTimeline& timeline) {
  CompressionResult result;
  result.state = state;
  result.sourceFrameCount =
      static_cast<std::uint32_t>(timeline.frames.size());
  result.segmentCount =
      static_cast<std::uint32_t>(timeline.segments.size());
  result.gapCount = static_cast<std::uint32_t>(timeline.gaps.size());
  result.complete = state.nextSegment >= timeline.segments.size();
  for (const std::uint32_t index : state.selectedIndices) {
    if (index >= timeline.frames.size()) {
      throw std::runtime_error("checkpoint selected index is out of range");
    }
    KeyframeRecord keyframe;
    keyframe.sourceIndex = index;
    keyframe.timeUs = timeline.frames[index].timeUs;
    keyframe.bones = timeline.frames[index].bones;
    result.keyframes.push_back(keyframe);
  }
  result.keyframeCount =
      static_cast<std::uint32_t>(result.keyframes.size());

  double angularSum = 0.0;
  double translationSum = 0.0;
  std::uint64_t sampleWeight = 0;
  for (const auto& segment : timeline.segments) {
    std::vector<std::uint32_t> indices;
    for (const std::uint32_t index : state.selectedIndices) {
      if (index >= segment.first && index < segment.second) {
        indices.push_back(index);
      }
    }
    for (std::size_t i = 0; i + 1 < indices.size(); ++i) {
      const IntervalError error =
          evaluator_.evaluate(timeline.frames, indices[i], indices[i + 1]);
      result.intervals.push_back(error);
      result.maxAngularErrorDeg =
          std::max(result.maxAngularErrorDeg, error.maxAngularDeg);
      result.maxTranslationErrorM =
          std::max(result.maxTranslationErrorM, error.maxTranslationM);
      const std::uint64_t weight = indices[i + 1] - indices[i];
      angularSum += error.maxAngularDeg * static_cast<double>(weight);
      translationSum += error.maxTranslationM * static_cast<double>(weight);
      sampleWeight += weight;
    }
  }
  if (sampleWeight != 0) {
    result.meanAngularErrorDeg =
        angularSum / static_cast<double>(sampleWeight);
    result.meanTranslationErrorM =
        translationSum / static_cast<double>(sampleWeight);
  }
  return result;
}

Json compressionConfigToJson(const CompressionConfig& config) {
  Json root = Json::object();
  root["resource_mode"] = config.resourceMode;
  root["angular_tolerance_deg"] = config.toleranceDeg;
  root["translation_tolerance_m"] = config.toleranceM;
  root["chunk_intervals"] = static_cast<std::uint64_t>(config.chunkIntervals);
  root["selection"] =
      "per-chunk greedy furthest feasible endpoint; gap boundaries are hard";
  root["interpolation"] =
      "shortest-arc quaternion slerp; linear translation; timestamps in us";
  return root;
}

Json compressionStateToJson(const CompressionState& state) {
  Json root = Json::object();
  root["schema_version"] = static_cast<std::uint64_t>(state.schemaVersion);
  root["next_segment"] = static_cast<std::uint64_t>(state.nextSegment);
  root["next_chunk"] = static_cast<std::uint64_t>(state.nextChunk);
  root["chunks_completed"] = state.chunksCompleted;
  root["work_units"] = state.workUnits;
  root["forced_steps"] = state.forcedSteps;
  root["accumulated_max_angular_deg"] = state.accumulatedMaxAngularDeg;
  root["accumulated_max_translation_m"] = state.accumulatedMaxTranslationM;
  Json selected = Json::array();
  for (const std::uint32_t index : state.selectedIndices) {
    selected.push(static_cast<std::uint64_t>(index));
  }
  root["selected_indices"] = selected;
  return root;
}

CompressionState compressionStateFromJson(const Json& value) {
  CompressionState state;
  state.schemaVersion = static_cast<std::uint32_t>(
      value.at("schema_version").asInt());
  state.nextSegment =
      static_cast<std::size_t>(value.at("next_segment").asInt());
  state.nextChunk = static_cast<std::size_t>(value.at("next_chunk").asInt());
  state.chunksCompleted =
      static_cast<std::uint64_t>(value.at("chunks_completed").asInt());
  state.workUnits = static_cast<std::uint64_t>(value.at("work_units").asInt());
  state.forcedSteps =
      static_cast<std::uint64_t>(value.at("forced_steps").asInt());
  state.accumulatedMaxAngularDeg =
      value.at("accumulated_max_angular_deg").asDouble();
  state.accumulatedMaxTranslationM =
      value.at("accumulated_max_translation_m").asDouble();
  for (const Json& index : value.at("selected_indices").asArray()) {
    state.selectedIndices.push_back(
        static_cast<std::uint32_t>(index.asInt()));
  }
  return state;
}

Json keyframeToJson(const KeyframeRecord& keyframe,
                    const std::vector<std::string>& boneNames) {
  Json root = Json::object();
  root["source_index"] = static_cast<std::uint64_t>(keyframe.sourceIndex);
  root["time_us"] = keyframe.timeUs;
  Json order = Json::array();
  for (const std::string& name : boneNames) order.push(name);
  root["bone_order"] = order;
  root["bones"] = bonesToJson(keyframe.bones);
  return root;
}

Json compressionResultToJson(const CompressionResult& result,
                             const std::vector<std::string>& boneNames) {
  Json root = Json::object();
  root["schema"] = "kfc.compressed-clip/v1";
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
  metrics["evaluated_intervals"] =
      static_cast<std::uint64_t>(result.intervals.size());
  root["metrics"] = metrics;
  root["work"] = Json::object();
  root["work"]["chunks_completed"] = result.state.chunksCompleted;
  root["work"]["interval_evaluations"] = result.state.workUnits;
  root["work"]["forced_steps"] = result.state.forcedSteps;
  root["keyframes"] = keyframesJson(result.keyframes, boneNames);
  root["state"] = compressionStateToJson(result.state);
  return root;
}

}  // namespace kfc
