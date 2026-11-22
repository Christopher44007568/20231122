#include "rtsync/artifact.hpp"
#include "rtsync/json_util.hpp"
#include "rtsync/renderer.hpp"
#include "rtsync/scenario.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace rtsync;

namespace {

std::string argument(int argc, char** argv, const char* name,
                     const std::string& fallback = "") {
  for (int i = 2; i + 1 < argc; ++i) {
    if (std::strcmp(argv[i], name) == 0) return argv[i + 1];
  }
  return fallback;
}

std::uint64_t argument_u64(int argc, char** argv, const char* name,
                           std::uint64_t fallback) {
  const std::string value = argument(argc, argv, name);
  if (value.empty()) return fallback;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(value.c_str(), &end, 10);
  return end != nullptr && *end == '\0' ? static_cast<std::uint64_t>(parsed)
                                         : fallback;
}

void usage() {
  std::printf(
      "rtsync %s — render-thread synchronization evidence tool\n\n"
      "Usage:\n"
      "  rtsync demo --out DIR\n"
      "  rtsync analyze --input FILE --out DIR [limits]\n"
      "  rtsync reload --artifact DIR [--out DIR]\n"
      "  rtsync verify --artifact DIR [--report FILE]\n\n"
      "Limits:\n"
      "  --max-input-bytes N   default 33554432\n"
      "  --max-records N       default 100000\n"
      "  --max-findings N      default 256\n\n"
      "Exit codes: 0 success, 1 runtime failure, 2 input rejected/invalid.\n",
      kVersion);
}

int status_exit(const std::string& status) {
  if (status == "ok" || status == "empty") return 0;
  return 2;
}

int command_demo(int argc, char** argv) {
  const std::string output = argument(argc, argv, "--out");
  if (output.empty()) {
    std::fprintf(stderr, "demo: --out is required\n");
    return 2;
  }
  const fs::path artifact(output);
  const fs::path staging = fs::path(output + ".source");
  std::error_code ec;
  fs::remove_all(staging, ec);
  fs::create_directories(staging, ec);
  if (ec) {
    std::fprintf(stderr, "demo: cannot create staging directory: %s\n",
                 ec.message().c_str());
    return 1;
  }
  RenderProbe probe;
  std::string error;
  if (!render_demo_frames(staging.string(), &probe, &error)) {
    std::fprintf(stderr, "demo: Vulkan/GLFW evidence render failed: %s\n",
                 error.c_str());
    std::fprintf(stderr, "staging directory kept at %s\n",
                 staging.string().c_str());
    return 1;
  }
  ScenarioOptions scenario;
  scenario.output_dir = staging.string();
  Trace trace;
  if (!write_demo_trace(scenario, &trace, &error)) {
    std::fprintf(stderr, "demo: cannot write trace: %s\n", error.c_str());
    return 1;
  }
  AnalyzeRequest request;
  request.input_path = (staging / "trace.json").string();
  request.output_dir = artifact.string();
  request.limits.max_input_bytes =
      argument_u64(argc, argv, "--max-input-bytes",
                   request.limits.max_input_bytes);
  request.limits.max_records =
      argument_u64(argc, argv, "--max-records", request.limits.max_records);
  request.limits.max_findings = static_cast<std::size_t>(
      argument_u64(argc, argv, "--max-findings",
                   static_cast<std::uint64_t>(request.limits.max_findings)));
  const AnalyzeOutcome outcome = run_analysis(request);
  if (!outcome.ok) {
    std::fprintf(stderr, "demo: analysis failed: %s\n",
                 outcome.message.c_str());
    return 1;
  }
  fs::remove_all(staging, ec);
  std::printf("demo artifact: %s\n", artifact.string().c_str());
  std::printf("status: %s\n", outcome.status.c_str());
  std::printf("Vulkan device: %s (%s), GLFW initialized: %s\n",
              probe.device_name.c_str(), probe.api_version.c_str(),
              probe.glfw_initialized ? "yes" : "no");
  std::printf("golden: %s\nobserved: %s\n",
              probe.golden_sha256.c_str(), probe.observed_sha256.c_str());
  return status_exit(outcome.status);
}

int command_analyze(int argc, char** argv) {
  AnalyzeRequest request;
  request.input_path = argument(argc, argv, "--input");
  request.output_dir = argument(argc, argv, "--out");
  if (request.input_path.empty() || request.output_dir.empty()) {
    std::fprintf(stderr, "analyze: --input and --out are required\n");
    return 2;
  }
  request.limits.max_input_bytes =
      argument_u64(argc, argv, "--max-input-bytes",
                   request.limits.max_input_bytes);
  request.limits.max_records =
      argument_u64(argc, argv, "--max-records", request.limits.max_records);
  request.limits.max_findings = static_cast<std::size_t>(
      argument_u64(argc, argv, "--max-findings",
                   static_cast<std::uint64_t>(request.limits.max_findings)));
  const AnalyzeOutcome outcome = run_analysis(request);
  if (!outcome.ok) {
    std::fprintf(stderr, "analyze: %s\n", outcome.message.c_str());
    return 1;
  }
  std::printf("artifact: %s\nstatus: %s\n%s\n",
              outcome.artifact_dir.string().c_str(), outcome.status.c_str(),
              outcome.message.c_str());
  return status_exit(outcome.status);
}

int command_reload(int argc, char** argv) {
  const std::string artifact = argument(argc, argv, "--artifact");
  if (artifact.empty()) {
    std::fprintf(stderr, "reload: --artifact is required\n");
    return 2;
  }
  const ReloadOutcome outcome =
      reload_artifact(artifact, argument(argc, argv, "--out"));
  std::cout << outcome.report.dump(2) << "\n";
  if (!outcome.ok) {
    std::fprintf(stderr, "reload: %s\n", outcome.message.c_str());
    return 1;
  }
  return 0;
}

int command_verify(int argc, char** argv) {
  const std::string artifact = argument(argc, argv, "--artifact");
  if (artifact.empty()) {
    std::fprintf(stderr, "verify: --artifact is required\n");
    return 2;
  }
  const VerifyOutcome outcome = verify_artifact(artifact);
  const std::string report_file = argument(argc, argv, "--report");
  if (!report_file.empty()) {
    std::string error;
    if (!save_json(report_file, outcome.report, &error)) {
      std::fprintf(stderr, "verify: %s\n", error.c_str());
      return 1;
    }
  }
  std::cout << outcome.report.dump(2) << "\n";
  if (!outcome.ok) {
    std::fprintf(stderr, "verify: %s\n", outcome.message.c_str());
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2 || std::strcmp(argv[1], "--help") == 0 ||
      std::strcmp(argv[1], "-h") == 0) {
    usage();
    return argc < 2 ? 2 : 0;
  }
  const std::string command = argv[1];
  if (command == "demo") return command_demo(argc, argv);
  if (command == "analyze") return command_analyze(argc, argv);
  if (command == "reload") return command_reload(argc, argv);
  if (command == "verify") return command_verify(argc, argv);
  std::fprintf(stderr, "unknown command: %s\n", command.c_str());
  usage();
  return 2;
}
