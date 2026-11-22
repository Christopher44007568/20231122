#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pcd {

// One extreme parameter set for the density clip.
struct ParamSet {
  std::string name;
  float radius = 1.0f;
  uint32_t min_neighbors = 0;
  uint32_t max_neighbors = 0xffffffffu;
};

// Parse params.txt (line based):
//   set <name> radius=<r> min_neighbors=<n> max_neighbors=<n>
std::vector<ParamSet> load_param_sets(const std::string& path);

} // namespace pcd
