// Shadow cache analyzer — CLI entry point.
//
// Records per-light / per-cascade / per-material invalidation reasons,
// refreshes individual cache levels, and compares depth maps before/after
// a refresh. Validated with three scenarios (static light, moving light,
// localized material change) under two refresh policies.
#include "analyzer.h"
#include "report.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>

static void usage(const char* argv0) {
  std::printf(
      "usage: %s [options]\n"
      "  --scenario <static|moving-light|material-change|all> (default all)\n"
      "  --policy <baseline|cached|both>                      (default both)\n"
      "  --frames N        frames per run                     (default 60)\n"
      "  --output DIR      report output directory     (default artifacts)\n"
      "  --interactive     open the GLFW cache viewer instead of batch\n"
      "  --validate        run all scenarios x both policies (default)\n",
      argv0);
}

int runInteractive();
int runBlitSelftest();

int main(int argc, char** argv) {
  std::string scenarioArg = "all";
  std::string policyArg = "both";
  std::string outDir = "artifacts";
  uint32_t frames = 60;
  bool interactive = false;

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", name);
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--scenario") scenarioArg = next("--scenario");
    else if (a == "--policy") policyArg = next("--policy");
    else if (a == "--frames") frames = (uint32_t)std::stoul(next("--frames"));
    else if (a == "--output") outDir = next("--output");
    else if (a == "--interactive") interactive = true;
    else if (a == "--selftest-blit") return runBlitSelftest();
    else if (a == "--validate") { /* default behavior */ }
    else if (a == "--help" || a == "-h") {
      usage(argv[0]);
      return 0;
    } else {
      std::fprintf(stderr, "unknown option: %s\n", a.c_str());
      usage(argv[0]);
      return 2;
    }
  }

  if (interactive) return runInteractive();

  std::vector<Scenario> scenarios;
  if (scenarioArg == "all" || scenarioArg == "static")
    scenarios.push_back(Scenario::Static);
  if (scenarioArg == "all" || scenarioArg == "moving-light")
    scenarios.push_back(Scenario::MovingLight);
  if (scenarioArg == "all" || scenarioArg == "material-change")
    scenarios.push_back(Scenario::MaterialChange);
  if (scenarios.empty()) {
    std::fprintf(stderr, "unknown scenario: %s\n", scenarioArg.c_str());
    return 2;
  }
  std::vector<Policy> policies;
  if (policyArg == "both" || policyArg == "baseline")
    policies.push_back(Policy::Baseline);
  if (policyArg == "both" || policyArg == "cached")
    policies.push_back(Policy::Cached);
  if (policies.empty()) {
    std::fprintf(stderr, "unknown policy: %s\n", policyArg.c_str());
    return 2;
  }

  mkdir(outDir.c_str(), 0755);

  Analyzer analyzer;
  if (!analyzer.init(false)) {
    std::fprintf(stderr, "failed to initialize Vulkan\n");
    return 1;
  }
  std::printf("device: %s\n", analyzer.context().deviceName().c_str());

  std::vector<RunSummary> runs;
  for (Scenario sc : scenarios)
    for (Policy pol : policies) {
      RunSummary s = analyzer.runScenario(sc, pol, frames);
      printSummary(s);
      std::string path = outDir + "/report_" + scenarioName(sc) + "_" +
                         policyName(pol) + ".json";
      if (!writeJsonReport(s, path))
        std::fprintf(stderr, "failed to write %s\n", path.c_str());
      runs.push_back(s);
    }

  std::string csvPath = outDir + "/summary.csv";
  if (writeCsvSummary(runs, csvPath))
    std::printf("\nwrote %s\n", csvPath.c_str());

  analyzer.shutdown();
  return 0;
}
