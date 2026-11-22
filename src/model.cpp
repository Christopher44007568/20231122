#include "model.hpp"

#include <cmath>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <stdexcept>

namespace bbox {

bool Bounds::finite() const {
  return std::isfinite(min.x) && std::isfinite(min.y) &&
         std::isfinite(min.z) && std::isfinite(max.x) &&
         std::isfinite(max.y) && std::isfinite(max.z);
}

bool Bounds::ordered() const {
  return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

bool Bounds::equals(const Bounds& other) const {
  return min.x == other.min.x && min.y == other.min.y &&
         min.z == other.min.z && max.x == other.max.x &&
         max.y == other.max.y && max.z == other.max.z;
}

namespace {

picojson::object asObject(const picojson::value& value, std::string context) {
  if (!value.is<picojson::object>()) {
    throw std::runtime_error(context + " must be a JSON object");
  }
  return value.get<picojson::object>();
}

picojson::value required(const picojson::object& object,
                         const std::string& key, std::string context) {
  const auto it = object.find(key);
  if (it == object.end()) {
    throw std::runtime_error(context + " is missing field '" + key + "'");
  }
  return it->second;
}

std::string asString(const picojson::value& value,
                     std::string context) {
  if (!value.is<std::string>()) {
    throw std::runtime_error(context + " must be a string");
  }
  return value.get<std::string>();
}

double asNumber(const picojson::value& value, std::string context) {
  if (!value.is<double>()) {
    throw std::runtime_error(context + " must be a number");
  }
  return value.get<double>();
}

std::uint64_t asUnsigned(const picojson::value& value,
                         std::string context) {
  const double number = asNumber(value, context);
  if (number < 0.0 || std::floor(number) != number) {
    throw std::runtime_error(context + " must be a non-negative integer");
  }
  return static_cast<std::uint64_t>(number);
}

Vec3 readVec3(const picojson::value& value, std::string context) {
  if (!value.is<picojson::array>() || value.get<picojson::array>().size() != 3) {
    throw std::runtime_error(context + " must be [x, y, z]");
  }
  const auto& array = value.get<picojson::array>();
  return {asNumber(array[0], context + "[0]"),
          asNumber(array[1], context + "[1]"),
          asNumber(array[2], context + "[2]")};
}

Bounds readBounds(const picojson::value& value, std::string context) {
  const picojson::object object = asObject(value, context);
  Bounds bounds;
  bounds.min = readVec3(required(object, "min", context), context + ".min");
  bounds.max = readVec3(required(object, "max", context), context + ".max");
  return bounds;
}

picojson::value numberArray(const Vec3& value) {
  picojson::array array;
  array.emplace_back(value.x);
  array.emplace_back(value.y);
  array.emplace_back(value.z);
  return picojson::value(array);
}

std::string readTextFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("cannot open " + path);
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

}  // namespace

picojson::value vec3Json(const Vec3& value) { return numberArray(value); }

picojson::value boundsJson(const Bounds& value) {
  picojson::object object;
  object["min"] = vec3Json(value.min);
  object["max"] = vec3Json(value.max);
  return picojson::value(object);
}

picojson::value modelJson(const Model& value) {
  picojson::object object;
  object["id"] = picojson::value(value.id);
  object["version"] = picojson::value(static_cast<double>(value.version));
  object["last_event"] = picojson::value(value.last_event);
  object["bounds"] = boundsJson(value.bounds);
  return picojson::value(object);
}

picojson::value modelsJson(const std::map<std::string, Model>& models) {
  picojson::array array;
  for (const auto& entry : models) {
    array.push_back(modelJson(entry.second));
  }
  return picojson::value(array);
}

picojson::value snapshotJson(const Snapshot& snapshot) {
  picojson::object object;
  object["snapshot_version"] =
      picojson::value(static_cast<double>(snapshot.snapshot_version));
  object["captured_at"] = picojson::value(snapshot.captured_at);
  object["models"] = modelsJson(snapshot.models);
  return picojson::value(object);
}

picojson::value updateJson(const Update& update) {
  picojson::object object;
  object["sequence"] = picojson::value(static_cast<double>(update.sequence));
  object["event_id"] = picojson::value(update.event_id);
  object["object_id"] = picojson::value(update.object_id);
  object["version"] = picojson::value(static_cast<double>(update.version));
  object["bounds"] = boundsJson(update.bounds);
  return picojson::value(object);
}

Snapshot loadSnapshot(const std::string& path) {
  picojson::value document;
  const std::string parse_error = picojson::parse(document, readTextFile(path));
  if (!parse_error.empty()) {
    throw std::runtime_error("invalid snapshot JSON in " + path + ": " +
                             parse_error);
  }

  const picojson::object root = asObject(document, path);
  Snapshot snapshot;
  snapshot.snapshot_version =
      asUnsigned(required(root, "snapshot_version", path), "snapshot_version");
  snapshot.captured_at =
      asString(required(root, "captured_at", path), "captured_at");
  const picojson::value models_value = required(root, "models", path);
  if (!models_value.is<picojson::array>()) {
    throw std::runtime_error("models must be an array");
  }
  const auto& models = models_value.get<picojson::array>();

  for (std::size_t index = 0; index < models.size(); ++index) {
    const std::string context = "models[" + std::to_string(index) + "]";
    const picojson::object model_object = asObject(models[index], context);
    Model model;
    model.id = asString(required(model_object, "id", context), context + ".id");
    model.version = asUnsigned(required(model_object, "version", context),
                               context + ".version");
    model.last_event = asString(required(model_object, "last_event", context),
                                context + ".last_event");
    model.bounds =
        readBounds(required(model_object, "bounds", context), context + ".bounds");
    if (!snapshot.models.emplace(model.id, model).second) {
      throw std::runtime_error("duplicate model id in snapshot: " + model.id);
    }
  }
  return snapshot;
}

bool parseUpdateJson(const std::string& text, Update& update,
                     std::string& error) {
  picojson::value document;
  error = picojson::parse(document, text);
  if (!error.empty()) {
    return false;
  }
  try {
    const picojson::object root = asObject(document, "update");
    update.sequence =
        asUnsigned(required(root, "sequence", "update"), "update.sequence");
    update.event_id =
        asString(required(root, "event_id", "update"), "update.event_id");
    update.object_id =
        asString(required(root, "object_id", "update"), "update.object_id");
    update.version =
        asUnsigned(required(root, "version", "update"), "update.version");
    update.bounds =
        readBounds(required(root, "bounds", "update"), "update.bounds");
    return true;
  } catch (const std::exception& exception) {
    error = exception.what();
    return false;
  }
}

std::string compactJson(const picojson::value& value) {
  return value.serialize();
}

std::string prettyJson(const picojson::value& value) {
  std::ostringstream output;
  value.serialize(std::ostream_iterator<char>(output), true);
  output << '\n';
  return output.str();
}

std::string modelsCanonicalText(const std::map<std::string, Model>& models) {
  std::ostringstream output;
  output << std::setprecision(17);
  for (const auto& entry : models) {
    const Model& model = entry.second;
    output << model.id << '|' << model.version << '|' << model.last_event << '|'
           << model.bounds.min.x << ',' << model.bounds.min.y << ','
           << model.bounds.min.z << '|' << model.bounds.max.x << ','
           << model.bounds.max.y << ',' << model.bounds.max.z << '\n';
  }
  return output.str();
}

}  // namespace bbox
