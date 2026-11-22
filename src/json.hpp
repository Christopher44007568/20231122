#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kfc {

class Json {
public:
  enum class Type { Null, Bool, Int, UInt, Double, String, Array, Object };
  using Array = std::vector<Json>;
  using Object = std::vector<std::pair<std::string, Json>>;

  Json() = default;
  Json(std::nullptr_t) {}
  Json(bool value) : type_(Type::Bool), bool_(value) {}
  Json(int value) : type_(Type::Int), int_(value) {}
  Json(std::int64_t value) : type_(Type::Int), int_(value) {}
  Json(std::uint64_t value) : type_(Type::UInt), uint_(value) {}
  Json(double value) : type_(Type::Double), double_(value) {}
  Json(const char* value) : type_(Type::String), string_(value) {}
  Json(std::string value) : type_(Type::String), string_(std::move(value)) {}

  static Json array();
  static Json object();
  static Json parse(std::string_view text, std::string* error = nullptr);

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isBool() const { return type_ == Type::Bool; }
  bool isNumber() const {
    return type_ == Type::Int || type_ == Type::UInt || type_ == Type::Double;
  }
  bool isString() const { return type_ == Type::String; }
  bool isArray() const { return type_ == Type::Array; }
  bool isObject() const { return type_ == Type::Object; }

  bool asBool(bool fallback = false) const;
  std::int64_t asInt(std::int64_t fallback = 0) const;
  double asDouble(double fallback = 0.0) const;
  const std::string& asString() const;
  const Array& asArray() const;
  Array& asArray();
  const Object& asObject() const;
  Object& asObject();

  bool contains(std::string_view key) const;
  const Json* get(std::string_view key) const;
  Json* get(std::string_view key);
  const Json& at(std::string_view key) const;
  Json& operator[](std::string key);
  Json& push(Json value);

  std::size_t size() const;
  const std::string& keyAt(std::size_t index) const;
  const Json& valueAt(std::size_t index) const;

  std::string dump(int indent = 2) const;
  const std::string& typeName() const;

private:
  Type type_ = Type::Null;
  bool bool_ = false;
  std::int64_t int_ = 0;
  std::uint64_t uint_ = 0;
  double double_ = 0.0;
  std::string string_;
  Array array_;
  Object object_;

  void dumpTo(std::string& out, int indent, int depth) const;
  static void appendEscaped(std::string& out, std::string_view value);
};

std::string readTextFile(const std::string& path);
void writeTextFile(const std::string& path, std::string_view text);
void ensureDirectory(const std::string& path);
std::string pathJoin(const std::string& a, const std::string& b);
bool fileExists(const std::string& path);

}  // namespace kfc
