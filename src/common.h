#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace vox {

constexpr uint32_t kMagicVox1 = 0x31584F56u; // "VOX1" little-endian
constexpr uint32_t kMagicVox2 = 0x32584F56u; // "VOX2"
constexpr uint32_t kTruncatedSentinel = 0xFFFFFFFFu;

enum class FormatVersion : uint8_t { LegacyV1 = 1, ModernV2 = 2 };

struct Dim3 {
  uint32_t x = 0;
  uint32_t y = 0;
  uint32_t z = 0;
  uint64_t cell_count() const {
    return uint64_t(x) * uint64_t(y) * uint64_t(z);
  }
};

// One voxel snapshot (one version of one object).
struct Snapshot {
  std::string object_id;
  FormatVersion version = FormatVersion::LegacyV1;
  Dim3 dims{};
  std::vector<uint8_t> voxels; // length == dims.cell_count() when complete
  bool truncated = false;      // the on-disk record was cut short
  uint64_t expected_bytes = 0; // payload bytes the header promised
  uint64_t observed_bytes = 0; // payload bytes actually present
  std::string source_path;
};

// Pair of snapshots that the review pipeline compares.
struct SnapshotPair {
  std::string object_id;
  Snapshot legacy; // VOX1 (old format)
  Snapshot modern; // VOX2 (new format)
};

class SlicerError : public std::runtime_error {
public:
  explicit SlicerError(const std::string& msg) : std::runtime_error(msg) {}
};

inline std::string version_name(FormatVersion v) {
  return v == FormatVersion::ModernV2 ? "VOX2" : "VOX1";
}

} // namespace vox
