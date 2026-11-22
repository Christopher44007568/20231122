#include "analyzer.h"

#include <cmath>
#include <cstdio>

const char* policyName(Policy p) {
  return p == Policy::Baseline ? "baseline" : "cached";
}

double RunSummary::meanUnchangedRatio() const {
  if (unchangedRatios.empty()) return -1.0; // n/a: nothing was re-rendered
  double s = 0;
  for (double r : unchangedRatios) s += r;
  return s / (double)unchangedRatios.size();
}

bool Analyzer::init(bool validation,
                    const std::vector<const char*>& extraInstanceExts) {
  if (!ctx_.init(validation, extraInstanceExts)) return false;
  resetScene();
  return true;
}

void Analyzer::resetScene() {
  if (ready_) cache_.shutdown();
  scene_ = buildDemoScene();
  ready_ = cache_.init(ctx_, scene_);
}

void Analyzer::shutdown() {
  if (ready_) cache_.shutdown();
  ready_ = false;
  ctx_.shutdown();
}

CompareResult Analyzer::compareDepth(const std::vector<float>& before,
                                     const std::vector<float>& after) const {
  CompareResult r;
  const float kEps = 1e-6f;
  r.texels = before.size();
  double sum = 0;
  for (size_t i = 0; i < before.size(); ++i) {
    float d = std::fabs(before[i] - after[i]);
    if (d <= kEps)
      r.unchangedTexels++;
    else {
      sum += d;
      if (d > r.maxAbsDiff) r.maxAbsDiff = d;
    }
  }
  r.unchangedRatio =
      r.texels ? (double)r.unchangedTexels / (double)r.texels : 1.0;
  size_t changed = r.texels - r.unchangedTexels;
  r.meanAbsDiff = changed ? sum / (double)changed : 0.0;
  return r;
}

CompareResult Analyzer::refreshAndCompare(size_t li, size_t ci,
                                          uint32_t reason) {
  std::vector<float> before, after;
  bool hadContent = cache_.entry(li, ci).rendered;
  if (hadContent) cache_.readback(li, ci, before);
  cache_.refresh(scene_, li, ci, reason);
  cache_.readback(li, ci, after);
  if (!hadContent) {
    // First render: treat the whole map as new content.
    CompareResult r;
    r.texels = after.size();
    r.unchangedRatio = 0.0;
    return r;
  }
  return compareDepth(before, after);
}

RunSummary Analyzer::runScenario(Scenario scenario, Policy policy,
                                 uint32_t frames) {
  resetScene();
  RunSummary sum;
  sum.scenario = scenario;
  sum.policy = policy;
  sum.frames = frames;

  for (uint32_t frame = 0; frame < frames; ++frame) {
    applyScenario(scenario, scene_, frame);

    for (size_t li = 0; li < cache_.lightCount(); ++li) {
      const std::string& lname = scene_.lights[li].name;
      for (size_t ci = 0; ci < cache_.cascadeCount(li); ++ci) {
        ReasonDetail det = cache_.computeReasons(scene_, li, ci);
        std::string entryKey = lname + "/c" + std::to_string(ci);

        // Record invalidation telemetry regardless of policy.
        for (uint32_t bit : kAllReasonBits) {
          if (!(det.bits & bit)) continue;
          sum.reasonCounts[reasonName(bit)]++;
          sum.reasonCountsPerEntry[entryKey + "/" + reasonName(bit)]++;
        }
        for (int mat : det.changedMaterials) {
          sum.materialInvalidations[entryKey + "/" +
                                    scene_.materials[mat].name]++;
        }

        bool doRefresh = (policy == Policy::Baseline) ||
                         (det.bits != ReasonNone);
        if (!doRefresh) continue;

        bool hadContent = cache_.entry(li, ci).rendered;
        CompareResult cmp = refreshAndCompare(li, ci, ReasonNone);
        const CacheEntry& e = cache_.entry(li, ci);
        sum.totalRefreshes++;
        if (!hadContent) sum.firstRenderRefreshes++;
        sum.refreshesPerEntry[entryKey]++;
        sum.totalCpuMs += e.lastCpuMs;
        sum.totalGpuMs += e.lastGpuMs;
        // First renders have no meaningful "before" image; exclude them
        // from the unchanged-region statistics.
        if (hadContent) sum.unchangedRatios.push_back(cmp.unchangedRatio);
      }
    }
  }
  return sum;
}
