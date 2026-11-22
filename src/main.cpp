#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include "capture_format.h"
#include "recorder.h"
#include "replayer.h"

namespace {

// GLFW provides platform/window integration. Headless runs (CI, lavapipe)
// fall back to the null platform; rendering itself is fully offscreen.
struct GlfwGuard {
  GlfwGuard() {
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
      glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (!glfwInit()) throw std::runtime_error("glfwInit failed");
  }
  ~GlfwGuard() { glfwTerminate(); }
};

GLFWwindow* maybeOpenPreview(const char* title) {
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* w = glfwCreateWindow(320, 240, title, nullptr, nullptr);
  return w;  // nullptr on headless/null platform: preview is optional
}

uint32_t parseTopology(const std::string& s) {
  static const std::map<std::string, uint32_t> kNames = {
      {"POINT_LIST", 0}, {"LINE_LIST", 1}, {"LINE_STRIP", 2},
      {"TRIANGLE_LIST", 3}, {"TRIANGLE_STRIP", 4}, {"TRIANGLE_FAN", 5},
  };
  auto it = kNames.find(s);
  if (it == kNames.end()) throw std::runtime_error("unknown topology name: " + s);
  return it->second;
}

void usage() {
  std::printf(
      "topo_diag - 图元拓扑诊断（C++17 / Vulkan / GLFW）\n"
      "用法:\n"
      "  topo_diag record --out <file.ptdc> [--legacy] [--corrupt-topology <draw> <TOPO>]\n"
      "  topo_diag replay <file.ptdc> [--evidence <dir>] [--tolerance <n>]\n"
      "                   [--override-topology <draw>=<TOPO>]...\n"
      "  topo_diag extensions\n"
      "拓扑名: POINT_LIST LINE_LIST LINE_STRIP TRIANGLE_LIST TRIANGLE_STRIP TRIANGLE_FAN\n");
}

int cmdRecord(int argc, char** argv) {
  RecordOptions opts;
  for (int i = 0; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--out" && i + 1 < argc) opts.outPath = argv[++i];
    else if (a == "--legacy") opts.legacyFormat = true;
    else if (a == "--corrupt-topology" && i + 2 < argc) {
      opts.corruptDraw = std::stoi(argv[++i]);
      opts.corruptTopology = parseTopology(argv[++i]);
    } else {
      throw std::runtime_error("record: bad or incomplete argument: " + a);
    }
  }
  if (opts.outPath.empty()) throw std::runtime_error("record: --out is required");
  GLFWwindow* preview = maybeOpenPreview("topo_diag record");
  int rc = recordCapture(opts);
  if (preview) glfwDestroyWindow(preview);
  return rc;
}

int cmdReplay(int argc, char** argv) {
  ReplayOptions opts;
  bool haveCapture = false;
  for (int i = 0; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--evidence" && i + 1 < argc) opts.evidenceRoot = argv[++i];
    else if (a == "--tolerance" && i + 1 < argc)
      opts.tolerance = static_cast<uint8_t>(std::stoi(argv[++i]));
    else if (a == "--override-topology" && i + 1 < argc) {
      std::string spec = argv[++i];
      auto eq = spec.find('=');
      if (eq == std::string::npos) throw std::runtime_error("override must be <draw>=<TOPO>");
      opts.topologyOverrides[static_cast<uint32_t>(std::stoul(spec.substr(0, eq)))] =
          parseTopology(spec.substr(eq + 1));
    } else if (!a.empty() && a[0] != '-' && !haveCapture) {
      opts.capturePath = a;
      haveCapture = true;
    } else {
      throw std::runtime_error("replay: bad or incomplete argument: " + a);
    }
  }
  if (!haveCapture) throw std::runtime_error("replay: capture file is required");
  GLFWwindow* preview = maybeOpenPreview("topo_diag replay");
  int rc = replayCapture(opts);
  if (preview) glfwDestroyWindow(preview);
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    usage();
    return 1;
  }
  std::string cmd = argv[1];
  try {
    if (cmd == "record") {
      GlfwGuard glfw;
      return cmdRecord(argc - 2, argv + 2);
    }
    if (cmd == "replay") {
      GlfwGuard glfw;
      return cmdReplay(argc - 2, argv + 2);
    }
    if (cmd == "extensions") {
      std::printf("本重放器已实现的扩展处理器:\n  VK_KHR_maintenance1\n"
                  "其余扩展记录一律按未知扩展处理：标记、保存证据、继续重放。\n");
      return 0;
    }
    usage();
    return 1;
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 1;
  }
}
