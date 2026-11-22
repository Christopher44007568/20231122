#pragma once
#include "workspace.h"
#include <string>

namespace pcd {

// Acceptance check for one run. Verifies the untouched original, the
// duplicated working copy, journal/output consistency, and the
// recorded UTC timeline. Returns 0 on PASS, 1 on FAIL.
int accept_run(const Workspace& ws, const std::string& run_id);

} // namespace pcd
