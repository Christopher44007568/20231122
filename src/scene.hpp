#pragma once

#include "math.hpp"

#include <cstdint>
#include <string>
#include <vector>

struct RenderObject {
  std::uint32_t id = 0;
  std::string name;
  Vec3 center;
  Vec2 halfSize;
  Vec3 color;
  float borderPhase = 0.0f;
};

class Scene {
 public:
  static Scene makeDefault();

  const std::vector<RenderObject>& objects() const { return objects_; }
  const RenderObject* findById(std::uint32_t id) const;
  const RenderObject* findByName(const std::string& name) const;

  RectI projectedBounds(const RenderObject& object, const Mat4& viewProjection,
                        int width, int height) const;

 private:
  std::vector<RenderObject> objects_;
};

