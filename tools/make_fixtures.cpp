// Generates the review fixtures under data/input:
//   obj_alpha/  complete VOX1 + VOX2 pair (diverging content)
//   obj_beta/   pair whose VOX2 record is truncated mid-payload
//   obj_gamma/  pair with a hollow/hole pattern
//   obj_delta/  VOX1 present, VOX2 replaced by a corrupt-magic file
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {
using std::string;
using std::vector;

constexpr uint32_t W = 8, H = 6, D = 4;

void put_u32(std::ofstream& f, uint32_t v) {
  char b[4] = {char(v), char(v >> 8), char(v >> 16), char(v >> 24)};
  f.write(b, 4);
}

void write_v1(const string& path, const vector<uint8_t>& vox) {
  std::ofstream f(path, std::ios::binary);
  put_u32(f, 0x31584F56u);
  put_u32(f, W);
  put_u32(f, H);
  put_u32(f, D);
  f.write(reinterpret_cast<const char*>(vox.data()),
          static_cast<std::streamsize>(vox.size()));
}

void write_v2(const string& path, const vector<uint8_t>& vox,
              const string& meta, bool truncate) {
  std::ofstream f(path, std::ios::binary);
  put_u32(f, 0x32584F56u);
  put_u32(f, W);
  put_u32(f, H);
  put_u32(f, D);
  put_u32(f, static_cast<uint32_t>(meta.size()));
  f.write(meta.data(), static_cast<std::streamsize>(meta.size()));
  // VOX2 stores y-major: index = x + W*(z + D*y)
  vector<uint8_t> ymajor(vox.size());
  for (uint32_t y = 0; y < H; ++y)
    for (uint32_t z = 0; z < D; ++z)
      for (uint32_t x = 0; x < W; ++x) {
        uint64_t canon = uint64_t(x) + W * (uint64_t(y) + H * z);
        uint64_t src = uint64_t(x) + W * (uint64_t(z) + D * y);
        ymajor[src] = vox[canon];
      }
  size_t bytes = truncate ? ymajor.size() - 13 : ymajor.size();
  f.write(reinterpret_cast<const char*>(ymajor.data()),
          static_cast<std::streamsize>(bytes));
}

uint64_t idx(uint32_t x, uint32_t y, uint32_t z) {
  return uint64_t(x) + W * (uint64_t(y) + H * z);
}

vector<uint8_t> make_alpha(bool v2) {
  vector<uint8_t> v(W * H * D, 0);
  for (uint32_t z = 0; z < D; ++z)
    for (uint32_t y = 0; y < H; ++y)
      for (uint32_t x = 0; x < W; ++x)
        v[idx(x, y, z)] = v2 ? ((x + y + z) % 8) + 1 : ((x + y) % 5) + 1;
  return v;
}

vector<uint8_t> make_beta(bool v2) {
  vector<uint8_t> v(W * H * D, 0);
  for (uint32_t z = 0; z < D; ++z)
    for (uint32_t y = 1; y < H - 1; ++y)
      for (uint32_t x = 1; x < W - 1; ++x)
        v[idx(x, y, z)] = v2 ? 7 - ((x + z) % 4) : 2 + ((x) % 3);
  return v;
}

vector<uint8_t> make_gamma(bool v2) {
  vector<uint8_t> v(W * H * D, 0);
  for (uint32_t z = 0; z < D; ++z)
    for (uint32_t y = 0; y < H; ++y)
      for (uint32_t x = 0; x < W; ++x) {
        bool shell = (x == 0 || x == W - 1 || y == 0 || y == H - 1);
        v[idx(x, y, z)] = shell ? (v2 ? 6u : 3u) : 0;
      }
  return v;
}

} // namespace

int main(int argc, char** argv) {
  string out = argc > 1 ? argv[1] : "data/input";
  const string dirs[] = {"obj_alpha", "obj_beta", "obj_gamma", "obj_delta"};
  for (const auto& d : dirs)
    if (std::system(("mkdir -p " + out + "/" + d).c_str()) != 0)
      return 2;

  write_v1(out + "/obj_alpha/snapshot.vox1", make_alpha(false));
  write_v2(out + "/obj_alpha/snapshot.vox2", make_alpha(true),
           "alpha modern snapshot", false);
  write_v1(out + "/obj_beta/snapshot.vox1", make_beta(false));
  write_v2(out + "/obj_beta/snapshot.vox2", make_beta(true),
           "beta truncated on purpose", true);
  write_v1(out + "/obj_gamma/snapshot.vox1", make_gamma(false));
  write_v2(out + "/obj_gamma/snapshot.vox2", make_gamma(true),
           "gamma shell pattern", false);
  write_v1(out + "/obj_delta/snapshot.vox1", make_gamma(true));
  {
    std::ofstream bad(out + "/obj_delta/snapshot.vox2", std::ios::binary);
    put_u32(bad, 0xDEADBEEFu); // corrupt magic -> structural error
    put_u32(bad, W);
    put_u32(bad, H);
    put_u32(bad, D);
  }

  std::ofstream manifest(out + "/manifest.txt");
  manifest << "obj_alpha\nsnapshot.vox1\nsnapshot.vox2\n"
           << "obj_beta\nsnapshot.vox1\nsnapshot.vox2\n"
           << "obj_gamma\nsnapshot.vox1\nsnapshot.vox2\n"
           << "obj_delta\nsnapshot.vox1\nsnapshot.vox2\n";
  std::printf("fixtures written to %s\n", out.c_str());
  return 0;
}
