#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vfcr {

class Json {
public:
    using Object = std::vector<std::pair<std::string, Json>>;
    using Array = std::vector<Json>;

    Json();
    Json(std::nullptr_t);
    Json(bool value);
    Json(int value);
    Json(std::int64_t value);
    Json(double value);
    Json(const char* value);
    Json(std::string value);

    static Json object();
    static Json array();

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    std::int64_t asInt(std::int64_t fallback = 0) const;
    const std::string& asString() const;
    std::string stringOr(const std::string& fallback) const;
    const Array& arrayItems() const;
    Array& arrayItems();
    const Object& objectItems() const;
    Object& objectItems();

    Json& set(std::string key, Json value);
    Json& push(Json value);
    const Json* find(std::string_view key) const;
    Json* find(std::string_view key);
    bool contains(std::string_view key) const;
    bool erase(std::string_view key);
    std::size_t size() const;

    std::string dump(bool pretty = true) const;
    static Json parse(std::string_view text, std::string* error = nullptr);

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    Storage storage_;

    void dumpTo(std::string& out, bool pretty, int depth) const;
};

std::string canonicalJson(const Json& value);
std::string jsonEscape(std::string_view text);

}  // namespace vfcr
