#include "voxel_io.h"

#include <cstring>
#include <fstream>

namespace vox {

namespace {

uint32_t read_u32(std::ifstream& f) {
  unsigned char b[4];
  f.read(reinterpret_cast<char*>(b), 4);
  return uint32_t(b[0]) | (uint32_t(b[1]) << 8) |
         (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
}

} // namespace

Snapshot load_snapshot(const std::string& path,
                       const std::string& object_id) {
  std::ifstream f(path, std::ios::binary);
  if (!f) throw SlicerError("cannot open snapshot: " + path);

  Snapshot snap;
  snap.object_id = object_id;
  snap.source_path = path;

  uint32_t magic = read_u32(f);
  if (!f) throw SlicerError("unreadable magic in: " + path);
  if (magic == kMagicVox1) {
    snap.version = FormatVersion::LegacyV1;
  } else if (magic == kMagicVox2) {
    snap.version = FormatVersion::ModernV2;
  } else {
    throw SlicerError("unknown magic 0x" + [&] {
      char buf[9];
      std::snprintf(buf, sizeof(buf), "%08x", magic);
      return std::string(buf);
    }() + " in: " + path);
  }

  snap.dims.x = read_u32(f);
  snap.dims.y = read_u32(f);
  snap.dims.z = read_u32(f);
  if (!f || snap.dims.x == 0 || snap.dims.y == 0 || snap.dims.z == 0) {
    throw SlicerError("invalid dimensions in: " + path);
  }

  uint32_t metadata_len = 0;
  if (snap.version == FormatVersion::ModernV2) {
    metadata_len = read_u32(f);
    if (!f) throw SlicerError("truncated VOX2 metadata header in: " + path);
    // Metadata is evidence-adjacent but not needed for rendering: skip it,
    // remembering how much the record promised so truncation stays visible.
    f.seekg(metadata_len, std::ios::cur);
    if (!f) f.clear(); // short metadata will surface as payload truncation
  }

  const uint64_t cells = snap.dims.cell_count();
  snap.expected_bytes = cells;
  snap.voxels.resize(static_cast<size_t>(cells));
  f.read(reinterpret_cast<char*>(snap.voxels.data()),
         static_cast<std::streamsize>(cells));
  snap.observed_bytes = static_cast<uint64_t>(f.gcount());

  if (snap.observed_bytes < snap.expected_bytes) {
    // Keep the zero-filled tail explicit: a missing cell renders as value 0,
    // and the truncation flag drives evidence + review reporting.
    snap.truncated = true;
  }
  return snap;
}

void normalize_layout(Snapshot& snap) {
  if (snap.version != FormatVersion::ModernV2 || snap.truncated) {
    // VOX1 is already canonical. A truncated VOX2 cannot be permuted safely
    // because bytes past the cut are absent; render it in-place instead.
    return;
  }
  const Dim3 d = snap.dims;
  std::vector<uint8_t> out(snap.voxels.size());
  for (uint32_t y = 0; y < d.y; ++y) {
    for (uint32_t z = 0; z < d.z; ++z) {
      for (uint32_t x = 0; x < d.x; ++x) {
        uint64_t src =
            uint64_t(x) + uint64_t(d.x) * (uint64_t(z) + uint64_t(d.z) * y);
        out[voxel_index(d, x, y, z)] = snap.voxels[static_cast<size_t>(src)];
      }
    }
  }
  snap.voxels.swap(out);
}

} // namespace vox
