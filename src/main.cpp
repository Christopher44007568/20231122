#include "app.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

void printUsage() {
  std::cout
    << "screen-depth-recheck " << DEPTH_RECHECK_VERSION << "\n"
    << "Usage: depth_recheck [options]\n"
    << "  --session <dir>   Checkpoint/evidence directory\n"
    << "  --headless        Render and export without a GLFW window\n"
    << "  --all             Replay and export every object on startup\n"
    << "  --frames <n>      Present n frames, then save and exit\n"
    << "  --fresh           Start a new checkpoint in the session directory\n"
    << "  --shader-dir <d>  Directory containing compiled .spv shaders\n"
    << "  --help            Show this message\n";
}

std::filesystem::path executableDirectory() {
  std::error_code error;
  const std::filesystem::path self =
    std::filesystem::read_symlink("/proc/self/exe", error);
  if (!error) {
    return self.parent_path();
  }
  return std::filesystem::current_path();
}

}  // namespace

int main(int argc, char** argv) {
  AppOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "--help" || argument == "-h") {
      printUsage();
      return 0;
    }
    if (argument == "--headless") {
      options.headless = true;
    } else if (argument == "--all") {
      options.runAll = true;
    } else if (argument == "--fresh" || argument == "--reset") {
      options.fresh = true;
    } else if (argument == "--frames" && index + 1 < argc) {
      options.frameLimit = std::stoi(argv[++index]);
    } else if (argument == "--session" && index + 1 < argc) {
      options.sessionDirectory = argv[++index];
    } else if (argument == "--shader-dir" && index + 1 < argc) {
      options.shaderDirectory = argv[++index];
    } else {
      std::cerr << "Unknown or incomplete option: " << argument << "\n";
      printUsage();
      return 2;
    }
  }
  if (options.shaderDirectory == "shaders") {
    options.shaderDirectory = executableDirectory() / "shaders";
  }

  Application application;
  std::string error;
  if (!application.initialize(options, error)) {
    std::cerr << "Initialization failed: " << error << "\n";
    if (!options.headless) {
      std::cerr << "For machines without a display, use --headless --all.\n";
    }
    return 1;
  }
  return application.run();
}

