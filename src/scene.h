#pragma once
#include "trace.h"

namespace rpsc {

// Deterministic demo scene: 3 pipeline states, 6 draws in one frame.
// The recorder executes this and injects Checkpoint records (v2) carrying
// the golden framebuffer/state hashes observed during recording.
std::vector<Record> buildScene();

// Index (into the scene record list) of the BindPipeline record preceding
// the 4th draw — the default truncation point used by the review flow.
size_t sceneTruncationRecordIndex();

}  // namespace rpsc
