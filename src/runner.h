#pragma once
#include "params.h"
#include "workspace.h"
#include <csignal>
#include <cstdint>
#include <string>

namespace pcd {

// Binary journal record, 16 bytes. Anchors every correction to an
// original offset: byte offset in the original material = index * 16.
struct EditRec {
  uint64_t index;    // point index in the original material
  uint32_t action;   // 1 = clipped (dropped)
  uint32_t reserved;
};
static_assert(sizeof(EditRec) == 16, "journal record layout");

// Create one duplicated run object per extreme parameter set.
void ws_plan(const Workspace& ws);

// Execute (or resume) a run. Returns 0 on completion, 2 when
// interrupted mid-way (checkpoint preserved for re-entry).
int run_execute(const Workspace& ws, const std::string& run_id,
                uint64_t chunk_size);

// Export the corrected cloud plus its evidence bundle.
void run_export(const Workspace& ws, const std::string& run_id,
                const std::string& out_dir);

// Global stop flag set by the SIGINT/SIGTERM handler.
extern volatile sig_atomic_t g_stop;

} // namespace pcd
