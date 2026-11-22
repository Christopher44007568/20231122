#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "common.hpp"
#include "verify_core.hpp"

namespace nmts {

struct BackendCounters {
  std::uint64_t allocation_attempts = 0;
  std::uint64_t allocation_failures = 0;
  std::uint64_t degraded_chunks = 0;
};

struct BackendOptions {
  bool force_cpu = false;
  bool force_gpu_oom = false;
  bool force_chunk_failure = false;
  std::uint32_t chunk_size = 64;
};

class IBackend {
 public:
  virtual ~IBackend() = default;
  virtual std::uint32_t kind() const = 0;
  // Verifies a contiguous index range [first, first + count).
  virtual bool verify_range(const Material& m,
                            std::uint32_t first,
                            std::uint32_t count,
                            std::vector<ResultRecord>& out,
                            std::string& err) = 0;
};

// Creates the best available backend. On any Vulkan/GLFW failure or forced
// flag, returns the CPU backend and records the degrade reason and evidence.
std::unique_ptr<IBackend> create_backend(const BackendOptions& opt,
                                         DegradeReason& reason,
                                         std::string& detail,
                                         BackendCounters& counters);

}  // namespace nmts
