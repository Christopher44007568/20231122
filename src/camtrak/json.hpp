#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace camtrak {

// JSON value that preserves object member insertion order on parse and
// re-serialization. Duplicate keys are rejected while parsing.
class JsonValue {
 public:
  enum class Type { Null, Bool, Number, String, Array, Object };

  JsonValue() = default;
  static JsonValue makeNull() { return JsonValue(Type::Null); }
  static JsonValue makeBool(bool value);
  static JsonValue makeNumber(double value);
  static JsonValue makeString(std::string value);
  static JsonValue makeArray();
  static JsonValue makeObject();

  Type type() const { return type_; }
  bool isNull() const { return type_ == Type::Null; }
  bool isObject() const { return type_ == Type::Object; }
  bool isArray() const { return type_ == Type::Array; }

  bool asBool() const;
  double asNumber() const;
  const std::string& asString() const;

  // Object access. Members stay ordered; setting an existing key updates the
  // value without moving its position.
  bool has(const std::string& key) const;
  const JsonValue& get(const std::string& key) const;
  JsonValue& get(const std::string& key);
  void set(const std::string& key, JsonValue value);
  const std::vector<std::string>& keys() const;

  // Array access.
  std::size_t size() const;
  const JsonValue& at(std::size_t index) const;
  JsonValue& at(std::size_t index);
  void push(JsonValue value);

  // Pretty-printed, deterministic serialization (2-space indent).
  std::string dump(int indent = 2) const;

 private:
  explicit JsonValue(Type type) : type_(type) {}

  Type type_ = Type::Null;
  bool bool_value_ = false;
  double number_value_ = 0.0;
  std::string string_value_;
  std::vector<JsonValue> array_value_;
  std::vector<std::string> object_order_;
  std::map<std::string, std::size_t> object_index_;

  void dumpInto(std::string& out, int indent, int depth) const;
};

struct JsonParseError {
  std::string message;
  int line = 0;
  int column = 0;
};

// Parses JSON. On failure, returns a null value and fills `error`.
JsonValue parseJson(const std::string& text, JsonParseError* error = nullptr);

// Escapes a string for embedding as a JSON string literal (with quotes).
std::string jsonEscape(const std::string& raw);

}  // namespace camtrak
