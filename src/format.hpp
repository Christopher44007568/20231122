#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace msphere {

constexpr uint64_t kMaxInputBytes = 8ULL * 1024ULL * 1024ULL;
constexpr uint32_t kMaxMaterials = 64;
constexpr uint32_t kMaxReflectionWidth = 512;
constexpr uint32_t kMaxReflectionHeight = 256;
constexpr uint64_t kHeaderSize = 64;
constexpr uint64_t kMaterialSize = 32;
constexpr uint64_t kEnergySize = 4;

struct Material {
  float base_color[3] = {0.0F, 0.0F, 0.0F};
  float roughness = 0.0F;
  float metallic = 0.0F;
  float reflectance = 0.0F;
  float albedo_scale = 1.0F;
  float energy_integral = 0.0F;
  uint32_t flags = 0;
};

enum class InputErrorCode {
  none,
  io,
  empty,
  too_large,
  truncated,
  bad_magic,
  bad_version,
  bad_header_size,
  bad_material_count,
  bad_reflection_extent,
  bad_offset,
  bad_file_size,
  bad_material_field,
  bad_energy_field,
};

struct ValidationError {
  InputErrorCode code = InputErrorCode::none;
  std::string field;
  std::string detail;
  uint64_t raw_offset = 0;
  uint64_t observed = 0;
  uint64_t limit = 0;

  explicit operator bool() const { return code != InputErrorCode::none; }
};

struct ParsedInput {
  std::filesystem::path source_path;
  std::vector<uint8_t> bytes;
  uint32_t flags = 0;
  uint32_t material_count = 0;
  uint32_t reflection_width = 0;
  uint32_t reflection_height = 0;
  uint64_t material_offset = 0;
  uint64_t reflection_offset = 0;
  uint64_t energy_offset = 0;
  uint64_t file_size = 0;
  std::vector<Material> materials;
  std::vector<uint8_t> reflection_rgba;
  std::vector<float> energy_integrals;
};

struct LoadResult {
  ParsedInput input;
  ValidationError error;
};

LoadResult load_and_validate(const std::filesystem::path& path,
                             uint64_t max_bytes = kMaxInputBytes);

ValidationError validate_bytes(const uint8_t* data, uint64_t size,
                               uint64_t declared_file_size, bool allow_empty,
                               ParsedInput* out);

std::string input_error_code_name(InputErrorCode code);

}  // namespace msphere
