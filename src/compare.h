#pragma once
#include "workspace.h"
#include <string>

namespace pcd {

// Compare two finished runs (or "original" against a run). Writes a
// JSON diff report into run A's evidence dir and prints a summary.
void compare_runs(const Workspace& ws, const std::string& a,
                  const std::string& b);

} // namespace pcd
