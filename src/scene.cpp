#include "gbi/scene.hpp"

#include "gbi/util.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gbi {

namespace {

constexpr float kPi = 3.14159265358979323846F;

Vec3 subtract(const Vec3& left, const Vec3& right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 cross(const Vec3& left, const Vec3& right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

float dot(const Vec3& left, const Vec3& right) {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 normalize(const Vec3& value) {
  const float length =
      std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
  if (length <= 1e-8F) {
    return {0.0F, 0.0F, 0.0F};
  }
  return {value.x / length, value.y / length, value.z / length};
}

Vec3 readVec3(const Json& object, const std::string& key,
              const Vec3& fallback) {
  if (!object.contains(key) || !object.at(key).isArray()) {
    return fallback;
  }
  const auto& array = object.at(key).asArray();
  if (array.size() != 3) {
    return fallback;
  }
  return {static_cast<float>(array[0].asDouble()),
          static_cast<float>(array[1].asDouble()),
          static_cast<float>(array[2].asDouble())};
}

float readNumber(const Json& object, const std::string& key, float fallback) {
  if (!object.contains(key) || !object.at(key).isNumber()) {
    return fallback;
  }
  return static_cast<float>(object.at(key).asDouble());
}

void addTriangle(std::vector<Vertex>& vertices, const Vec3& a, const Vec3& b,
                 const Vec3& c, const Vec3& normal) {
  vertices.push_back({{a.x, a.y, a.z}, {normal.x, normal.y, normal.z}});
  vertices.push_back({{b.x, b.y, b.z}, {normal.x, normal.y, normal.z}});
  vertices.push_back({{c.x, c.y, c.z}, {normal.x, normal.y, normal.z}});
}

}  // namespace

Mat4 Mat4::identity() {
  Mat4 result;
  result.data = {1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F,
                 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F};
  return result;
}

Mat4 Mat4::translation(const Vec3& value) {
  Mat4 result = identity();
  result.data[12] = value.x;
  result.data[13] = value.y;
  result.data[14] = value.z;
  return result;
}

Mat4 Mat4::rotationX(float radians) {
  Mat4 result = identity();
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  result.data[5] = cosine;
  result.data[6] = sine;
  result.data[9] = -sine;
  result.data[10] = cosine;
  return result;
}

Mat4 Mat4::rotationY(float radians) {
  Mat4 result = identity();
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  result.data[0] = cosine;
  result.data[2] = -sine;
  result.data[8] = sine;
  result.data[10] = cosine;
  return result;
}

Mat4 Mat4::rotationZ(float radians) {
  Mat4 result = identity();
  const float cosine = std::cos(radians);
  const float sine = std::sin(radians);
  result.data[0] = cosine;
  result.data[1] = sine;
  result.data[4] = -sine;
  result.data[5] = cosine;
  return result;
}

Mat4 Mat4::scale(const Vec3& value) {
  Mat4 result = identity();
  result.data[0] = value.x;
  result.data[5] = value.y;
  result.data[10] = value.z;
  return result;
}

Mat4 Mat4::lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
  const Vec3 forward = normalize(subtract(target, eye));
  const Vec3 side = normalize(cross(forward, up));
  const Vec3 upward = cross(side, forward);
  Mat4 result = identity();
  result.data[0] = side.x;
  result.data[1] = upward.x;
  result.data[2] = -forward.x;
  result.data[4] = side.y;
  result.data[5] = upward.y;
  result.data[6] = -forward.y;
  result.data[8] = side.z;
  result.data[9] = upward.z;
  result.data[10] = -forward.z;
  result.data[12] = -dot(side, eye);
  result.data[13] = -dot(upward, eye);
  result.data[14] = dot(forward, eye);
  return result;
}

Mat4 Mat4::orthographic(float left, float right, float bottom, float top,
                        float near_plane, float far_plane) {
  Mat4 result = identity();
  result.data[0] = 2.0F / (right - left);
  result.data[5] = 2.0F / (top - bottom);
  result.data[10] = 1.0F / (near_plane - far_plane);
  result.data[12] = -(right + left) / (right - left);
  result.data[13] = -(top + bottom) / (top - bottom);
  result.data[14] = near_plane / (near_plane - far_plane);
  return result;
}

Mat4 Mat4::perspective(float vertical_fov_radians, float aspect,
                       float near_plane, float far_plane) {
  Mat4 result;
  const float focal = 1.0F / std::tan(vertical_fov_radians * 0.5F);
  result.data[0] = focal / aspect;
  result.data[5] = -focal;
  result.data[10] = far_plane / (near_plane - far_plane);
  result.data[11] = -1.0F;
  result.data[14] = (near_plane * far_plane) / (near_plane - far_plane);
  return result;
}

Mat4 Mat4::operator*(const Mat4& other) const {
  Mat4 result;
  for (std::size_t column = 0; column < 4; ++column) {
    for (std::size_t row = 0; row < 4; ++row) {
      float value = 0.0F;
      for (std::size_t index = 0; index < 4; ++index) {
        value += data[index * 4 + row] * other.data[column * 4 + index];
      }
      result.data[column * 4 + row] = value;
    }
  }
  return result;
}

const float* Mat4::dataPtr() const {
  return data.data();
}

Scene Scene::load(const std::filesystem::path& path) {
  Scene scene;
  scene.source_path = path;
  scene.source_text = readText(path);
  scene.source_sha256 = sha256Hex(
      reinterpret_cast<const std::uint8_t*>(scene.source_text.data()),
      scene.source_text.size());

  const Json root = Json::parse(scene.source_text);
  scene.name = root.value("name", "unnamed-scene");
  if (root.contains("camera") && root.at("camera").isObject()) {
    const Json& camera = root.at("camera");
    scene.camera.eye = readVec3(camera, "eye", scene.camera.eye);
    scene.camera.target = readVec3(camera, "target", scene.camera.target);
    scene.camera.up = readVec3(camera, "up", scene.camera.up);
    const Vec3 scale = readVec3(camera, "scale",
                                {scene.camera.scale_x, scene.camera.scale_y,
                                 1.0F});
    scene.camera.scale_x = scale.x;
    scene.camera.scale_y = scale.y;
    scene.camera.near_plane =
        readNumber(camera, "near", scene.camera.near_plane);
    scene.camera.far_plane =
        readNumber(camera, "far", scene.camera.far_plane);
  }

  if (!root.contains("objects") || !root.at("objects").isArray()) {
    throw std::runtime_error("scene has no objects array: " + path.string());
  }
  for (const Json& item : root.at("objects").asArray()) {
    if (!item.isObject()) {
      throw std::runtime_error("scene object is not a JSON object");
    }
    SceneObject object;
    object.id = item.value("id");
    object.shape = item.value("shape", "cube");
    if (object.id.empty()) {
      throw std::runtime_error("scene object has an empty id");
    }
    if (scene.find(object.id) != nullptr) {
      throw std::runtime_error("duplicate scene object id: " + object.id);
    }
    object.position = readVec3(item, "position", {0.0F, 0.0F, 0.0F});
    object.rotation_deg = readVec3(item, "rotation_deg", {0.0F, 0.0F, 0.0F});
    object.scale = readVec3(item, "scale", {1.0F, 1.0F, 1.0F});
    object.albedo = readVec3(item, "albedo", {0.8F, 0.8F, 0.8F});
    object.roughness = readNumber(item, "roughness", 0.5F);
    object.metallic = readNumber(item, "metallic", 0.0F);
    object.source_range = findObjectRange(scene.source_text, object.id);
    if (object.source_range.length == 0) {
      throw std::runtime_error("cannot locate source range for object: " +
                               object.id);
    }
    scene.objects.push_back(std::move(object));
  }
  if (scene.objects.empty()) {
    throw std::runtime_error("scene contains no render objects");
  }
  return scene;
}

const SceneObject* Scene::find(const std::string& id) const {
  for (const SceneObject& object : objects) {
    if (object.id == id) {
      return &object;
    }
  }
  return nullptr;
}

SceneObject* Scene::find(const std::string& id) {
  for (SceneObject& object : objects) {
    if (object.id == id) {
      return &object;
    }
  }
  return nullptr;
}

std::optional<std::size_t> Scene::indexOf(const std::string& id) const {
  for (std::size_t index = 0; index < objects.size(); ++index) {
    if (objects[index].id == id) {
      return index;
    }
  }
  return std::nullopt;
}

void Scene::applyOverlay(const std::string& object_id, const Json& overlay,
                         const std::filesystem::path& overlay_path,
                         const std::string& overlay_text) {
  SceneObject* object = find(object_id);
  if (object == nullptr) {
    throw std::runtime_error("overlay references unknown object: " + object_id);
  }
  if (overlay.contains("albedo")) {
    const auto& array = overlay.at("albedo").asArray();
    if (array.size() != 3) {
      throw std::runtime_error("overlay albedo must have three channels");
    }
    object->albedo = {static_cast<float>(array[0].asDouble()),
                      static_cast<float>(array[1].asDouble()),
                      static_cast<float>(array[2].asDouble())};
    object->overlay_albedo_range =
        findFieldRange(overlay_text, "albedo");
  }
  if (overlay.contains("roughness")) {
    object->roughness =
        static_cast<float>(overlay.at("roughness").asDouble());
    object->overlay_roughness_range =
        findFieldRange(overlay_text, "roughness");
  }
  if (overlay.contains("metallic")) {
    object->metallic = static_cast<float>(overlay.at("metallic").asDouble());
    object->overlay_metallic_range = findFieldRange(overlay_text, "metallic");
  }
  object->corrected = true;
  object->overlay_path = overlay_path.string();
  object->overlay_sha256 = sha256Hex(
      reinterpret_cast<const std::uint8_t*>(overlay_text.data()),
      overlay_text.size());
}

std::string Scene::fingerprint() const {
  std::string material = source_sha256;
  for (const SceneObject& object : objects) {
    if (object.corrected) {
      material += "|" + object.id + ":" + object.overlay_sha256;
    }
  }
  return sha256Hex(reinterpret_cast<const std::uint8_t*>(material.data()),
                   material.size());
}

ObjectOverlay parseOverlay(const std::string& object_id,
                           const std::filesystem::path& path) {
  ObjectOverlay overlay;
  overlay.object_id = object_id;
  overlay.path = path;
  overlay.text = readText(path);
  const Json root = Json::parse(overlay.text);
  if (root.contains("object_id") &&
      root.value("object_id") != object_id) {
    throw std::runtime_error("overlay object_id mismatch in " + path.string());
  }
  if (root.contains("albedo")) {
    if (!root.at("albedo").isArray() ||
        root.at("albedo").asArray().size() != 3) {
      throw std::runtime_error("overlay albedo must be an array of 3 numbers");
    }
    const auto& array = root.at("albedo").asArray();
    overlay.albedo = {static_cast<float>(array[0].asDouble()),
                      static_cast<float>(array[1].asDouble()),
                      static_cast<float>(array[2].asDouble())};
    overlay.has_albedo = true;
  }
  if (root.contains("roughness")) {
    overlay.roughness = static_cast<float>(root.at("roughness").asDouble());
    overlay.has_roughness = true;
  }
  if (root.contains("metallic")) {
    overlay.metallic = static_cast<float>(root.at("metallic").asDouble());
    overlay.has_metallic = true;
  }
  if (!overlay.has_albedo && !overlay.has_roughness &&
      !overlay.has_metallic) {
    throw std::runtime_error("overlay has no supported material fields: " +
                             path.string());
  }
  return overlay;
}

Json overlayToJson(const ObjectOverlay& overlay) {
  Json root = Json::object();
  root["schema"] = "gbi.overlay/1";
  root["object_id"] = overlay.object_id;
  if (overlay.has_albedo) {
    Json values = Json::array();
    values.push(static_cast<double>(overlay.albedo.x));
    values.push(static_cast<double>(overlay.albedo.y));
    values.push(static_cast<double>(overlay.albedo.z));
    root["albedo"] = values;
  }
  if (overlay.has_roughness) {
    root["roughness"] = static_cast<double>(overlay.roughness);
  }
  if (overlay.has_metallic) {
    root["metallic"] = static_cast<double>(overlay.metallic);
  }
  return root;
}

std::vector<Vertex> makeCube() {
  const Vec3 p000{-0.5F, -0.5F, -0.5F};
  const Vec3 p001{-0.5F, -0.5F, 0.5F};
  const Vec3 p010{-0.5F, 0.5F, -0.5F};
  const Vec3 p011{-0.5F, 0.5F, 0.5F};
  const Vec3 p100{0.5F, -0.5F, -0.5F};
  const Vec3 p101{0.5F, -0.5F, 0.5F};
  const Vec3 p110{0.5F, 0.5F, -0.5F};
  const Vec3 p111{0.5F, 0.5F, 0.5F};

  std::vector<Vertex> vertices;
  vertices.reserve(36);
  const Vec3 front{0.0F, 0.0F, 1.0F};
  addTriangle(vertices, p001, p101, p111, front);
  addTriangle(vertices, p001, p111, p011, front);
  const Vec3 back{0.0F, 0.0F, -1.0F};
  addTriangle(vertices, p100, p000, p010, back);
  addTriangle(vertices, p100, p010, p110, back);
  const Vec3 left{-1.0F, 0.0F, 0.0F};
  addTriangle(vertices, p000, p001, p011, left);
  addTriangle(vertices, p000, p011, p010, left);
  const Vec3 right{1.0F, 0.0F, 0.0F};
  addTriangle(vertices, p101, p100, p110, right);
  addTriangle(vertices, p101, p110, p111, right);
  const Vec3 top{0.0F, 1.0F, 0.0F};
  addTriangle(vertices, p010, p011, p111, top);
  addTriangle(vertices, p010, p111, p110, top);
  const Vec3 bottom{0.0F, -1.0F, 0.0F};
  addTriangle(vertices, p000, p100, p101, bottom);
  addTriangle(vertices, p000, p101, p001, bottom);
  return vertices;
}

std::vector<Vertex> makePrism() {
  const float root_three = std::sqrt(3.0F);
  const Vec3 a{-0.5F, -0.5F, -0.5F / root_three};
  const Vec3 b{0.5F, -0.5F, -0.5F / root_three};
  const Vec3 c{0.0F, -0.5F, 0.5F / root_three};
  const Vec3 d{-0.5F, 0.5F, -0.5F / root_three};
  const Vec3 e{0.5F, 0.5F, -0.5F / root_three};
  const Vec3 f{0.0F, 0.5F, 0.5F / root_three};

  std::vector<Vertex> vertices;
  vertices.reserve(24);
  addTriangle(vertices, a, c, b, {0.0F, -1.0F, 0.0F});
  addTriangle(vertices, d, e, f, {0.0F, 1.0F, 0.0F});
  const Vec3 side_ab = normalize(cross(subtract(b, a), subtract(d, a)));
  addTriangle(vertices, a, b, e, side_ab);
  addTriangle(vertices, a, e, d, side_ab);
  const Vec3 side_bc = normalize(cross(subtract(c, b), subtract(e, b)));
  addTriangle(vertices, b, c, f, side_bc);
  addTriangle(vertices, b, f, e, side_bc);
  const Vec3 side_ca = normalize(cross(subtract(a, c), subtract(f, c)));
  addTriangle(vertices, c, a, d, side_ca);
  addTriangle(vertices, c, d, f, side_ca);
  return vertices;
}

namespace {

std::size_t findMatching(const std::string& text, std::size_t open_position,
                         char open_character, char close_character) {
  int depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t index = open_position; index < text.size(); ++index) {
    const char character = text[index];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        in_string = false;
      }
      continue;
    }
    if (character == '"') {
      in_string = true;
    } else if (character == open_character) {
      ++depth;
    } else if (character == close_character) {
      --depth;
      if (depth == 0) {
        return index;
      }
    }
  }
  return std::string::npos;
}

