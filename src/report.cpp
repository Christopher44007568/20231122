#include "report.h"

#include <cstdio>

void printSummary(const RunSummary& s) {
  std::printf("\n=== scenario=%s policy=%s frames=%u ===\n",
              scenarioName(s.scenario), policyName(s.policy), s.frames);
  std::printf("total refreshes:      %llu\n",
              (unsigned long long)s.totalRefreshes);
  std::printf("first-render fills:   %llu\n",
              (unsigned long long)s.firstRenderRefreshes);
  std::printf("refresh time (CPU):   %.2f ms total, %.3f ms avg\n", s.totalCpuMs,
              s.totalRefreshes ? s.totalCpuMs / s.totalRefreshes : 0.0);
  std::printf("refresh time (GPU):   %.2f ms total, %.3f ms avg\n", s.totalGpuMs,
              s.totalRefreshes ? s.totalGpuMs / s.totalRefreshes : 0.0);
  if (!s.unchangedRatios.empty())
    std::printf("mean unchanged ratio: %.4f (over %zu re-renders)\n",
                s.meanUnchangedRatio(), s.unchangedRatios.size());
  else
    std::printf("mean unchanged ratio: n/a (no re-renders)\n");

  if (!s.refreshesPerEntry.empty()) {
    std::printf("refreshes per cache level:\n");
    for (const auto& kv : s.refreshesPerEntry)
      std::printf("  %-12s %llu\n", kv.first.c_str(),
                  (unsigned long long)kv.second);
  }
  if (!s.reasonCounts.empty()) {
    std::printf("invalidation reasons (frame x level events):\n");
    for (const auto& kv : s.reasonCounts)
      std::printf("  %-18s %llu\n", kv.first.c_str(),
                  (unsigned long long)kv.second);
  }
  if (!s.reasonCountsPerEntry.empty()) {
    std::printf("invalidation reasons per cache level:\n");
    for (const auto& kv : s.reasonCountsPerEntry)
      std::printf("  %-28s %llu\n", kv.first.c_str(),
                  (unsigned long long)kv.second);
  }
  if (!s.materialInvalidations.empty()) {
    std::printf("material-driven invalidations:\n");
    for (const auto& kv : s.materialInvalidations)
      std::printf("  %-24s %llu\n", kv.first.c_str(),
                  (unsigned long long)kv.second);
  }
}

static void jsonMap(FILE* f, const std::map<std::string, uint64_t>& m,
                    const char* indent) {
  bool first = true;
  for (const auto& kv : m) {
    if (!first) std::fprintf(f, ",\n");
    std::fprintf(f, "%s\"%s\": %llu", indent, kv.first.c_str(),
                 (unsigned long long)kv.second);
    first = false;
  }
  std::fprintf(f, "\n");
}

bool writeJsonReport(const RunSummary& s, const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  std::fprintf(f, "{\n");
  std::fprintf(f, "  \"scenario\": \"%s\",\n", scenarioName(s.scenario));
  std::fprintf(f, "  \"policy\": \"%s\",\n", policyName(s.policy));
  std::fprintf(f, "  \"frames\": %u,\n", s.frames);
  std::fprintf(f, "  \"total_refreshes\": %llu,\n",
               (unsigned long long)s.totalRefreshes);
  std::fprintf(f, "  \"first_render_refreshes\": %llu,\n",
               (unsigned long long)s.firstRenderRefreshes);
  std::fprintf(f, "  \"total_cpu_ms\": %.4f,\n", s.totalCpuMs);
  std::fprintf(f, "  \"total_gpu_ms\": %.4f,\n", s.totalGpuMs);
  std::fprintf(f, "  \"avg_cpu_ms_per_refresh\": %.4f,\n",
               s.totalRefreshes ? s.totalCpuMs / s.totalRefreshes : 0.0);
  std::fprintf(f, "  \"avg_gpu_ms_per_refresh\": %.4f,\n",
               s.totalRefreshes ? s.totalGpuMs / s.totalRefreshes : 0.0);
  std::fprintf(f, "  \"mean_unchanged_ratio\": %.6f,\n",
               s.meanUnchangedRatio());
  std::fprintf(f, "  \"refreshes_per_entry\": {\n");
  jsonMap(f, s.refreshesPerEntry, "    ");
  std::fprintf(f, "  },\n");
  std::fprintf(f, "  \"invalidation_reasons\": {\n");
  jsonMap(f, s.reasonCounts, "    ");
  std::fprintf(f, "  },\n");
  std::fprintf(f, "  \"invalidation_reasons_per_entry\": {\n");
  jsonMap(f, s.reasonCountsPerEntry, "    ");
  std::fprintf(f, "  },\n");
  std::fprintf(f, "  \"material_invalidations\": {\n");
  jsonMap(f, s.materialInvalidations, "    ");
  std::fprintf(f, "  }\n");
  std::fprintf(f, "}\n");
  std::fclose(f);
  return true;
}

bool writeCsvSummary(const std::vector<RunSummary>& runs,
                     const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "w");
  if (!f) return false;
  std::fprintf(f, "scenario,policy,frames,total_refreshes,total_cpu_ms,"
                  "total_gpu_ms,avg_cpu_ms_per_refresh,"
                  "avg_gpu_ms_per_refresh,mean_unchanged_ratio\n");
  for (const auto& s : runs) {
    std::fprintf(f, "%s,%s,%u,%llu,%.4f,%.4f,%.4f,%.4f,",
                 scenarioName(s.scenario), policyName(s.policy), s.frames,
                 (unsigned long long)s.totalRefreshes, s.totalCpuMs,
                 s.totalGpuMs,
                 s.totalRefreshes ? s.totalCpuMs / s.totalRefreshes : 0.0,
                 s.totalRefreshes ? s.totalGpuMs / s.totalRefreshes : 0.0);
    if (s.unchangedRatios.empty())
      std::fprintf(f, "n/a\n");
    else
      std::fprintf(f, "%.6f\n", s.meanUnchangedRatio());
  }
  std::fclose(f);
  return true;
}
