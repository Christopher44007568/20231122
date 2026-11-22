#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include <picojson.h>

namespace bbox {

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Bounds {
  Vec3 min;
  Vec3 max;

  bool finite() const;
  bool ordered() const;
  bool equals(const Bounds& other) const;
};

struct Model {
  std::string id;
  std::uint64_t version = 0;
  std::string last_event;
  Bounds bounds;
};

struct Snapshot {
  std::uint64_t snapshot_version = 0;
  std::string captured_at;
  std::map<std::string, Model> models;
};

struct Update {
  std::uint64_t sequence = 0;
  std::string event_id;
  std::string object_id;
  std::uint64_t version = 0;
  Bounds bounds;
};

picojson::value vec3Json(const Vec3& value);
picojson::value boundsJson(const Bounds& value);
picojson::value modelJson(const Model& value);
picojson::value modelsJson(const std::map<std::string, Model>& models);
picojson::value snapshotJson(const Snapshot& snapshot);
picojson::value updateJson(const Update& update);

Snapshot loadSnapshot(const std::string& path);
bool parseUpdateJson(const std::string& text, Update& update,
                     std::string& error);

std::string compactJson(const picojson::value& value);
std::string prettyJson(const picojson::value& value);
std::string modelsCanonicalText(const std::map<std::string, Model>& models);

}  // namespace bbox
