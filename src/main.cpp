#include "experiment.h"
#include "present.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static void printUsage(const char* argv0) {
  std::printf(
    "csm_lab - 阴影级联可视化可回放实验 (C++17 / Vulkan / GLFW)\n"
    "usage:\n"
    "  %s run [--artifacts DIR] [--seed N] [--width W] [--height H]\n"
    "      完整实验: 记录 -> 快照 -> 恢复 -> 重放 -> 乱序 -> 重复提交\n"
    "      输出 trace.jsonl / snapshot.state / hashes.json / *.png / report.md\n"
    "  %s present [--seed N] [--timeout S]\n"
    "      GLFW 开窗展示级联可视化结果\n"
    "  %s help\n",
    argv0, argv0, argv0);
}

int main(int argc, char** argv) {
  if (argc < 2 || std::strcmp(argv[1], "help") == 0) {
    printUsage(argv[0]);
    return argc < 2 ? 1 : 0;
  }

  ExperimentOptions opts;
  opts.artifactsDir = ARTIFACTS_DIR;
  int timeout = 60;

  std::string mode = argv[1];
  for (int i = 2; i < argc; ++i) {
    auto need = [&](const char* name) -> const char* {
      if (i + 1 >= argc) {
        std::fprintf(stderr, "missing value for %s\n", name);
        std::exit(1);
      }
      return argv[++i];
    };
    std::string a = argv[i];
    if (a == "--artifacts") opts.artifactsDir = need("--artifacts");
    else if (a == "--seed") opts.params.seed = uint32_t(std::atoi(need("--seed")));
    else if (a == "--width") opts.params.width = uint32_t(std::atoi(need("--width")));
    else if (a == "--height") opts.params.height = uint32_t(std::atoi(need("--height")));
    else if (a == "--timeout") timeout = std::atoi(need("--timeout"));
    else {
      std::fprintf(stderr, "unknown option: %s\n", a.c_str());
      return 1;
    }
  }

  if (mode == "run") return runExperiment(opts);
  if (mode == "present") return runPresent(opts.params, timeout);

  std::fprintf(stderr, "unknown mode: %s\n", mode.c_str());
  printUsage(argv[0]);
  return 1;
}
