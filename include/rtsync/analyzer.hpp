#pragma once

#include "rtsync/types.hpp"

#include <string>
#include <vector>

namespace rtsync {

struct AnalyzeOptions {
  std::string input_sha256;
  std::uint64_t input_bytes = 0;
  std::string source_dir;
  std::string diff_dir;
  std::string frame_dir;
  Limits limits;
};

Analysis analyze_trace(Trace trace, const AnalyzeOptions& options);
const Finding* find_finding(const Analysis& analysis, const std::string& id);

}  // namespace rtsync

