#include "compare.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace {

void includePoint(std::optional<RectI>& bounds, int x, int y) {
  if (!bounds) {
    bounds = RectI{x, y, 1, 1};
    return;
  }
  const int right = std::max(bounds->right(), x + 1);
  const int bottom = std::max(bounds->bottom(), y + 1);
  bounds->x = std::min(bounds->x, x);
  bounds->y = std::min(bounds->y, y);
  bounds->width = right - bounds->x;
  bounds->height = bottom - bounds->y;
}

BoundarySample makeBoundarySample(const FrameData& baseline, const FrameData& current,
                                  std::string label, int x, int y) {
  BoundarySample sample;
  sample.label = std::move(label);
  sample.point = {x, y};
  sample.before = samplePixel(baseline, x, y);
  sample.after = samplePixel(current, x, y);
  if (sample.before.valid && sample.after.valid) {
    sample.depthDelta = sample.after.depth - sample.before.depth;
  }
  sample.objectChanged = sample.before.objectId != sample.after.objectId;
  sample.colorChanged = sample.before.r != sample.after.r ||
                        sample.before.g != sample.after.g ||
                        sample.before.b != sample.after.b;
  return sample;
}

}  // namespace

PixelSample samplePixel(const FrameData& frame, int x, int y) {
  PixelSample sample;
  if (x < 0 || y < 0 || x >= frame.width || y >= frame.height) {
    return sample;
  }
  const std::size_t index = static_cast<std::size_t>(y) * frame.width + x;
  if (index >= frame.aux.size()) {
    return sample;
  }
  sample.valid = true;
  sample.objectId = static_cast<std::uint32_t>(frame.aux[index].objectId + 0.5f);
  sample.depth = frame.aux[index].depth;
  if (index * 4 + 3 < frame.rgba.size()) {
    sample.r = frame.rgba[index * 4];
    sample.g = frame.rgba[index * 4 + 1];
    sample.b = frame.rgba[index * 4 + 2];
  }
  return sample;
}

CompareResult compareFrames(const FrameData& baseline, const FrameData& current,
                            std::uint32_t focusId) {
  CompareResult result;
  result.focusId = focusId;
  if (baseline.empty() || current.empty() ||
      baseline.width != current.width || baseline.height != current.height) {
    result.classification = "frame-unavailable";
    return result;
  }

  std::optional<RectI> missingBounds;
  std::optional<RectI> addedBounds;
  double depthDeltaSum = 0.0;
  int depthDeltaCount = 0;
  const std::size_t pixelCount = static_cast<std::size_t>(baseline.width) * baseline.height;
  for (std::size_t index = 0; index < pixelCount; ++index) {
    const auto baselineId = static_cast<std::uint32_t>(baseline.aux[index].objectId + 0.5f);
    const auto currentId = static_cast<std::uint32_t>(current.aux[index].objectId + 0.5f);
    const int x = static_cast<int>(index % static_cast<std::size_t>(baseline.width));
    const int y = static_cast<int>(index / static_cast<std::size_t>(baseline.width));
    const bool expected = baselineId == focusId;
    const bool rerun = currentId == focusId;
    if (expected) {
      ++result.focusPixels;
      includePoint(result.expectedBounds, x, y);
    }
    if (baselineId != currentId) {
      ++result.changedPixels;
      includePoint(result.changeBounds, x, y);
    }
    if (expected && !rerun) {
      ++result.missingPixels;
      includePoint(missingBounds, x, y);
    }
    if (!expected && rerun) {
      ++result.addedPixels;
      includePoint(addedBounds, x, y);
    }
    if (expected && rerun) {
      depthDeltaSum += std::abs(static_cast<double>(current.aux[index].depth -
                                                   baseline.aux[index].depth));
      ++depthDeltaCount;
    }
  }
  if (depthDeltaCount > 0) {
    result.meanAbsDepthDelta =
      static_cast<float>(depthDeltaSum / static_cast<double>(depthDeltaCount));
  }

  if (result.missingPixels > 0) {
    result.classification = "fragment-gap";
  } else if (result.addedPixels > 0) {
    result.classification = "depth-bleed";
  } else if (result.focusPixels == 0) {
    result.classification = "not-visible";
  } else {
    result.classification = "stable";
  }

  std::optional<RectI> boundaryBounds = missingBounds ? missingBounds : result.expectedBounds;
  if (!boundaryBounds && addedBounds) {
    boundaryBounds = addedBounds;
  }
  if (boundaryBounds) {
    RectI bounds = *boundaryBounds;
    const int y = std::max(0, std::min(current.height - 1,
                                       bounds.y + bounds.height / 2));
    const int left = std::max(0, std::min(current.width - 1, bounds.x));
    const int right = std::max(0, std::min(current.width - 1, bounds.right() - 1));
    result.boundary.push_back(makeBoundarySample(
      baseline, current, "left-outside", left - 1, y));
    result.boundary.push_back(makeBoundarySample(
      baseline, current, "left-inside", left, y));
    result.boundary.push_back(makeBoundarySample(
      baseline, current, "right-inside", right, y));
    result.boundary.push_back(makeBoundarySample(
      baseline, current, "right-outside", right + 1, y));
  }
  return result;
}

