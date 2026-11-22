#pragma once

#include <string>

#include "common.hpp"

namespace nmts {

constexpr std::uint32_t kCaseCount = 5;

struct GenOptions {
  std::uint32_t repeats = kRepeatCount;
  std::uint32_t width = 8;
  std::uint32_t height = 8;
  float eps_orthogonal = 1e-4f;
  float eps_unit = 1e-4f;
  float eps_normal_z = 1e-6f;
};

// Builds the deterministic extreme-parameter material entirely in memory.
Material generate_material(const GenOptions& opt);

// Writes the packed little-endian material file.
bool write_material(const std::string& path, const Material& m);

// Loads and validates structural invariants; never mutates the bytes.
bool load_material(const std::string& path, Material& out, std::string& err);

const char* case_name(std::uint32_t case_id);

}  // namespace nmts
