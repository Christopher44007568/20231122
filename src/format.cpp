#include "format.hpp"

#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace msphere {
namespace {

constexpr uint8_t kMagic[8] = {'M', 'S', 'P', 'H', 'E', 'R', 'E', '1'};
constexpr uint16_t kVersion = 1;

uint16_t read_u16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8U);
}

uint32_t read_u32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8U) |
         (static_cast<uint32_t>(p[2]) << 16U) |
         (static_cast<uint32_t>(p[3]) << 24U);
}

uint64_t read_u64(const uint8_t* p) {
  return static_cast<uint64_t>(read_u32(p)) |
         (static_cast<uint64_t>(read_u32(p + 4)) << 32U);
}

float read_f32(const uint8_t* p) {
  const uint32_t bits = read_u32(p);
  float value = 0.0F;
  static_assert(sizeof(value) == sizeof(bits), "unexpected float width");
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

bool add_overflow(uint64_t a, uint64_t b, uint64_t* result) {
  if (b > std::numeric_limits<uint64_t>::max() - a) {
    return true;
  }
  *result = a + b;
  return false;
}

ValidationError make_error(InputErrorCode code, std::string field,
                           std::string detail, uint64_t raw_offset,
                           uint64_t observed = 0, uint64_t limit = 0) {
  ValidationError error;
  error.code = code;
  error.field = std::move(field);
  error.detail = std::move(detail);
  error.raw_offset = raw_offset;
  error.observed = observed;
  error.limit = limit;
  return error;
}

bool finite_unit(float value) {
  return std::isfinite(value) && value >= 0.0F && value <= 1.0F;
}

}  // namespace

std::string input_error_code_name(InputErrorCode code) {
  switch (code) {
    case InputErrorCode::none:
      return "none";
    case InputErrorCode::io:
      return "io";
    case InputErrorCode::empty:
      return "empty";
    case InputErrorCode::too_large:
      return "too_large";
    case InputErrorCode::truncated:
      return "truncated";
    case InputErrorCode::bad_magic:
      return "bad_magic";
    case InputErrorCode::bad_version:
      return "bad_version";
    case InputErrorCode::bad_header_size:
      return "bad_header_size";
    case InputErrorCode::bad_material_count:
      return "bad_material_count";
    case InputErrorCode::bad_reflection_extent:
      return "bad_reflection_extent";
    case InputErrorCode::bad_offset:
      return "bad_offset";
    case InputErrorCode::bad_file_size:
      return "bad_file_size";
    case InputErrorCode::bad_material_field:
      return "bad_material_field";
    case InputErrorCode::bad_energy_field:
      return "bad_energy_field";
  }
  return "unknown";
}