FrameData buildDiffFrame(const FrameData& baseline, const FrameData& current,
                         std::uint32_t focusId) {
  FrameData diff;
  diff.width = baseline.width;
  diff.height = baseline.height;
  diff.rgba.assign(static_cast<std::size_t>(diff.width) * diff.height * 4, 0);
  diff.aux.assign(static_cast<std::size_t>(diff.width) * diff.height, {});
  if (diff.width <= 0 || diff.height <= 0) {
    return diff;
  }
  const std::size_t pixelCount = static_cast<std::size_t>(diff.width) * diff.height;
  for (std::size_t index = 0; index < pixelCount; ++index) {
    const auto baselineId = static_cast<std::uint32_t>(baseline.aux[index].objectId + 0.5f);
    const auto currentId = static_cast<std::uint32_t>(current.aux[index].objectId + 0.5f);
    std::uint8_t r = 24;
    std::uint8_t g = 27;
    std::uint8_t b = 32;
    if (baselineId == focusId && currentId != focusId) {
      r = 244; g = 62; b = 72;
    } else if (baselineId != focusId && currentId == focusId) {
      r = 42; g = 214; b = 140;
    } else if (baselineId == focusId && currentId == focusId) {
      r = 52; g = 86; b = 112;
    }
    diff.rgba[index * 4] = r;
    diff.rgba[index * 4 + 1] = g;
    diff.rgba[index * 4 + 2] = b;
    diff.rgba[index * 4 + 3] = 255;
    diff.aux[index] = current.aux[index];
  }
  return diff;
}

FrameData buildSideBySideFrame(const FrameData& baseline,
                               const FrameData& current, float split) {
  FrameData result;
  result.width = baseline.width;
  result.height = baseline.height;
  const std::size_t pixelCount =
    static_cast<std::size_t>(result.width) * result.height;
  result.rgba.assign(pixelCount * 4, 0);
  result.aux.assign(pixelCount, {});
  if (result.width <= 0 || result.height <= 0 ||
      current.width != baseline.width || current.height != baseline.height ||
      baseline.rgba.size() < pixelCount * 4 ||
      current.rgba.size() < pixelCount * 4) {
    return result;
  }
  const int splitX = std::max(0, std::min(
    result.width - 1, static_cast<int>(split * static_cast<float>(result.width))));
  for (int y = 0; y < result.height; ++y) {
    for (int x = 0; x < result.width; ++x) {
      const std::size_t index =
        static_cast<std::size_t>(y) * result.width + static_cast<std::size_t>(x);
      const FrameData& source = x < splitX ? baseline : current;
      result.rgba[index * 4] = source.rgba[index * 4];
      result.rgba[index * 4 + 1] = source.rgba[index * 4 + 1];
      result.rgba[index * 4 + 2] = source.rgba[index * 4 + 2];
      result.rgba[index * 4 + 3] = 255;
      result.aux[index] = source.aux[index];
      if (x >= splitX - 1 && x <= splitX + 1) {
        result.rgba[index * 4] = 5;
        result.rgba[index * 4 + 1] = 219;
        result.rgba[index * 4 + 2] = 202;
      }
    }
  }
  return result;
}

double candidateScore(const CompareResult& result) {
  return static_cast<double>(result.missingPixels) * 4.0 +
         static_cast<double>(result.addedPixels) * 2.0 +
         static_cast<double>(result.changedPixels) * 0.25;
}

