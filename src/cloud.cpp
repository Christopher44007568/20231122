#include "cloud.h"
#include "util.h"

#include <cmath>
#include <cstring>
#include <random>

namespace pcd {

static const char kMagic[8] = {'P','C','D','C','L','P','1','\0'};

Cloud load_cloud(const std::string& path) {
  auto bytes = read_file(path);
  if (bytes.size() < sizeof(CloudHeader))
    die("not a pcdclip cloud (too small): " + path);
  CloudHeader h{};
  std::memcpy(&h, bytes.data(), sizeof(h));
  if (std::memcmp(h.magic, kMagic, 8) != 0)
    die("not a pcdclip cloud (bad magic): " + path);
  uint64_t need = sizeof(CloudHeader) + h.count * sizeof(Point);
  if (bytes.size() < need)
    die("truncated cloud: " + path);
  Cloud c;
  c.points.resize(static_cast<size_t>(h.count));
  std::memcpy(c.points.data(), bytes.data() + sizeof(CloudHeader),
              h.count * sizeof(Point));
  c.original_count = h.count;
  return c;
}

void save_cloud(const std::string& path, const Cloud& cloud) {
  CloudHeader h{};
  std::memcpy(h.magic, kMagic, 8);
  h.count = cloud.points.size();
  h.reserved = 0;
  std::string tmp = path + ".tmp";
  {
    std::vector<uint8_t> head(sizeof(CloudHeader));
    std::memcpy(head.data(), &h, sizeof(h));
    write_file(tmp, head.data(), head.size());
  }
  // Append points after the header.
  {
    FILE* f = std::fopen(tmp.c_str(), "ab");
    if (!f) die("cannot write: " + tmp);
    if (!cloud.points.empty())
      std::fwrite(cloud.points.data(), sizeof(Point),
                  cloud.points.size(), f);
    std::fclose(f);
  }
  if (std::rename(tmp.c_str(), path.c_str()) != 0)
    die("rename failed for: " + path);
}

Cloud generate_cloud(uint64_t n, uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> u(0.0f, 1.0f);
  std::normal_distribution<float> gauss(0.0f, 1.0f);
  Cloud c;
  c.points.reserve(static_cast<size_t>(n));
  // 90% uniform background, 10% planted anomalies.
  uint64_t anomalies = n / 10;
  uint64_t background = n - anomalies;
  for (uint64_t i = 0; i < background; ++i) {
    Point p{u(rng) * 100.0f, u(rng) * 100.0f, u(rng) * 100.0f, 0};
    c.points.push_back(p);
  }
  // Anomaly A: hyper-dense cluster (should be clipped by max-density rules).
  for (uint64_t i = 0; i < anomalies / 2; ++i) {
    Point p{50.0f + gauss(rng) * 0.05f, 50.0f + gauss(rng) * 0.05f,
            50.0f + gauss(rng) * 0.05f, 0};
    c.points.push_back(p);
  }
  // Anomaly B: sparse outliers far from everything (min-density rules).
  for (uint64_t i = anomalies / 2; i < anomalies; ++i) {
    Point p{u(rng) * 1000.0f - 500.0f, u(rng) * 1000.0f - 500.0f,
            u(rng) * 1000.0f - 500.0f, 0};
    c.points.push_back(p);
  }
  c.original_count = c.points.size();
  return c;
}

void cloud_bounds(const Cloud& c, float lo[3], float hi[3]) {
  lo[0] = lo[1] = lo[2] = 1e30f;
  hi[0] = hi[1] = hi[2] = -1e30f;
  for (const auto& p : c.points) {
    lo[0] = std::min(lo[0], p.x); hi[0] = std::max(hi[0], p.x);
    lo[1] = std::min(lo[1], p.y); hi[1] = std::max(hi[1], p.y);
    lo[2] = std::min(lo[2], p.z); hi[2] = std::max(hi[2], p.z);
  }
}

} // namespace pcd