std::size_t skipWhitespace(const std::string& text, std::size_t position) {
  while (position < text.size()) {
    const char character = text[position];
    if (character != ' ' && character != '\t' && character != '\r' &&
        character != '\n') {
      break;
    }
    ++position;
  }
  return position;
}

}  // namespace

SourceRange findObjectRange(const std::string& text,
                            const std::string& object_id) {
  const std::string needle = "\"id\"";
  std::size_t search = 0;
  while ((search = text.find(needle, search)) != std::string::npos) {
    std::size_t position = skipWhitespace(text, search + needle.size());
    if (position >= text.size() || text[position] != ':') {
      search += needle.size();
      continue;
    }
    position = skipWhitespace(text, position + 1);
    if (position >= text.size() || text[position] != '"') {
      search += needle.size();
      continue;
    }
    const std::size_t id_start = ++position;
    bool escaped = false;
    while (position < text.size()) {
      const char character = text[position];
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        break;
      }
      ++position;
    }
    if (position >= text.size()) {
      break;
    }
    const std::string found = text.substr(id_start, position - id_start);
    if (found == object_id) {
      const std::size_t object_start = text.rfind('{', search);
      if (object_start == std::string::npos) {
        return {};
      }
      const std::size_t object_end =
          findMatching(text, object_start, '{', '}');
      if (object_end == std::string::npos) {
        return {};
      }
      return {object_start, object_end - object_start + 1};
    }
    search = position + 1;
  }
  return {};
}

