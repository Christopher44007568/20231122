#include "params.hpp"

#include <sstream>

const DepthParams& baselineParams() {
  static const DepthParams params{
    "baseline", 0.05f, 500.0f, 0.0f, 0.0f, false, 0.0f, 1.0f,
  };
  return params;
}

const std::array<DepthParams, kPresetCount>& extremePresets() {
  static const std::array<DepthParams, kPresetCount> presets{{
    {"precision-drift", 0.0001f, 10000.0f, 0.0f, 0.0f, false, 0.0f, 1.0f},
    {"bias-storm", 0.05f, 1000.0f, 3000.0f, 18.0f, false, 0.0f, 1.0f},
    {"clamp-edge", 0.001f, 2000.0f, 1800.0f, 8.0f, true, 0.0001f, 0.9999f},
    {"thin-depth", 0.02f, 80.0f, 0.0f, 0.0f, false, 0.32f, 0.68f},
  }};
  return presets;
}

const DepthParams& presetAt(int index) {
  if (index < 0 || index >= kPresetCount) {
    return extremePresets()[0];
  }
  return extremePresets()[static_cast<std::size_t>(index)];
}

std::string presetSummary(const DepthParams& params) {
  std::ostringstream out;
  out << "near=" << params.nearZ << " far=" << params.farZ
      << " bias=" << params.depthBiasConstant << "/" << params.depthBiasSlope
      << " clamp=" << (params.depthClamp ? "on" : "off")
      << " range=" << params.viewportMinDepth << ".." << params.viewportMaxDepth;
  return out.str();
}

std::vector<BiasCandidate> correctionCandidates(const DepthParams& extreme) {
  const float constant = extreme.depthBiasConstant;
  const float slope = extreme.depthBiasSlope;
  const float constants[] = {
    0.0f, constant * 0.125f, constant * 0.25f, constant * 0.5f,
    constant * 0.75f, constant, -constant * 0.25f, -constant * 0.5f,
  };
  std::vector<BiasCandidate> result;
  result.reserve(8);
  for (float value : constants) {
    result.push_back({value, slope * (constant == 0.0f ? 0.0f : value / constant)});
  }
  return result;
}

