#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "report.h"
#include "scene.h"
#include "trace.h"
#include "vk.h"

namespace {

GLFWwindow* initGlfw() {
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);  // headless-safe
  if (!glfwInit()) {
    std::cerr << "[probe] glfwInit failed, continuing without window\n";
    return nullptr;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  GLFWwindow* win = glfwCreateWindow(640, 480, "reflection-probe", nullptr, nullptr);
  if (!win) {
    const char* msg = nullptr;
    glfwGetError(&msg);
    std::cerr << "[probe] no window (" << (msg ? msg : "?") << "), offscreen only\n";
  }
  return win;
}

struct RenderEnv {
  GLFWwindow* win = nullptr;
  Gpu gpu;
  SceneRenderer scene;
  RenderEnv() {
    win = initGlfw();
    gpu.init();
    scene.init(gpu);
  }
  ~RenderEnv() {
    scene.destroy();
    gpu.destroy();
    if (win) glfwDestroyWindow(win);
    glfwTerminate();
  }
};

std::map<std::string, std::string> parseArgs(int argc, char** argv, int from) {
  std::map<std::string, std::string> m;
  for (int i = from; i < argc; ++i) {
    std::string a = argv[i];
    if (a.rfind("--", 0) == 0 && i + 1 < argc) m[a.substr(2)] = argv[++i];
  }
  return m;
}

std::string need(const std::map<std::string, std::string>& m,
                 const std::string& k) {
  auto it = m.find(k);
  if (it == m.end()) throw std::runtime_error("missing --" + k);
  return it->second;
}

uint64_t hashImage(const Image& im) {
  return im.rgba.empty() ? 0 : trace::fnv1a64(im.rgba.data(), im.rgba.size());
}

int cmdRecord(const std::map<std::string, std::string>& args) {
  std::string fmt = args.count("format") ? args.at("format") : "v2";
  std::string out = need(args, "out");
  RenderEnv env;
  trace::Trace t;
  t.version = fmt == "v1" ? trace::kVersionLegacy : trace::kVersionCurrent;
  std::vector<uint64_t> fb;
  // One live frame of the probe; GLFW pumps events like the real-time app.
  Image final = env.scene.run(SceneRenderer::fullFrame(), &t, &fb);
  if (env.win) glfwPollEvents();
  trace::writeTrace(out, t);
  std::string png = args.count("png") ? args.at("png") : "";
  if (!png.empty()) writePng(png, final);
  std::cout << "[record] format=" << fmt << " records=" << t.records.size()
            << " finalHash=0x" << std::hex << t.finalFbHash << std::dec
            << " -> " << out << "\n";
  for (const auto& r : t.records) {
    if (r.type == trace::RecordType::kDraw)
      std::cout << "  #" << r.draw.index << " draw " << r.draw.name
                << " fb=" << r.draw.fbTarget << "\n";
    else
      std::cout << "  #" << r.res.index << " res  " << r.res.name << " "
                << trace::layoutName(r.res.oldState) << " -> "
                << trace::layoutName(r.res.newState) << "\n";
  }
  return 0;
}

int cmdTruncate(const std::map<std::string, std::string>& args) {
  trace::Trace full = trace::readTrace(need(args, "in"), true);
  size_t keep = std::stoul(need(args, "keep-records"));
  size_t part = std::stoul(need(args, "partial-bytes"));
  std::string out = need(args, "out");
  trace::writeTruncated(out, full, keep, part);
  std::cout << "[truncate] kept " << keep << " of " << full.records.size()
            << " records + " << part << " partial bytes -> " << out << "\n";
  return 0;
}

int cmdRecover(const std::map<std::string, std::string>& args) {
  std::string in = need(args, "in");
  trace::Trace t = trace::readTrace(in, false);
  std::string out = need(args, "out");
  trace::Trace clean;
  clean.version = t.version;
  clean.records = t.records;
  clean.finalFbHash = 0;  // unknown: footer was lost
  trace::writeTrace(out, clean);
  std::cout << "[recover] salvaged " << t.records.size() << " records";
  if (t.truncated)
    std::cout << ", truncation at step " << t.truncatedAtIndex << " ("
              << t.droppedBytes << " bytes dropped)";
  std::cout << " -> " << out << "\n";
  return 0;
}

struct ReplayOut {
  std::vector<uint64_t> fbHashes;
  Image final;
  uint64_t finalHash = 0;
};

// Re-executes the draw calls listed in a trace (any version, possibly
// recovered) on a fresh renderer and captures per-draw framebuffer hashes.
ReplayOut replayTrace(const trace::Trace& t) {
  std::vector<DrawStep> frame = SceneRenderer::fullFrame();
  std::vector<DrawStep> steps;
  for (const auto& r : t.records) {
    if (r.type != trace::RecordType::kDraw) continue;
    bool found = false;
    for (const auto& s : frame)
      if (s.name == r.draw.name) { steps.push_back(s); found = true; break; }
    if (!found)
      throw std::runtime_error("trace references unknown draw: " + r.draw.name);
  }
  RenderEnv env;
  ReplayOut out;
  out.final = env.scene.run(steps, nullptr, &out.fbHashes);
  if (env.win) glfwPollEvents();
  out.finalHash = hashImage(out.final);
  return out;
}

int cmdReplay(const std::map<std::string, std::string>& args) {
  trace::Trace t = trace::readTrace(need(args, "trace"), false);
  ReplayOut r = replayTrace(t);
  std::string dir = args.count("out-dir") ? args.at("out-dir") : ".";
  writePng(dir + "/replay_final.png", r.final);
  std::cout << "[replay] format=v" << t.version
            << (t.recovered ? " (recovered)" : "") << " draws="
            << r.fbHashes.size() << "\n";
  size_t n = 0;
  for (const auto& rec : t.records)
    if (rec.type == trace::RecordType::kDraw && n < r.fbHashes.size())
      std::cout << "  #" << rec.draw.index << " " << rec.draw.name
                << " fbHash=0x" << std::hex << r.fbHashes[n++] << std::dec
                << "\n";
  std::cout << "  final=0x" << std::hex << r.finalHash << std::dec << "\n";
  return 0;
}

int cmdReview(const std::map<std::string, std::string>& args) {
  std::string dir = args.count("out-dir") ? args.at("out-dir") : ".";
  trace::Trace ref = trace::readTrace(need(args, "reference"), true);
  trace::Trace cand = trace::readTrace(need(args, "candidate"), false);

  ReplayOut replay = replayTrace(cand);

  // Reference pixels for the visual diff (deterministic re-render).
  Image refFinal;
  {
    RenderEnv env;
    refFinal = env.scene.run(SceneRenderer::fullFrame(), nullptr, nullptr);
  }
  if (ref.finalFbHash == 0) ref.finalFbHash = hashImage(refFinal);  // v1

  Image diff;
  ReviewResult res = buildReview(ref, cand, replay.fbHashes, replay.finalHash,
                                 refFinal, replay.final, &diff);

  std::vector<std::string> evidence;
  writePng(dir + "/reference.png", refFinal);
  evidence.push_back("reference.png");
  writePng(dir + "/replay.png", replay.final);
  evidence.push_back("replay.png");
  if (diff.width) {
    writePng(dir + "/diff.png", diff);
    evidence.push_back("diff.png");
  }
  writeReportJson(dir + "/report.json", res, evidence);
  evidence.push_back("report.json");

  std::cout << "[review] reference=v" << res.referenceVersion
            << " candidate=v" << res.candidateVersion
            << (res.candidateRecovered ? " (recovered, truncated at step " +
                   std::to_string(res.truncatedAtIndex) + ")"
                                      : "")
            << "\n";
  for (const auto& row : res.rows) {
    const char* mark = (row.missing || !row.stateOk || !row.fbOk) ? "FAIL" : "ok  ";
    std::cout << "  [" << mark << "] #" << row.index << " " << row.kind
              << " " << row.name;
    if (!row.detail.empty()) std::cout << "  -- " << row.detail;
    std::cout << "\n";
  }
  if (res.firstError.found)
    std::cout << "[review] FIRST ERROR: " << res.firstError.kind << " -- "
              << res.firstError.summary << "\n";
  else
    std::cout << "[review] no divergence: candidate matches reference\n";
  std::cout << "[review] final image diff: " << res.finalDiffPixels
            << " px, max channel delta " << res.finalMaxDiff
            << "; evidence in " << dir << "\n";
  return res.firstError.found ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr
        << "usage:\n"
        << "  probe record --format v1|v2 --out T [--png P]\n"
        << "  probe truncate --in T --out T2 --keep-records K --partial-bytes P\n"
        << "  probe recover --in T2 --out T3\n"
        << "  probe replay --trace T [--out-dir D]\n"
        << "  probe review --reference T --candidate T2 [--out-dir D]\n";
    return 1;
  }
  try {
    std::string cmd = argv[1];
    auto args = parseArgs(argc, argv, 2);
    if (cmd == "record") return cmdRecord(args);
    if (cmd == "truncate") return cmdTruncate(args);
    if (cmd == "recover") return cmdRecover(args);
    if (cmd == "replay") return cmdReplay(args);
    if (cmd == "review") return cmdReview(args);
    std::cerr << "unknown command: " << cmd << "\n";
    return 1;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
    return 1;
  }
}
