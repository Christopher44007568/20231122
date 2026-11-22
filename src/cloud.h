#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pcd {

// 16-byte point record. The byte offset of point i in the original
// material is exactly i * 16; this anchors acceptance to original offsets.
struct Point {
  float x, y, z;
  uint32_t flags; // bit0: clipped by correction
};
static_assert(sizeof(Point) == 16, "point record must stay 16 bytes");

constexpr uint32_t kFlagClipped = 1u;
constexpr uint64_t kPointStride = sizeof(Point);

struct CloudHeader {
  char magic[8];      // "PCDCLP1\0"
  uint64_t count;
  uint64_t reserved;
};
static_assert(sizeof(CloudHeader) == 24, "header layout");

struct Cloud {
  std::vector<Point> points;
  uint64_t original_count = 0; // count in the original material
};

// Binary cloud IO (header + packed Point array).
Cloud load_cloud(const std::string& path);
void save_cloud(const std::string& path, const Cloud& cloud);

// Synthetic dataset with planted density anomalies (extreme regions).
Cloud generate_cloud(uint64_t n, uint32_t seed);

// Bounding box.
void cloud_bounds(const Cloud& c, float lo[3], float hi[3]);

} // namespace pcd