SourceRange findFieldRange(const std::string& text, const std::string& field,
                           std::size_t start) {
  const std::string needle = "\"" + field + "\"";
  const std::size_t position = text.find(needle, start);
  if (position == std::string::npos) {
    return {};
  }
  std::size_t cursor = skipWhitespace(text, position + needle.size());
  if (cursor >= text.size() || text[cursor] != ':') {
    return {};
  }
  cursor = skipWhitespace(text, cursor + 1);
  if (cursor >= text.size()) {
    return {};
  }
  const std::size_t value_start = cursor;
  std::size_t value_end = cursor;
  if (text[cursor] == '"') {
    ++cursor;
    bool escaped = false;
    while (cursor < text.size()) {
      const char character = text[cursor];
      if (escaped) {
        escaped = false;
      } else if (character == '\\') {
        escaped = true;
      } else if (character == '"') {
        ++cursor;
        break;
      }
      ++cursor;
    }
    value_end = cursor;
  } else if (text[cursor] == '{' || text[cursor] == '[') {
    const char close = text[cursor] == '{' ? '}' : ']';
    const std::size_t close_position =
        findMatching(text, cursor, text[cursor], close);
    if (close_position == std::string::npos) {
      return {};
    }
    value_end = close_position + 1;
  } else {
    while (cursor < text.size() && text[cursor] != ',' &&
           text[cursor] != '}' && text[cursor] != ']' &&
           text[cursor] != '\n' && text[cursor] != '\r') {
      ++cursor;
    }
    value_end = cursor;
    while (value_end > value_start &&
           (text[value_end - 1] == ' ' || text[value_end - 1] == '\t')) {
      --value_end;
    }
  }
  return {value_start, value_end - value_start};
}

}  // namespace gbi
