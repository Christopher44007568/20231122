#include "scene.hpp"

#include <algorithm>

Scene Scene::makeDefault() {
  Scene scene;
  scene.objects_ = {
    {1, "alpha-near", {-1.02f, 0.43f, -1.80f}, {0.58f, 0.35f}, {0.20f, 0.67f, 0.92f}, 0.2f},
    {2, "beta-gate", {-0.18f, 0.25f, -2.00f}, {0.66f, 0.44f}, {0.94f, 0.43f, 0.24f}, 0.8f},
    {3, "gamma-far", {0.92f, 0.36f, -2.60f}, {0.54f, 0.36f}, {0.27f, 0.82f, 0.50f}, 1.4f},
    {4, "delta-plate", {-0.78f, -0.45f, -2.20f}, {0.65f, 0.37f}, {0.75f, 0.49f, 0.95f}, 2.0f},
    {5, "epsilon-overlap", {-0.63f, -0.40f, -2.19f}, {0.53f, 0.33f}, {0.95f, 0.75f, 0.24f}, 2.6f},
    {6, "zeta-deep", {0.42f, -0.53f, -4.20f}, {0.75f, 0.48f}, {0.22f, 0.79f, 0.76f}, 3.2f},
    {7, "eta-edge", {1.25f, -0.40f, -1.55f}, {0.55f, 0.42f}, {0.92f, 0.30f, 0.56f}, 3.8f},
    {8, "theta-stack", {0.15f, -0.02f, -2.10f}, {0.38f, 0.30f}, {0.55f, 0.65f, 0.98f}, 4.4f},
    {9, "iota-stack", {0.22f, 0.03f, -2.096f}, {0.40f, 0.29f}, {0.86f, 0.90f, 0.38f}, 5.0f},
  };
  return scene;
}

const RenderObject* Scene::findById(std::uint32_t id) const {
  for (const RenderObject& object : objects_) {
    if (object.id == id) {
      return &object;
    }
  }
  return nullptr;
}

const RenderObject* Scene::findByName(const std::string& name) const {
  for (const RenderObject& object : objects_) {
    if (object.name == name) {
      return &object;
    }
  }
  return nullptr;
}

RectI Scene::projectedBounds(const RenderObject& object, const Mat4& viewProjection,
                             int width, int height) const {
  float minX = 1.0f;
  float minY = 1.0f;
  float maxX = -1.0f;
  float maxY = -1.0f;
  const Vec3 corners[] = {
    {object.center.x - object.halfSize.x, object.center.y - object.halfSize.y, object.center.z},
    {object.center.x + object.halfSize.x, object.center.y - object.halfSize.y, object.center.z},
    {object.center.x - object.halfSize.x, object.center.y + object.halfSize.y, object.center.z},
    {object.center.x + object.halfSize.x, object.center.y + object.halfSize.y, object.center.z},
  };
  for (const Vec3& corner : corners) {
    const Vec3 ndc = projectToNdc(viewProjection, corner);
    minX = std::min(minX, ndc.x);
    minY = std::min(minY, ndc.y);
    maxX = std::max(maxX, ndc.x);
    maxY = std::max(maxY, ndc.y);
  }

  const int x0 = static_cast<int>((minX * 0.5f + 0.5f) * static_cast<float>(width));
  const int x1 = static_cast<int>((maxX * 0.5f + 0.5f) * static_cast<float>(width));
  const int y0 = static_cast<int>((0.5f - maxY * 0.5f) * static_cast<float>(height));
  const int y1 = static_cast<int>((0.5f - minY * 0.5f) * static_cast<float>(height));
  const int clampedX0 = std::max(0, std::min(width - 1, x0));
  const int clampedY0 = std::max(0, std::min(height - 1, y0));
  const int clampedX1 = std::max(0, std::min(width - 1, x1));
  const int clampedY1 = std::max(0, std::min(height - 1, y1));
  return {clampedX0, clampedY0, clampedX1 - clampedX0 + 1, clampedY1 - clampedY0 + 1};
}

