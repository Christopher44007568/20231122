#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace rr {

class JsonValue {
public:
    using Array = std::vector<JsonValue>;
    using Object = std::vector<std::pair<std::string, JsonValue>>;

    JsonValue() = default;

    static JsonValue null();
    static JsonValue boolean(bool value);
    static JsonValue number(double value);
    static JsonValue string(std::string value);
    static JsonValue array(Array value = {});
    static JsonValue object(Object value = {});

    bool isNull() const;
    bool isBool() const;
    bool isNumber() const;
    bool isString() const;
    bool isArray() const;
    bool isObject() const;

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;
    Array& asArray();
    Object& asObject();

    const JsonValue* find(std::string_view key) const;
    JsonValue& set(std::string key, JsonValue value);

private:
    using Storage = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;
    explicit JsonValue(Storage storage);

    Storage storage_;
};

struct JsonError {
    std::size_t offset = 0;
    std::size_t line = 1;
    std::size_t column = 1;
    std::string message;
};

class JsonParser {
public:
    struct Limits {
        std::size_t max_depth = 128;
        std::size_t max_nodes = 8'000'000;
    };

    JsonParser();
    explicit JsonParser(Limits limits);

    bool parse(std::string_view text, JsonValue& out, JsonError& error);

private:
    bool parseValue(JsonValue& out, std::size_t depth);
    bool parseObject(JsonValue& out, std::size_t depth);
    bool parseArray(JsonValue& out, std::size_t depth);
    bool parseString(std::string& out);
    bool parseNumber(double& out);
    bool parseLiteral(std::string_view literal);
    void skipWhitespace();
    bool consume(char expected);
    bool fail(std::string message);
    bool atEnd() const;
    char peek() const;

    std::string_view text_;
    std::size_t pos_ = 0;
    std::size_t nodes_ = 0;
    Limits limits_;
    JsonError error_;
};

std::string jsonEscape(std::string_view value);
std::string jsonNumber(double value);

}  // namespace rr
