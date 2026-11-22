#pragma once

#include "math.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct PixelAux {
  float depth = 0.0f;
  float objectId = 0.0f;
};

struct FrameData {
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba;
  std::vector<PixelAux> aux;

  bool empty() const { return width <= 0 || height <= 0 || aux.empty(); }
};

struct PixelSample {
  bool valid = false;
  std::uint32_t objectId = 0;
  float depth = 0.0f;
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

struct BoundarySample {
  std::string label;
  ScreenPoint point;
  PixelSample before;
  PixelSample after;
  float depthDelta = 0.0f;
  bool objectChanged = false;
  bool colorChanged = false;
};

struct CompareResult {
  std::uint32_t focusId = 0;
  int focusPixels = 0;
  int missingPixels = 0;
  int addedPixels = 0;
  int changedPixels = 0;
  float meanAbsDepthDelta = 0.0f;
  std::optional<RectI> expectedBounds;
  std::optional<RectI> changeBounds;
  std::vector<BoundarySample> boundary;
  std::string classification;
};

PixelSample samplePixel(const FrameData& frame, int x, int y);
CompareResult compareFrames(const FrameData& baseline, const FrameData& current,
                            std::uint32_t focusId);
FrameData buildDiffFrame(const FrameData& baseline, const FrameData& current,
                         std::uint32_t focusId);
FrameData buildSideBySideFrame(const FrameData& baseline,
                               const FrameData& current, float split = 0.5f);
double candidateScore(const CompareResult& result);

