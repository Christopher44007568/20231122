#include "params.h"
#include "util.h"

#include <fstream>
#include <sstream>

namespace pcd {

std::vector<ParamSet> load_param_sets(const std::string& path) {
  std::ifstream in(path);
  if (!in) die("cannot open params: " + path);
  std::vector<ParamSet> sets;
  std::string line;
  int lineno = 0;
  while (std::getline(in, line)) {
    ++lineno;
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line);
    std::string kw;
    ss >> kw;
    if (kw != "set") die("params.txt:" + std::to_string(lineno) +
                         ": expected 'set'");
    ParamSet ps;
    ss >> ps.name;
    if (ps.name.empty())
      die("params.txt:" + std::to_string(lineno) + ": missing name");
    std::string kv;
    while (ss >> kv) {
      auto eq = kv.find('=');
      if (eq == std::string::npos)
        die("params.txt:" + std::to_string(lineno) + ": bad token " + kv);
      std::string k = kv.substr(0, eq), v = kv.substr(eq + 1);
      if (k == "radius") ps.radius = std::stof(v);
      else if (k == "min_neighbors") ps.min_neighbors = std::stoul(v);
      else if (k == "max_neighbors") ps.max_neighbors = std::stoul(v);
      else die("params.txt:" + std::to_string(lineno) + ": unknown key " + k);
    }
    if (ps.radius <= 0.0f)
      die("params.txt:" + std::to_string(lineno) + ": radius must be > 0");
    sets.push_back(ps);
  }
  if (sets.empty()) die("no parameter sets in: " + path);
  return sets;
}

} // namespace pcd
