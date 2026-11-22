#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace skincheck {

class Json {
public:
    enum class Type { Null, Bool, Number, String, Array, Object };

    Json();
    explicit Json(bool value);
    explicit Json(int value);
    explicit Json(std::int64_t value);
    explicit Json(double value);
    explicit Json(const char* value);
    explicit Json(std::string value);

    static Json array();
    static Json object();
    static Json number_from_token(std::string token);

    Type type() const;
    bool is_null() const;
    bool is_bool() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    bool as_bool() const;
    std::int64_t as_int64() const;
    double as_double() const;
    const std::string& as_string() const;

    std::vector<Json>& items();
    const std::vector<Json>& items() const;
    std::vector<std::pair<std::string, Json>>& fields();
    const std::vector<std::pair<std::string, Json>>& fields() const;
    std::vector<std::string> keys() const;

    Json& push(Json value);
    Json& set(std::string key, Json value);
    const Json* find(std::string_view key) const;
    Json* find(std::string_view key);
    bool contains(std::string_view key) const;

    std::string dump(int indent = 2) const;
    static Json parse(const std::string& text, std::string* error = nullptr);

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    std::string scalar_;
    std::vector<Json> items_;
    std::vector<std::pair<std::string, Json>> fields_;
};

}  // namespace skincheck

