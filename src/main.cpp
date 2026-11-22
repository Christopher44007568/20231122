#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "diff.h"
#include "review.h"
#include "snapshot.h"
#include "util.h"

namespace {

void Usage() {
  std::cerr <<
      "texarray-review - reviewable texture-array paging pipeline\n"
      "usage:\n"
      "  texarray-review review --out DIR [--frames N] [--grid N]\n"
      "                         [--inject-fault i,j,...] [--shader-dir DIR]\n"
      "  texarray-review capture --version v1|v2 --snapshot FILE\n"
      "                         [--format 1|2] [--evidence DIR] ...\n"
      "  texarray-review diff --a SNAP --b SNAP --evidence DIR\n"
      "  texarray-review selftest --out DIR\n";
}

std::string ArgValue(int& i, int argc, char** argv, const std::string& key,
                     const std::string& dflt) {
  if (key.rfind("--", 0) == 0 && i + 1 < argc) return argv[++i];
  return dflt;
}

std::vector<int> ParseIntList(const std::string& s) {
  std::vector<int> out;
  if (s.empty()) return out;
  size_t start = 0;
  while (start <= s.size()) {
    size_t comma = s.find(',', start);
    std::string tok = s.substr(start, comma == std::string::npos
                                       ? std::string::npos
                                       : comma - start);
    if (!tok.empty()) out.push_back(std::stoi(tok));
    if (comma == std::string::npos) break;
    start = comma + 1;
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  tar::Log log;
  if (argc < 2) {
    Usage();
    return 2;
  }
  const std::string cmd = argv[1];
  try {
    if (cmd == "review" || cmd == "selftest") {
      tar::ReviewOptions opt;
      for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--out") opt.out_dir = ArgValue(i, argc, argv, a, opt.out_dir);
        else if (a == "--frames") opt.frames = std::stoi(ArgValue(i, argc, argv, a, "6"));
        else if (a == "--grid") opt.grid = std::stoi(ArgValue(i, argc, argv, a, "4"));
        else if (a == "--shader-dir") opt.shader_dir = ArgValue(i, argc, argv, a, "");
        else if (a == "--inject-fault")
          opt.inject_fault_objs =
              ParseIntList(ArgValue(i, argc, argv, a, ""));
      }
      if (opt.shader_dir.empty()) {
        opt.shader_dir = tar::JoinPath(tar::DirName(argv[0]), "shaders");
      }
      if (cmd == "review") return tar::RunReview(opt, log);
      // selftest: clean review, then a fault-injection review that must
      // still complete and record the failures.
      int rc = tar::RunReview(opt, log);
      if (rc != 0) return rc;
      tar::ReviewOptions faulty = opt;
      faulty.out_dir = tar::JoinPath(opt.out_dir, "fault_run");
      faulty.inject_fault_objs = {3, 7};
      rc = tar::RunReview(faulty, log);
      if (rc != 0) return rc;
      // Verify the fault run recorded error events but kept processing.
      tar::Snapshot s = tar::ReadSnapshot(
          tar::JoinPath(tar::JoinPath(faulty.out_dir, "evidence"),
                        "snapshot_v2.taps"));
      int errors = 0;
      int draws = 0;
      for (const auto& e : s.events) {
        if (e.kind == "error") ++errors;
      }
      draws = static_cast<int>(s.draws.size());
      int expected_draws = s.frames * (s.objects - 2);
      if (errors != s.frames * 2 || draws != expected_draws) {
        log.Error("selftest: expected " + std::to_string(s.frames * 2) +
                  " errors and " + std::to_string(expected_draws) +
                  " draws, got " + std::to_string(errors) + " / " +
                  std::to_string(draws));
        return 1;
      }
      if (tar::FileExists(tar::JoinPath(faulty.out_dir, "tmp"))) {
        log.Error("selftest: temp dir was not cleaned up");
        return 1;
      }
      log.Info("selftest OK");
      return 0;
    }
    if (cmd == "capture") {
      tar::RenderConfig cfg;
      std::string snapshot_path;
      std::string version = "v1";
      int format = 0;
      for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--version") version = ArgValue(i, argc, argv, a, version);
        else if (a == "--snapshot") snapshot_path = ArgValue(i, argc, argv, a, "");
        else if (a == "--format") format = std::stoi(ArgValue(i, argc, argv, a, "0"));
        else if (a == "--evidence") cfg.evidence_dir = ArgValue(i, argc, argv, a, "");
        else if (a == "--shader-dir") cfg.shader_dir = ArgValue(i, argc, argv, a, "");
        else if (a == "--frames") cfg.frames = std::stoi(ArgValue(i, argc, argv, a, "6"));
        else if (a == "--inject-fault")
          cfg.inject_fault_objs =
              ParseIntList(ArgValue(i, argc, argv, a, ""));
      }
      if (snapshot_path.empty() || cfg.evidence_dir.empty()) {
        log.Error("capture requires --snapshot and --evidence");
        return 2;
      }
      if (cfg.shader_dir.empty()) {
        cfg.shader_dir = tar::JoinPath(tar::DirName(argv[0]), "shaders");
      }
      if (format == 0) format = version == "v2" ? 2 : 1;
      cfg.label = version;
      cfg.paging.extension_enabled = version == "v2";
      tar::MakeDirs(cfg.evidence_dir);
      tar::RunCapture(cfg, format, snapshot_path, log);
      return 0;
    }
    if (cmd == "diff") {
      std::string path_a, path_b, evidence;
      for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--a") path_a = ArgValue(i, argc, argv, a, "");
        else if (a == "--b") path_b = ArgValue(i, argc, argv, a, "");
        else if (a == "--evidence") evidence = ArgValue(i, argc, argv, a, "");
      }
      if (path_a.empty() || path_b.empty() || evidence.empty()) {
        log.Error("diff requires --a, --b and --evidence");
        return 2;
      }
      tar::Snapshot a = tar::ReadSnapshot(path_a);
      tar::Snapshot b = tar::ReadSnapshot(path_b);
      tar::DiffReport rep = tar::Correlate(a, b, evidence, log);
      log.Info("total diff pixels = " + std::to_string(rep.total_diff_pixels));
      return 0;
    }
    Usage();
    return 2;
  } catch (const std::exception& e) {
    log.Error(std::string("fatal: ") + e.what());
    return 1;
  }
}
