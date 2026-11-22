#pragma once

#include "scene.h"
#include "shadow_cache.h"
#include "vk_context.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

enum class Policy {
  Baseline, // re-render every level every frame (the wasteful behavior)
  Cached,   // re-render only invalidated levels
};
const char* policyName(Policy p);

struct CompareResult {
  size_t texels = 0;
  size_t unchangedTexels = 0;
  double unchangedRatio = 1.0;
  double meanAbsDiff = 0.0;
  double maxAbsDiff = 0.0;
};

struct RunSummary {
  Scenario scenario = Scenario::Static;
  Policy policy = Policy::Cached;
  uint32_t frames = 0;
  uint64_t totalRefreshes = 0;
  uint64_t firstRenderRefreshes = 0; // excluded from unchanged-ratio stats
  double totalCpuMs = 0.0;
  double totalGpuMs = 0.0;
  std::map<std::string, uint64_t> refreshesPerEntry;   // "<light>/c<i>"
  std::map<std::string, uint64_t> reasonCounts;        // per reason bit
  std::map<std::string, uint64_t> reasonCountsPerEntry;
  std::map<std::string, uint64_t> materialInvalidations; // "<l>/c<i>/<mat>"
  std::vector<double> unchangedRatios;                 // one per refresh
  double meanUnchangedRatio() const;
};

// Drives scenario frames over the shadow cache and records why each cache
// level was invalidated, how often it refreshed, how long it took, and how
// much of the depth map actually changed.
class Analyzer {
public:
  bool init(bool validation,
            const std::vector<const char*>& extraInstanceExts = {});
  void shutdown();

  // Runs a scenario under a refresh policy and returns the collected stats.
  RunSummary runScenario(Scenario scenario, Policy policy, uint32_t frames);

  // Single-entry refresh with before/after depth comparison (also used by
  // the interactive viewer's "refresh one cache" action).
  CompareResult refreshAndCompare(size_t light, size_t cascade,
                                  uint32_t reason);

  // Accessors for the interactive viewer.
  Scene& scene() { return scene_; }
  ShadowCacheSystem& cache() { return cache_; }
  VkContext& context() { return ctx_; }
  void resetScene();

private:
  VkContext ctx_;
  Scene scene_;
  ShadowCacheSystem cache_;
  bool ready_ = false;

  CompareResult compareDepth(const std::vector<float>& before,
                             const std::vector<float>& after) const;
};
