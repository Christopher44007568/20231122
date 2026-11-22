#include "scene.h"

#include <cmath>

Mat4 SceneObject::modelMatrix() const {
  return Mat4::multiply(Mat4::translate(position),
                        Mat4::multiply(Mat4::rotateY(rotationY),
                                       Mat4::scale(scale)));
}

float SceneObject::boundingRadius() const {
  // Unit cube half-diagonal is sqrt(3)/2; scale by the largest axis.
  float s = scale.x;
  if (scale.y > s) s = scale.y;
  if (scale.z > s) s = scale.z;
  return 0.8660254f * s;
}

const float* Scene::cubeVertices() {
  static const float v[8 * 3] = {
      -0.5f, -0.5f, -0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f,
      -0.5f, 0.5f, -0.5f,  -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f,
      0.5f,  0.5f,  0.5f,  -0.5f, 0.5f,  0.5f,
  };
  return v;
}

const uint32_t* Scene::cubeIndices() {
  static const uint32_t idx[36] = {
      0, 2, 1, 0, 3, 2, // back
      4, 5, 6, 4, 6, 7, // front
      0, 4, 7, 0, 7, 3, // left
      1, 6, 5, 1, 2, 6, // right
      3, 6, 2, 3, 7, 6, // top
      0, 5, 4, 0, 1, 5, // bottom
  };
  return idx;
}

int Scene::addMaterial(const std::string& name, bool castsShadow) {
  materials.push_back(Material{name, 0, castsShadow});
  return (int)materials.size() - 1;
}

uint32_t Scene::addObject(const std::string& name, int materialId, Vec3 pos,
                          Vec3 scale, float rotY) {
  SceneObject o;
  o.id = (uint32_t)objects.size();
  o.name = name;
  o.materialId = materialId;
  o.position = pos;
  o.scale = scale;
  o.rotationY = rotY;
  objects.push_back(o);
  return o.id;
}

void Scene::moveObject(uint32_t id, Vec3 newPos) {
  objects[id].position = newPos;
  objects[id].transformVersion++;
}

void Scene::rotateObject(uint32_t id, float deltaRad) {
  objects[id].rotationY += deltaRad;
  objects[id].transformVersion++;
}

void Scene::bumpMaterialVersion(int materialId) {
  materials[materialId].version++;
}

void Scene::setLightDirection(size_t i, Vec3 dir) {
  lights[i].direction = dir;
  lights[i].transformVersion++;
}

void Scene::setLightPosition(size_t i, Vec3 pos) {
  lights[i].position = pos;
  lights[i].transformVersion++;
}

Scene buildDemoScene() {
  Scene s;
  int concrete = s.addMaterial("concrete");
  int metal = s.addMaterial("metal");
  int foliage = s.addMaterial("foliage");

  s.addObject("ground", concrete, {0, -0.7f, 0}, {40, 0.2f, 40});
  // Near cluster (sun cascade 0, radius 4).
  s.addObject("bush_a", foliage, {1.5f, 0.3f, 1.0f}, {1.0f, 1.2f, 1.0f});
  s.addObject("bush_b", foliage, {-1.8f, 0.25f, -0.8f}, {0.9f, 1.0f, 0.9f});
  s.addObject("crate_a", metal, {0.5f, 0.4f, -2.2f}, {1.2f, 1.4f, 1.2f});
  // Mid ring (sun cascade 1, radius 10).
  s.addObject("crate_b", metal, {6.0f, 0.5f, 3.0f}, {1.5f, 1.6f, 1.5f});
  s.addObject("pillar", concrete, {-5.0f, 1.5f, 5.0f}, {1.0f, 4.0f, 1.0f});
  // Far ring (sun cascade 2, radius 25).
  s.addObject("tower", concrete, {15.0f, 3.0f, -10.0f}, {3.0f, 7.0f, 3.0f});
  s.addObject("wall", concrete, {-14.0f, 1.0f, -6.0f}, {8.0f, 2.5f, 1.0f});

  Light sun;
  sun.name = "sun";
  sun.type = LightType::Directional;
  sun.direction = Vec3{0.45f, -1.0f, 0.3f}.normalized();
  sun.cascade.splits = {4.0f, 10.0f, 25.0f};
  sun.cascade.resolution = 256;
  s.lights.push_back(sun);

  Light lamp;
  lamp.name = "lamp";
  lamp.type = LightType::Spot;
  lamp.position = {5.0f, 6.0f, 4.0f};
  lamp.direction = (Vec3{1.0f, 0.0f, 0.5f} - lamp.position).normalized();
  lamp.fovDeg = 65.0f;
  lamp.cascade.splits = {7.0f, 14.0f};
  lamp.cascade.resolution = 256;
  s.lights.push_back(lamp);
  return s;
}

const char* scenarioName(Scenario s) {
  switch (s) {
  case Scenario::Static: return "static";
  case Scenario::MovingLight: return "moving-light";
  case Scenario::MaterialChange: return "material-change";
  }
  return "?";
}

void applyScenario(Scenario s, Scene& scene, uint32_t frame) {
  switch (s) {
  case Scenario::Static:
    break; // nothing ever changes
  case Scenario::MovingLight: {
    // Sun orbits slowly; lamp stays put to show per-light granularity.
    float a = (float)frame * 0.045f;
    Vec3 dir{std::sin(a) * 0.6f, -1.0f, 0.35f + 0.25f * std::cos(a)};
    scene.setLightDirection(0, dir.normalized());
    break;
  }
  case Scenario::MaterialChange: {
    // Localized material edit: the foliage material changes (e.g. its
    // alpha-cutout pattern), which also deforms the two bushes slightly.
    if (frame == 20 || frame == 40) {
      scene.bumpMaterialVersion(2); // foliage
      scene.rotateObject(1, 0.4f);  // bush_a
      scene.rotateObject(2, -0.3f); // bush_b
    }
    break;
  }
  }
}
