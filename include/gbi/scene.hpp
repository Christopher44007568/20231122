#pragma once

#include "gbi/json.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace gbi {

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct Mat4 {
  std::array<float, 16> data{};

  static Mat4 identity();
  static Mat4 translation(const Vec3& value);
  static Mat4 rotationX(float radians);
  static Mat4 rotationY(float radians);
  static Mat4 rotationZ(float radians);
  static Mat4 scale(const Vec3& value);
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up);
  static Mat4 orthographic(float left, float right, float bottom, float top,
                           float near_plane, float far_plane);
  static Mat4 perspective(float vertical_fov_radians, float aspect,
                          float near_plane, float far_plane);

  Mat4 operator*(const Mat4& other) const;
  const float* dataPtr() const;
};

struct Vertex {
  float position[3];
  float normal[3];
};

struct Mesh {
  std::vector<Vertex> vertices;
  std::uint32_t first = 0;
  std::uint32_t count = 0;
};

struct SourceRange {
  std::uint64_t offset = 0;
  std::uint64_t length = 0;
};

struct SceneObject {
  std::string id;
  std::string shape;
  Vec3 position;
  Vec3 rotation_deg;
  Vec3 scale;
  Vec3 albedo;
  float roughness = 0.5F;
  float metallic = 0.0F;
  SourceRange source_range;
  bool corrected = false;
  std::string overlay_path;
  std::string overlay_sha256;
  SourceRange overlay_albedo_range;
  SourceRange overlay_roughness_range;
  SourceRange overlay_metallic_range;
};

struct Camera {
  Vec3 eye{0.0F, 0.0F, 2.6F};
  Vec3 target{0.0F, 0.0F, 0.0F};
  Vec3 up{0.0F, 1.0F, 0.0F};
  float scale_x = 1.85F;
  float scale_y = 1.15F;
  float near_plane = 0.1F;
  float far_plane = 8.0F;
};

struct Scene {
  std::string name;
  std::filesystem::path source_path;
  std::string source_text;
  std::string source_sha256;
  Camera camera;
  std::vector<SceneObject> objects;

  static Scene load(const std::filesystem::path& path);
  const SceneObject* find(const std::string& id) const;
  SceneObject* find(const std::string& id);
  std::optional<std::size_t> indexOf(const std::string& id) const;
  void applyOverlay(const std::string& object_id, const Json& overlay,
                    const std::filesystem::path& overlay_path,
                    const std::string& overlay_text);
  std::string fingerprint() const;
};

struct ObjectOverlay {
  std::string object_id;
  bool has_albedo = false;
  bool has_roughness = false;
  bool has_metallic = false;
  Vec3 albedo;
  float roughness = 0.0F;
  float metallic = 0.0F;
  std::filesystem::path path;
  std::string text;
};

ObjectOverlay parseOverlay(const std::string& object_id,
                           const std::filesystem::path& path);
Json overlayToJson(const ObjectOverlay& overlay);

std::vector<Vertex> makeCube();
std::vector<Vertex> makePrism();
SourceRange findObjectRange(const std::string& text,
                            const std::string& object_id);
SourceRange findFieldRange(const std::string& text, const std::string& field,
                           std::size_t start = 0);

}  // namespace gbi
