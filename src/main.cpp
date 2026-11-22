#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "material.hpp"
#include "pipeline.hpp"
#include "report.hpp"
#include "sha256.hpp"
#include "util.hpp"

namespace {

using namespace nmts;

std::string env_or(const char* key, std::string dflt) {
  const char* v = std::getenv(key);
  return v ? std::string(v) : dflt;
}

bool env_flag(const char* key) {
  const char* v = std::getenv(key);
  return v && std::strcmp(v, "1") == 0;
}

int cmd_generate(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: nmts_verify generate <out.nmts>\n";
    return kExitBadArgs;
  }
  GenOptions opt;
  Material m = generate_material(opt);
  if (!write_material(argv[2], m)) {
    std::cerr << "failed to write material\n";
    return kExitMaterial;
  }
  std::cout << "material=" << argv[2]
            << " instances=" << m.header.instance_count
            << " sha256=" << sha256_hex_file(argv[2]) << "\n";
  return 0;
}

RunConfig config_from_env(const std::string& run_id) {
  RunConfig cfg;
  cfg.run_id = run_id;
  cfg.work_dir = env_or("NMTS_WORK_DIR", "out/run");
  cfg.material_path =
      env_or("NMTS_MATERIAL", "out/original/material.nmts");
  cfg.chunk_size =
      static_cast<std::uint32_t>(std::stoul(env_or("NMTS_CHUNK", "64")));
  cfg.pause_after = static_cast<std::uint32_t>(
      std::stoul(env_or("NMTS_PAUSE_AFTER", "4294967295")));
  cfg.force_cpu = env_flag("NMTS_FORCE_CPU");
  cfg.force_gpu_oom = env_flag("NMTS_FORCE_GPU_OOM");
  cfg.force_chunk_failure = env_flag("NMTS_FORCE_CHUNK_FAILURE");
  return cfg;
}

int cmd_run(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: nmts_verify run <run_id>\n";
    return kExitBadArgs;
  }
  return run_pipeline(config_from_env(argv[2]));
}

int cmd_resume(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: nmts_verify resume <run_id>\n";
    return kExitBadArgs;
  }
  RunConfig cfg = config_from_env(argv[2]);
  cfg.pause_after = UINT32_MAX;  // resume always runs to completion
  return run_pipeline(cfg);
}

int cmd_compare(int argc, char** argv) {
  if (argc < 4) {
    std::cerr
        << "usage: nmts_verify compare [--ignore-backend] <a.bin> <b.bin>\n";
    return kExitBadArgs;
  }
  bool ignore = argc >= 5 && std::string(argv[2]) == "--ignore-backend";
  const char* pa = ignore ? argv[3] : argv[2];
  const char* pb = ignore ? argv[4] : argv[3];
  CompareResult cr = compare_result_files(pa, pb, ignore);
  std::cout << "records_a=" << cr.records_a
            << " records_b=" << cr.records_b
            << " differing=" << cr.differing
            << " identical=" << (cr.identical ? "true" : "false") << "\n";
  if (!cr.first_difference.empty())
    std::cout << "first_difference=" << cr.first_difference << "\n";
  return cr.identical ? 0 : 6;
}

int cmd_selftest() {
  std::cout << "selftest: use scripts/acceptance.sh for the full "
               "pause/resume + degrade acceptance flow\n";
  GenOptions opt;
  Material m = generate_material(opt);
  std::cout << "generated instances=" << m.header.instance_count
            << " cases=" << kCaseCount
            << " repeats_per_case=" << kRepeatCount << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cout
        << "nmts_verify - normal-map tangent verification pipeline\n"
        << "subcommands: generate | run | resume | compare | selftest\n";
    return kExitBadArgs;
  }
  std::string cmd = argv[1];
  if (cmd == "generate") return cmd_generate(argc, argv);
  if (cmd == "run") return cmd_run(argc, argv);
  if (cmd == "resume") return cmd_resume(argc, argv);
  if (cmd == "compare") return cmd_compare(argc, argv);
  if (cmd == "selftest") return cmd_selftest();
  std::cerr << "unknown subcommand: " << cmd << "\n";
  return kExitBadArgs;
}
