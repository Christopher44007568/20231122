#pragma once

#include <string>
#include <vector>

#include "mi/types.hpp"

namespace mi {

struct Scenario {
  std::string name;
  std::string purpose;
  std::string control_of;
  std::vector<Command> commands;
};

std::vector<Scenario> build_scenarios();

}  // namespace mi
