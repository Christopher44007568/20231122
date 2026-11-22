#pragma once
#include "workspace.h"
#include <string>

namespace pcd {

// Interactive GLFW viewer for a finished run (or "original").
// Requires a display; headless environments should use the PPM
// snapshots produced by 'run' instead.
void view_run(const Workspace& ws, const std::string& run_id);

} // namespace pcd
