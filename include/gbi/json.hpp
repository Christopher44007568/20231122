#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace gbi {

class Json {
 public:
  using Array = std::vector<Json>;
  using Object = std::map<std::string, Json>;

  Json();
  Json(std::nullptr_t);
  Json(bool value);
  Json(double value);
  Json(int value);
  Json(std::uint32_t value);
  Json(std::uint64_t value);
  Json(std::int64_t value);
  Json(const char* value);
  Json(std::string value);
  Json(Array value);
  Json(Object value);

  static Json array();
  static Json object();
  static Json parse(const std::string& text);

  bool isNull() const;
  bool isBool() const;
  bool isNumber() const;
  bool isString() const;
  bool isArray() const;
  bool isObject() const;

  bool asBool(bool fallback = false) const;
  double asDouble(double fallback = 0.0) const;
  std::int64_t asInt(std::int64_t fallback = 0) const;
  std::uint64_t asUint(std::uint64_t fallback = 0) const;
  const std::string& asString() const;
  const Array& asArray() const;
  const Object& asObject() const;
  Array& asArray();
  Object& asObject();

  bool contains(const std::string& key) const;
  const Json& at(const std::string& key) const;
  Json& operator[](const std::string& key);
  const Json& operator[](const std::string& key) const;
  Json& operator[](std::size_t index);
  const Json& operator[](std::size_t index) const;
  void push(Json value);

  std::string value(const std::string& key, const std::string& fallback = {})
      const;
  std::uint64_t u64(const std::string& key, std::uint64_t fallback = 0) const;
  std::int64_t i64(const std::string& key, std::int64_t fallback = 0) const;
  double number(const std::string& key, double fallback = 0.0) const;
  bool boolean(const std::string& key, bool fallback = false) const;

  std::string dump(int indent = 2) const;

 private:
  using Value = std::variant<std::nullptr_t, bool, double, std::string, Array,
                             Object>;
  Value value_;
};

}  // namespace gbi
