#include "material.hpp"

#include <cmath>
#include <cstring>
#include <fstream>

#include "util.hpp"

namespace nmts {
namespace {

struct Vec3 {
  float x, y, z;
};

float dot(Vec3 a, Vec3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

float length(Vec3 a) { return std::sqrt(dot(a, a)); }

Vec3 normalize(Vec3 a) {
  float len = length(a);
  if (len <= 0.0f) return {0.0f, 0.0f, 0.0f};
  return {a.x / len, a.y / len, a.z / len};
}

void put_pixel(std::vector<std::uint8_t>& px, std::uint32_t idx,
               std::uint8_t r, std::uint8_t g, std::uint8_t b,
               std::uint8_t a = 255) {
  std::size_t off = std::size_t(idx) * 4;
  px[off] = r;
  px[off + 1] = g;
  px[off + 2] = b;
  px[off + 3] = a;
}

// Builds the 8 instance records for one extreme-parameter case.
void build_case(std::uint32_t case_id, std::uint32_t repeats,
                std::vector<InstanceRecord>& out) {
  for (std::uint32_t r = 0; r < repeats; ++r) {
    InstanceRecord rec{};
    rec.case_id = case_id;
    rec.repeat_index = r;
    rec.reserved = 0;
    Vec3 t{1, 0, 0}, b{0, 1, 0}, n{0, 0, 1};

    switch (case_id) {
      case 0: {  // canonical, valid frame; even repeats flip the green channel
        rec.pixel_index = r;
        rec.green_flip = (r % 2u);
        break;
      }
      case 1: {  // non-orthogonal tangent: T tilted 30 deg towards B
        t = normalize({1.0f, 0.5f, 0.0f});
        rec.pixel_index = 8 + r;
        rec.green_flip = 0;
        break;
      }
      case 2: {  // non-unit tangent, length scales with repeat index
        float s = 1.25f + 0.25f * float(r);
        t = {s, 0.0f, 0.0f};
        rec.pixel_index = 16 + r;
        rec.green_flip = 0;
        break;
      }
      case 3: {  // inverted handedness, negative scaling on B
        b = {0.0f, -1.0f, 0.0f};
        rec.pixel_index = 24 + r;
        rec.green_flip = 0;
        break;
      }
      case 4: {  // degenerate frame: T and B parallel; pixel normal z <= 0
        t = {1.0f, 0.0f, 0.0f};
        b = {0.5f, 0.0f, 0.0f};
        n = {0.0f, 0.0f, 1.0f};
        rec.pixel_index = 32 + r;
        // Unit-length quaternion normal (1/2, sqrt(3)/2, 0); flipping green
        // makes the reconstructed vector over-long, exercising check 6.
        rec.green_flip = 1;
        break;
      }
    }
    std::memcpy(rec.tangent, &t, sizeof(t));
    std::memcpy(rec.bitangent, &b, sizeof(b));
    std::memcpy(rec.normal_geo, &n, sizeof(n));
    out.push_back(rec);
  }
}

}  // namespace

const char* case_name(std::uint32_t case_id) {
  switch (case_id) {
    case 0: return "canonical_valid_green_flip";
    case 1: return "non_orthogonal_tangent_30deg";
    case 2: return "non_unit_tangent_scaled";
    case 3: return "inverted_handedness";
    case 4: return "degenerate_parallel_frame";
    default: return "unknown";
  }
}

Material generate_material(const GenOptions& opt) {
  Material m;
  std::uint32_t pixel_count = opt.width * opt.height;
  std::uint32_t instance_count = kCaseCount * opt.repeats;
  m.header = {};
  std::memcpy(m.header.magic, kMagic.data(), 4);
  m.header.version = kFormatVersion;
  m.header.width = opt.width;
  m.header.height = opt.height;
  m.header.instance_count = instance_count;
  m.header.pixel_offset = kHeaderSize;
  m.header.instance_offset =
      kHeaderSize + pixel_count * kPixelSize;
  m.header.eps_orthogonal = opt.eps_orthogonal;
  m.header.eps_unit = opt.eps_unit;
  m.header.eps_normal_z = opt.eps_normal_z;

  m.pixels.assign(std::size_t(pixel_count) * kPixelSize, 0);
  // Case 0: flat (128,128,255); green-flip instances use G=127.
  for (std::uint32_t r = 0; r < opt.repeats; ++r) {
    std::uint8_t g = (r % 2u) ? 127 : 128;
    put_pixel(m.pixels, r, 128, g, 255);
  }
  // Case 1/2: moderately perturbed normal.
  for (std::uint32_t r = 0; r < opt.repeats; ++r) {
    put_pixel(m.pixels, 8 + r, 140, 116, 250);
    put_pixel(m.pixels, 16 + r, 116, 140, 250);
  }
  // Case 3: valid tangent-space normal, failure is frame-side only.
  for (std::uint32_t r = 0; r < opt.repeats; ++r)
    put_pixel(m.pixels, 24 + r, 128, 128, 255);
  // Case 4: raw normal points backwards/down (z <= 0); stored tangent normal
  // is unit length (nx=0.5, ny=sqrt(3)/2) so green-flip breaks check 6.
  for (std::uint32_t r = 0; r < opt.repeats; ++r)
    put_pixel(m.pixels, 32 + r, 191, 238, 0);

  m.instances.reserve(instance_count);
  for (std::uint32_t c = 0; c < kCaseCount; ++c)
    build_case(c, opt.repeats, m.instances);
  return m;
}

bool write_material(const std::string& path, const Material& m) {
  std::vector<std::uint8_t> buf;
  buf.resize(m.header.instance_offset +
             m.header.instance_count * kInstanceSize);
  std::memcpy(buf.data(), &m.header, kHeaderSize);
  std::memcpy(buf.data() + m.header.pixel_offset, m.pixels.data(),
              m.pixels.size());
  std::memcpy(buf.data() + m.header.instance_offset,
              m.instances.data(),
              m.instances.size() * kInstanceSize);
  return write_atomic(path, buf);
}

bool load_material(const std::string& path, Material& out, std::string& err) {
  std::vector<std::uint8_t> raw;
  if (!read_file(path, raw)) {
    err = "cannot read material file";
    return false;
  }
  if (raw.size() < kHeaderSize) {
    err = "file smaller than 40-byte header";
    return false;
  }
  MaterialHeader h{};
  std::memcpy(&h, raw.data(), kHeaderSize);
  if (std::memcmp(h.magic, kMagic.data(), 4) != 0) {
    err = "magic mismatch (expected NMTS)";
    return false;
  }
  if (h.version != kFormatVersion) {
    err = "unsupported format version";
    return false;
  }
  if (h.pixel_offset != kHeaderSize ||
      h.instance_offset !=
          h.pixel_offset + h.width * h.height * kPixelSize) {
    err = "section offset invariants violated";
    return false;
  }
  std::uint64_t need = std::uint64_t(h.instance_offset) +
                       std::uint64_t(h.instance_count) * kInstanceSize;
  if (need != raw.size()) {
    err = "file length does not match declared sections";
    return false;
  }
  out.header = h;
  out.source_path = path;
  out.pixels.assign(raw.begin() + h.pixel_offset,
                    raw.begin() + h.instance_offset);
  out.instances.resize(h.instance_count);
  std::memcpy(out.instances.data(), raw.data() + h.instance_offset,
              std::size_t(h.instance_count) * kInstanceSize);
  return true;
}

}  // namespace nmts
