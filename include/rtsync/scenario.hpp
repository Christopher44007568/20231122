#pragma once

#include "rtsync/types.hpp"

#include <string>

namespace rtsync {

struct ScenarioOptions {
  std::string output_dir;
  std::string golden_image = "golden.ppm";
  std::string observed_image = "observed.ppm";
};

bool write_demo_trace(const ScenarioOptions& options, Trace* trace,
                      std::string* error);

}  // namespace rtsync

