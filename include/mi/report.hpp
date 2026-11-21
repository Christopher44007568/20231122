#pragma once

#include <string>

#include "mi/backend.hpp"
#include "mi/experiment.hpp"

namespace mi {

struct ReportContext {
  BackendInfo backend;
  std::string output_dir;
  std::string build_mode;
  std::string failure;
};

std::string render_report(const ExperimentResult& result,
                          const ReportContext& context);
std::string render_manual_confirmation(const ExperimentResult& result,
                                       const ReportContext& context);
std::string render_failure_report(const ReportContext& context);

}  // namespace mi
