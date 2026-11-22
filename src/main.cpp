#include "pipeline.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using kfc::DemoOptions;
using kfc::RunOptions;

const char* kUsage = R"(skeleton-kf - pausable skeletal keyframe compression lab

Usage:
  skeleton-kf demo   --out DIR [--budget-mb MB] [--pause-after-chunks N]
                     [--window] [--no-vulkan]
  skeleton-kf run    --out DIR [--input DIR] [--budget-mb MB]
                     [--pause-after-chunks N] [--window] [--no-vulkan]
                     [--baseline FILE] [--degraded FILE]
  skeleton-kf resume --checkpoint FILE [--out DIR]
                     [--pause-after-chunks N] [--window]
                     [--baseline FILE] [--degraded FILE]
  skeleton-kf verify --record FILE [--strict]
  skeleton-kf replay --record FILE
  skeleton-kf reload-check --file FILE
  skeleton-kf inspect --file FILE
  skeleton-kf compare --a FILE --b FILE
  skeleton-kf manifest --out DIR

With no arguments, runs `demo --out artifacts/demo`.
)";

std::string valueFor(const std::vector<std::string>& args, std::size_t& index,
                     const std::string& name) {
  if (index + 1 >= args.size()) {
    throw std::runtime_error("missing value for " + name);
  }
  return args[++index];
}

double parseDouble(const std::string& text, const std::string& name) {
  std::size_t consumed = 0;
  const double value = std::stod(text, &consumed);
  if (consumed != text.size()) {
    throw std::runtime_error("invalid number for " + name + ": " + text);
  }
  return value;
}

std::uint64_t parseUint(const std::string& text, const std::string& name) {
  std::size_t consumed = 0;
  const unsigned long long value = std::stoull(text, &consumed);
  if (consumed != text.size()) {
    throw std::runtime_error("invalid integer for " + name + ": " + text);
  }
  return static_cast<std::uint64_t>(value);
}

int runDemo(const std::vector<std::string>& args) {
  DemoOptions options;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--out") {
      options.outputDir = valueFor(args, i, args[i]);
    } else if (args[i] == "--budget-mb") {
      options.budgetMb = parseDouble(valueFor(args, i, args[i]), args[i]);
    } else if (args[i] == "--pause-after-chunks") {
      options.pauseAfterChunks =
          parseUint(valueFor(args, i, args[i]), args[i]);
    } else if (args[i] == "--window") {
      options.openWindow = true;
    } else if (args[i] == "--no-vulkan") {
      options.disableVulkan = true;
    } else {
      throw std::runtime_error("unknown demo option: " + args[i]);
    }
  }
  if (options.outputDir.empty()) options.outputDir = "artifacts/demo";
  return kfc::commandDemo(options);
}

int runPipelineCommand(const std::vector<std::string>& args) {
  RunOptions options;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--out") {
      options.outputDir = valueFor(args, i, args[i]);
    } else if (args[i] == "--input") {
      options.inputDir = valueFor(args, i, args[i]);
    } else if (args[i] == "--budget-mb") {
      options.budgetMb = parseDouble(valueFor(args, i, args[i]), args[i]);
    } else if (args[i] == "--pause-after-chunks") {
      options.pauseAfterChunks =
          parseUint(valueFor(args, i, args[i]), args[i]);
    } else if (args[i] == "--window") {
      options.openWindow = true;
    } else if (args[i] == "--no-vulkan") {
      options.disableVulkan = true;
    } else if (args[i] == "--baseline") {
      options.baselinePath = valueFor(args, i, args[i]);
    } else if (args[i] == "--degraded") {
      options.degradedPath = valueFor(args, i, args[i]);
    } else {
      throw std::runtime_error("unknown run option: " + args[i]);
    }
  }
  return kfc::commandRun(options);
}

