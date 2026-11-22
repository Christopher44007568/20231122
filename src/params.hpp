#pragma once

#include <array>
#include <string>
#include <vector>

struct DepthParams {
  std::string name;
  float nearZ = 0.05f;
  float farZ = 500.0f;
  float depthBiasConstant = 0.0f;
  float depthBiasSlope = 0.0f;
  bool depthClamp = false;
  float viewportMinDepth = 0.0f;
  float viewportMaxDepth = 1.0f;
};

constexpr int kPresetCount = 4;

const DepthParams& baselineParams();
const std::array<DepthParams, kPresetCount>& extremePresets();
const DepthParams& presetAt(int index);
std::string presetSummary(const DepthParams& params);

struct BiasCandidate {
  float constant = 0.0f;
  float slope = 0.0f;
};

std::vector<BiasCandidate> correctionCandidates(const DepthParams& extreme);