ValidationError validate_bytes(const uint8_t* data, uint64_t size,
                               uint64_t declared_file_size, bool allow_empty,
                               ParsedInput* out) {
  if (size == 0) {
    if (allow_empty) {
      if (out != nullptr) {
        out->bytes.clear();
        out->file_size = 0;
      }
      return {};
    }
    return make_error(InputErrorCode::empty, "input", "input is empty", 0, 0,
                      kHeaderSize);
  }
  if (size < kHeaderSize) {
    return make_error(InputErrorCode::truncated, "header",
                      "input shorter than the 64-byte header", size, size,
                      kHeaderSize);
  }
  if (std::memcmp(data, kMagic, sizeof(kMagic)) != 0) {
    return make_error(InputErrorCode::bad_magic, "magic",
                      "expected MSPHERE1", 0);
  }
  const uint16_t version = read_u16(data + 8);
  if (version != kVersion) {
    return make_error(InputErrorCode::bad_version, "version",
                      "only format version 1 is accepted", 8, version,
                      kVersion);
  }
  const uint16_t header_size = read_u16(data + 10);
  if (header_size != kHeaderSize) {
    return make_error(InputErrorCode::bad_header_size, "header_size",
                      "header_size must be 64", 10, header_size, kHeaderSize);
  }

  const uint32_t flags = read_u32(data + 12);
  const uint32_t material_count = read_u32(data + 16);
  const uint32_t reflection_width = read_u32(data + 20);
  const uint32_t reflection_height = read_u32(data + 24);
  const uint64_t material_offset = read_u64(data + 32);
  const uint64_t reflection_offset = read_u64(data + 40);
  const uint64_t energy_offset = read_u64(data + 48);
  const uint64_t file_size = read_u64(data + 56);

  if (material_count == 0 || material_count > kMaxMaterials) {
    return make_error(InputErrorCode::bad_material_count, "material_count",
                      "material_count must be in [1, 64]", 16,
                      material_count, kMaxMaterials);
  }
  if (reflection_width == 0 || reflection_width > kMaxReflectionWidth ||
      reflection_height == 0 || reflection_height > kMaxReflectionHeight) {
    return make_error(InputErrorCode::bad_reflection_extent,
                      "reflection_extent",
                      "reflection extent must be in [1,512] x [1,256]", 20,
                      static_cast<uint64_t>(reflection_width) *
                          static_cast<uint64_t>(reflection_height),
                      static_cast<uint64_t>(kMaxReflectionWidth) *
                          static_cast<uint64_t>(kMaxReflectionHeight));
  }
  if (file_size != size || declared_file_size != size) {
    return make_error(InputErrorCode::bad_file_size, "file_size",
                      "declared file_size does not equal the actual byte count",
                      56, file_size, size);
  }

  uint64_t material_end = 0;
  uint64_t reflection_bytes = 0;
  uint64_t reflection_end = 0;
  uint64_t energy_end = 0;
  if (add_overflow(material_offset,
                   static_cast<uint64_t>(material_count) * kMaterialSize,
                   &material_end)) {
    return make_error(InputErrorCode::bad_offset, "material_range",
                      "material range overflows uint64", 32);
  }
  if (add_overflow(static_cast<uint64_t>(reflection_width) *
                       static_cast<uint64_t>(reflection_height),
                   0, &reflection_bytes) ||
      reflection_bytes > std::numeric_limits<uint64_t>::max() / 4U) {
    return make_error(InputErrorCode::bad_reflection_extent,
                      "reflection_extent", "reflection byte count overflows", 20);
  }
  reflection_bytes *= 4U;
  if (add_overflow(reflection_offset, reflection_bytes, &reflection_end) ||
      add_overflow(energy_offset,
                   static_cast<uint64_t>(material_count) * kEnergySize,
                   &energy_end)) {
    return make_error(InputErrorCode::bad_offset, "energy_range",
                      "region range overflows uint64", 48);
  }

  if (material_offset < kHeaderSize || material_end > size) {
    return make_error(InputErrorCode::bad_offset, "material_offset",
                      "material region is outside the file", 32,
                      material_offset, size);
  }
  if (reflection_offset < material_end || reflection_end > size) {
    return make_error(InputErrorCode::bad_offset, "reflection_offset",
                      "reflection region must follow materials and fit in file",
                      40, reflection_offset, size);
  }
  if (energy_offset < reflection_end || energy_end > size) {
    return make_error(InputErrorCode::bad_offset, "energy_offset",
                      "energy region must follow reflection and fit in file",
                      48, energy_offset, size);
  }
  if (energy_end != size) {
    return make_error(InputErrorCode::bad_offset, "energy_range",
                      "energy region must end at file_size", 48, energy_end,
                      size);
  }

  ParsedInput parsed;
  parsed.flags = flags;
  parsed.material_count = material_count;
  parsed.reflection_width = reflection_width;
  parsed.reflection_height = reflection_height;
  parsed.material_offset = material_offset;
  parsed.reflection_offset = reflection_offset;
  parsed.energy_offset = energy_offset;
  parsed.file_size = file_size;
  parsed.bytes.assign(data, data + size);
  parsed.materials.resize(material_count);
  parsed.energy_integrals.resize(material_count);
  parsed.reflection_rgba.assign(data + reflection_offset,
                                data + reflection_end);

  for (uint32_t i = 0; i < material_count; ++i) {
    const uint64_t base = material_offset + static_cast<uint64_t>(i) * kMaterialSize;
    Material& material = parsed.materials[i];
    material.base_color[0] = read_f32(data + base + 0);
    material.base_color[1] = read_f32(data + base + 4);
    material.base_color[2] = read_f32(data + base + 8);
    material.roughness = read_f32(data + base + 12);
    material.metallic = read_f32(data + base + 16);
    material.reflectance = read_f32(data + base + 20);
    material.albedo_scale = read_f32(data + base + 24);
    material.flags = read_u32(data + base + 28);

    for (int channel = 0; channel < 3; ++channel) {
      if (!finite_unit(material.base_color[channel])) {
        return make_error(InputErrorCode::bad_material_field, "base_color",
                          "base_color must be finite and in [0,1]",
                          base + static_cast<uint64_t>(channel) * 4U);
      }
    }
    if (!finite_unit(material.roughness)) {
      return make_error(InputErrorCode::bad_material_field, "roughness",
                        "roughness must be finite and in [0,1]", base + 12);
    }
    if (!finite_unit(material.metallic)) {
      return make_error(InputErrorCode::bad_material_field, "metallic",
                        "metallic must be finite and in [0,1]", base + 16);
    }
    if (!finite_unit(material.reflectance)) {
      return make_error(InputErrorCode::bad_material_field, "reflectance",
                        "reflectance must be finite and in [0,1]", base + 20);
    }
    if (!std::isfinite(material.albedo_scale) ||
        material.albedo_scale < 0.0F || material.albedo_scale > 8.0F) {
      return make_error(InputErrorCode::bad_material_field, "albedo_scale",
                        "albedo_scale must be finite and in [0,8]", base + 24);
    }
    if (material.flags != 0) {
      return make_error(InputErrorCode::bad_material_field, "flags",
                        "reserved material flags must be zero", base + 28,
                        material.flags, 0);
    }
  }

  for (uint32_t i = 0; i < material_count; ++i) {
    const uint64_t offset = energy_offset + static_cast<uint64_t>(i) * kEnergySize;
    const float energy = read_f32(data + offset);
    if (!std::isfinite(energy) || energy < 0.0F || energy > 4.0F) {
      return make_error(InputErrorCode::bad_energy_field, "energy_integral",
                        "energy_integral must be finite and in [0,4]", offset);
    }
    parsed.energy_integrals[i] = energy;
    parsed.materials[i].energy_integral = energy;
  }

  if (out != nullptr) {
    *out = std::move(parsed);
  }
  return {};
}

