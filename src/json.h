#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

class Json {
public:
    enum class Type { Null, Bool, Int, Real, String, Array, Object };

    Json();
    Json(bool value);
    Json(int value);
    Json(int64_t value);
    Json(uint32_t value);
    Json(uint64_t value);
    Json(double value);
    Json(const char* value);
    Json(std::string value);

    static Json array();
    static Json object();

    Type type() const;
    bool isNull() const;
    bool isObject() const;
    bool isArray() const;
    bool asBool() const;
    int64_t asInt64() const;
    double asDouble() const;
    const std::string& asString() const;
    const std::vector<Json>& items() const;
    const std::vector<std::pair<std::string, Json>>& members() const;

    Json& set(std::string key, Json value);
    Json& push(Json value);
    const Json* get(const std::string& key) const;
    const Json& at(size_t index) const;

private:
    Type type_;
    bool bool_value_;
    int64_t int_value_;
    double real_value_;
    std::string string_value_;
    std::vector<Json> array_value_;
    std::vector<std::pair<std::string, Json>> object_value_;
};

Json parseJson(const std::string& text);
std::string dumpJson(const Json& value, bool pretty = true);

