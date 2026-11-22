#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace ph {

// Ordered JSON is intentionally strict: reload never sorts object fields.
class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json();
    explicit Json(bool value);
    explicit Json(const char* value);
    explicit Json(std::string value);
    explicit Json(int value);
    explicit Json(int64_t value);
    explicit Json(uint32_t value);
    explicit Json(uint64_t value);
    explicit Json(double value, int precision = 6);

    static Json array();
    static Json object();
    static Json makeNumberToken(std::string token);

    Type type() const { return type_; }
    bool isObject() const { return type_ == Type::Object; }
    bool isArray() const { return type_ == Type::Array; }
    bool isString() const { return type_ == Type::String; }
    bool isNumber() const { return type_ == Type::Number; }
    bool isBool() const { return type_ == Type::Bool; }

    bool asBool() const { return bool_; }
    int64_t asInt() const;
    uint64_t asUInt() const;
    double asDouble() const;
    const std::string& asString() const { return string_; }
    const std::vector<Json>& items() const { return array_; }
    const std::vector<std::pair<std::string, Json>>& fields() const { return object_; }
    std::vector<Json>& items() { return array_; }
    std::vector<std::pair<std::string, Json>>& fields() { return object_; }

    Json& set(std::string key, Json value);
    Json& push(Json value);
    const Json* find(const std::string& key) const;
    Json* find(const std::string& key);
    bool erase(const std::string& key);
    size_t size() const;
    std::vector<std::string> keys() const;

    std::string dumpPretty(int indent = 2) const;
    std::string dumpCompact() const;
    static bool parse(const std::string& text, Json& out, std::string& error);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::string number_;
    std::string string_;
    std::vector<Json> array_;
    std::vector<std::pair<std::string, Json>> object_;

    void dumpPrettyTo(std::string& out, int indent, int depth) const;
    void dumpCompactTo(std::string& out) const;
};

std::string escapeJson(const std::string& value);

}  // namespace ph