LoadResult load_and_validate(const std::filesystem::path& path,
                             uint64_t max_bytes) {
  LoadResult result;
  result.input.source_path = path;
  std::error_code ec;
  const uint64_t file_size = std::filesystem::file_size(path, ec);
  if (ec) {
    result.error = make_error(InputErrorCode::io, "input",
                              "cannot stat input: " + ec.message(), 0);
    return result;
  }
  if (file_size == 0) {
    result.error = make_error(InputErrorCode::empty, "input",
                              "input is empty", 0, 0, kHeaderSize);
    return result;
  }
  if (file_size > max_bytes) {
    result.error = make_error(InputErrorCode::too_large, "input",
                              "input exceeds the configured byte limit",
                              max_bytes, file_size, max_bytes);
    return result;
  }

  std::vector<uint8_t> bytes(static_cast<size_t>(file_size));
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    result.error = make_error(InputErrorCode::io, "input",
                              "cannot open input for reading", 0);
    return result;
  }
  stream.read(reinterpret_cast<char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream || static_cast<uint64_t>(stream.gcount()) != file_size) {
    result.error = make_error(InputErrorCode::io, "input",
                              "short read while loading input", 0);
    return result;
  }

  result.error = validate_bytes(bytes.data(), file_size, file_size, false,
                                &result.input);
  result.input.source_path = path;
  return result;
}

}  // namespace msphere
