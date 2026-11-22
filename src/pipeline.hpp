#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "backend.hpp"
#include "common.hpp"

namespace nmts {

struct RunConfig {
  std::string work_dir;
  std::string material_path;
  std::string run_id;
  std::uint32_t chunk_size = 64;
  std::uint32_t pause_after = UINT32_MAX;
  bool force_cpu = false;
  bool force_gpu_oom = false;
  bool force_chunk_failure = false;
};

struct RunSummary {
  std::string run_id;
  std::string started_at;
  std::string ended_at;
  std::uint32_t backend = 0;
  std::uint32_t degrade_reason = 0;
  std::string degrade_detail;
  std::uint32_t total = 0;
  std::uint32_t processed = 0;
  std::uint32_t failed_checks = 0;
  std::uint32_t chunks = 0;
  std::uint32_t chunk_shrink_events = 0;
  bool paused = false;
  std::string material_sha256;
  std::string results_sha256;
  BackendCounters counters;
};

// Exit codes are part of the acceptance contract.
enum ExitCode {
  kExitOk = 0,
  kExitPaused = 10,
  kExitResumeComplete = 0,
  kExitBadArgs = 2,
  kExitMaterial = 3,
  kExitCheckpoint = 4,
  kExitResource = 5,
};

int run_pipeline(const RunConfig& cfg);

}  // namespace nmts