int runResume(const std::vector<std::string>& args) {
  std::string checkpoint;
  std::string output;
  std::string baseline;
  std::string degraded;
  std::uint64_t pause = 0;
  bool window = false;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--checkpoint") {
      checkpoint = valueFor(args, i, args[i]);
    } else if (args[i] == "--out") {
      output = valueFor(args, i, args[i]);
    } else if (args[i] == "--pause-after-chunks") {
      pause = parseUint(valueFor(args, i, args[i]), args[i]);
    } else if (args[i] == "--baseline") {
      baseline = valueFor(args, i, args[i]);
    } else if (args[i] == "--degraded") {
      degraded = valueFor(args, i, args[i]);
    } else if (args[i] == "--window") {
      window = true;
    } else {
      throw std::runtime_error("unknown resume option: " + args[i]);
    }
  }
  if (checkpoint.empty()) throw std::runtime_error("--checkpoint is required");
  return kfc::commandResume(checkpoint, output, pause, window, baseline,
                            degraded);
}

int runSingleFileCommand(const std::vector<std::string>& args,
                         const std::string& option,
                         int (*handler)(const std::string&)) {
  std::string path;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == option) {
      path = valueFor(args, i, args[i]);
    } else {
      throw std::runtime_error("unknown option: " + args[i]);
    }
  }
  if (path.empty()) throw std::runtime_error(option + " is required");
  return handler(path);
}

int runVerify(const std::vector<std::string>& args) {
  std::string record;
  bool strict = false;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--record") {
      record = valueFor(args, i, args[i]);
    } else if (args[i] == "--strict") {
      strict = true;
    } else {
      throw std::runtime_error("unknown verify option: " + args[i]);
    }
  }
  if (record.empty()) throw std::runtime_error("--record is required");
  return kfc::commandVerify(record, strict);
}

int runCompare(const std::vector<std::string>& args) {
  std::string left;
  std::string right;
  std::vector<std::string> positional;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--a") {
      left = valueFor(args, i, args[i]);
    } else if (args[i] == "--b") {
      right = valueFor(args, i, args[i]);
    } else if (args[i].rfind("--", 0) == 0) {
      throw std::runtime_error("unknown compare option: " + args[i]);
    } else {
      positional.push_back(args[i]);
    }
  }
  if (left.empty() && !positional.empty()) left = positional[0];
  if (right.empty() && positional.size() > 1) right = positional[1];
  if (left.empty() || right.empty()) {
    throw std::runtime_error("compare needs --a FILE --b FILE");
  }
  return kfc::commandCompare(left, right);
}

int runManifest(const std::vector<std::string>& args) {
  std::string outputDir;
  for (std::size_t i = 2; i < args.size(); ++i) {
    if (args[i] == "--out") {
      outputDir = valueFor(args, i, args[i]);
    } else {
      throw std::runtime_error("unknown manifest option: " + args[i]);
    }
  }
  if (outputDir.empty()) throw std::runtime_error("--out is required");
  return kfc::commandManifest(outputDir);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::vector<std::string> args;
    args.reserve(static_cast<std::size_t>(argc));
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    const std::string command = args.size() > 1 ? args[1] : "demo";
    if (command == "help" || command == "--help" || command == "-h") {
      std::cout << kUsage;
      return 0;
    }
    if (command == "--version") {
      std::cout << "skeleton-kf 1.0.0 (C++17, Vulkan compute, GLFW)\n";
      return 0;
    }
    if (command == "demo") return runDemo(args);
    if (command == "run") return runPipelineCommand(args);
    if (command == "resume") return runResume(args);
    if (command == "verify") return runVerify(args);
    if (command == "replay") {
      return runSingleFileCommand(args, "--record", &kfc::commandReplay);
    }
    if (command == "reload-check") {
      return runSingleFileCommand(args, "--file", &kfc::commandReloadCheck);
    }
    if (command == "inspect") {
      return runSingleFileCommand(args, "--file", &kfc::commandInspect);
    }
    if (command == "compare") return runCompare(args);
    if (command == "manifest") return runManifest(args);
    std::cerr << "unknown command: " << command << "\n\n" << kUsage;
    return 2;
  } catch (const std::exception& exception) {
    std::cerr << "error: " << exception.what() << "\n";
    return 1;
  }
}
