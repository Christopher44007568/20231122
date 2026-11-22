#pragma once

#include "common.h"
#include <string>

namespace vox {

// On-disk layouts (little-endian, all integers packed, no padding):
//
// VOX1 (legacy):
//   u32 magic = "VOX1"
//   u32 dim_x, u32 dim_y, u32 dim_z
//   u8  voxel[dim_x * dim_y * dim_z]   (one byte per cell)
//
// VOX2 (modern):
//   u32 magic = "VOX2"
//   u32 dim_x, u32 dim_y, u32 dim_z
//   u32 metadata_length
//   u8  metadata[metadata_length]      (UTF-8, may contain NULs)
//   u8  voxel[dim_x * dim_z * dim_y]   (Y-major: x fastest, then z, then y)
//
// A truncated record (payload shorter than the header promises) does not
// throw: the Snapshot is returned with truncated=true so the caller can keep
// processing the remaining objects. Structural failures (missing file, bad
// magic, unreadable header) throw SlicerError.

Snapshot load_snapshot(const std::string& path, const std::string& object_id);

// VOX2 stores y-major; normalize both versions to the canonical x,y,z order
// (index = x + dims.x * (y + dims.y * z)) before rendering/diffing.
void normalize_layout(Snapshot& snap);

// Linear index into a normalized voxel buffer.
inline uint64_t voxel_index(const Dim3& d, uint32_t x, uint32_t y, uint32_t z) {
  return uint64_t(x) + uint64_t(d.x) * (uint64_t(y) + uint64_t(d.y) * z);
}

} // namespace vox
