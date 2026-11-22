#pragma once

#include <string>

#include "backend.hpp"
#include "common.hpp"
#include "pipeline.hpp"

namespace nmts {

void save_report(const std::string& work_dir,
                 const RunSummary& summary,
                 const Material& material,
                 DegradeReason degrade,
                 const BackendCounters& counters,
                 std::uint32_t committed,
                 bool fatal);

struct CompareResult {
  bool identical = false;
  std::size_t records_a = 0;
  std::size_t records_b = 0;
  std::size_t differing = 0;
  std::string first_difference;
};

CompareResult compare_result_files(const std::string& a,
                                   const std::string& b,
                                   bool ignore_backend = false);

}  // namespace nmts
