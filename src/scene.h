#pragma once

#include "mathutil.h"

#include <cstdint>
#include <string>
#include <vector>

// A material carries a version counter: any edit (albedo, alpha-cutout
// pattern, displacement, ...) bumps the version, which is what the shadow
// cache invalidation tracker keys on.
struct Material {
  std::string name;
  uint32_t version = 0;
  bool castsShadow = true;
};

struct SceneObject {
  uint32_t id = 0;
  std::string name;
  int materialId = 0;
  Vec3 position{0, 0, 0};
  float rotationY = 0.0f;
  Vec3 scale{1, 1, 1};
  uint32_t transformVersion = 0; // bumped by move()/rotate()

  Mat4 modelMatrix() const;
  float boundingRadius() const; // world-space bounding sphere radius
};

enum class LightType { Directional, Spot };

struct CascadeConfig {
  // Directional: ortho box half-extent per cascade around the focus point.
  // Spot: far-plane per cascade (near is the previous split, 0.5 first).
  std::vector<float> splits;
  uint32_t resolution = 256;
};

struct Light {
  std::string name;
  LightType type = LightType::Directional;
  Vec3 position{0, 10, 0};  // spot only
  Vec3 direction{0, -1, 0}; // normalized on use
  float fovDeg = 60.0f;     // spot only
  Vec3 focus{0, 0, 0};      // directional cascade focus
  CascadeConfig cascade;
  uint32_t transformVersion = 0; // pos/dir/fov edits
  uint32_t cascadeVersion = 0;   // split/resolution edits
};

struct Scene {
  std::vector<Material> materials;
  std::vector<SceneObject> objects;
  std::vector<Light> lights;

  // Shared unit-cube mesh (positions only, CCW).
  static const float* cubeVertices(); // 8 * vec3
  static const uint32_t* cubeIndices(); // 36 indices

  int addMaterial(const std::string& name, bool castsShadow = true);
  uint32_t addObject(const std::string& name, int materialId, Vec3 pos,
                     Vec3 scale, float rotY = 0.0f);
  void moveObject(uint32_t id, Vec3 newPos);
  void rotateObject(uint32_t id, float deltaRad);
  void bumpMaterialVersion(int materialId);
  void setLightDirection(size_t light, Vec3 dir);
  void setLightPosition(size_t light, Vec3 pos);
};

// Builds the shared demo scene (ground + scattered boxes, sun + lamp).
Scene buildDemoScene();

// Scenario scripts: mutate the scene for frame `frame`.
enum class Scenario { Static, MovingLight, MaterialChange };
const char* scenarioName(Scenario s);
void applyScenario(Scenario s, Scene& scene, uint32_t frame);
