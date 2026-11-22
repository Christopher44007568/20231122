#include "json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace rr {

JsonValue::JsonValue(Storage storage) : storage_(std::move(storage)) {}

JsonValue JsonValue::null() { return JsonValue(nullptr); }
JsonValue JsonValue::boolean(bool value) { return JsonValue(value); }
JsonValue JsonValue::number(double value) { return JsonValue(value); }
JsonValue JsonValue::string(std::string value) { return JsonValue(std::move(value)); }
JsonValue JsonValue::array(Array value) { return JsonValue(std::move(value)); }
JsonValue JsonValue::object(Object value) { return JsonValue(std::move(value)); }

bool JsonValue::isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
bool JsonValue::isBool() const { return std::holds_alternative<bool>(storage_); }
bool JsonValue::isNumber() const { return std::holds_alternative<double>(storage_); }
bool JsonValue::isString() const { return std::holds_alternative<std::string>(storage_); }
bool JsonValue::isArray() const { return std::holds_alternative<Array>(storage_); }
bool JsonValue::isObject() const { return std::holds_alternative<Object>(storage_); }

bool JsonValue::asBool(bool fallback) const {
    const auto* value = std::get_if<bool>(&storage_);
    return value ? *value : fallback;
}

double JsonValue::asNumber(double fallback) const {
    const auto* value = std::get_if<double>(&storage_);
    return value ? *value : fallback;
}

const std::string& JsonValue::asString() const {
    static const std::string empty;
    const auto* value = std::get_if<std::string>(&storage_);
    return value ? *value : empty;
}

const JsonValue::Array& JsonValue::asArray() const {
    static const Array empty;
    const auto* value = std::get_if<Array>(&storage_);
    return value ? *value : empty;
}

const JsonValue::Object& JsonValue::asObject() const {
    static const Object empty;
    const auto* value = std::get_if<Object>(&storage_);
    return value ? *value : empty;
}

JsonValue::Array& JsonValue::asArray() {
    return std::get<Array>(storage_);
}

JsonValue::Object& JsonValue::asObject() {
    return std::get<Object>(storage_);
}

const JsonValue* JsonValue::find(std::string_view key) const {
    const auto* object = std::get_if<Object>(&storage_);
    if (!object) {
        return nullptr;
    }
    for (const auto& entry : *object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

JsonValue& JsonValue::set(std::string key, JsonValue value) {
    auto& object = std::get<Object>(storage_);
    for (auto& entry : object) {
        if (entry.first == key) {
            entry.second = std::move(value);
            return entry.second;
        }
    }
    object.emplace_back(std::move(key), std::move(value));
    return object.back().second;
}

JsonParser::JsonParser() = default;

JsonParser::JsonParser(Limits limits) : limits_(limits) {}

bool JsonParser::parse(std::string_view text, JsonValue& out, JsonError& error) {
    text_ = text;
    pos_ = 0;
    nodes_ = 0;
    error_ = {};
    skipWhitespace();
    if (!parseValue(out, 0)) {
        error = error_;
        return false;
    }
    skipWhitespace();
    if (pos_ != text_.size()) {
        fail("trailing data after JSON document");
        error = error_;
        return false;
    }
    return true;
}

char JsonParser::peek() const {
    return pos_ < text_.size() ? text_[pos_] : '\0';
}

bool JsonParser::atEnd() const { return pos_ >= text_.size(); }

void JsonParser::skipWhitespace() {
    while (!atEnd()) {
        const char c = text_[pos_];
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            break;
        }
        ++pos_;
    }
}

bool JsonParser::consume(char expected) {
    if (atEnd() || text_[pos_] != expected) {
        return false;
    }
    ++pos_;
    return true;
}

bool JsonParser::fail(std::string message) {
    error_.offset = pos_;
    error_.line = 1;
    error_.column = 1;
    for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
        if (text_[i] == '\n') {
            ++error_.line;
            error_.column = 1;
        } else {
            ++error_.column;
        }
    }
    error_.message = std::move(message);
    return false;
}

bool JsonParser::parseValue(JsonValue& out, std::size_t depth) {
    if (depth > limits_.max_depth) {
        return fail("JSON nesting exceeds max_depth");
    }
    if (++nodes_ > limits_.max_nodes) {
        return fail("JSON node count exceeds max_nodes");
    }
    skipWhitespace();
    if (atEnd()) {
        return fail("unexpected end of JSON");
    }
    switch (peek()) {
        case '{':
            return parseObject(out, depth);
        case '[':
            return parseArray(out, depth);
        case '"': {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = JsonValue::string(std::move(value));
            return true;
        }
        case 't':
            if (!parseLiteral("true")) {
                return false;
            }
            out = JsonValue::boolean(true);
            return true;
        case 'f':
            if (!parseLiteral("false")) {
                return false;
            }
            out = JsonValue::boolean(false);
            return true;
        case 'n':
            if (!parseLiteral("null")) {
                return false;
            }
            out = JsonValue::null();
            return true;
        default: {
            double value = 0.0;
            if (!parseNumber(value)) {
                return false;
            }
            out = JsonValue::number(value);
            return true;
        }
    }
}

bool JsonParser::parseObject(JsonValue& out, std::size_t depth) {
    if (!consume('{')) {
        return fail("expected '{'");
    }
    JsonValue::Object object;
    skipWhitespace();
    if (consume('}')) {
        out = JsonValue::object(std::move(object));
        return true;
    }
    for (;;) {
        skipWhitespace();
        if (peek() != '"') {
            return fail("expected object key string");
        }
        std::string key;
        if (!parseString(key)) {
            return false;
        }
        skipWhitespace();
        if (!consume(':')) {
            return fail("expected ':' after object key");
        }
        JsonValue value;
        if (!parseValue(value, depth + 1)) {
            return false;
        }
        object.emplace_back(std::move(key), std::move(value));
        skipWhitespace();
        if (consume('}')) {
            break;
        }
        if (!consume(',')) {
            return fail("expected ',' or '}' in object");
        }
    }
    out = JsonValue::object(std::move(object));
    return true;
}

bool JsonParser::parseArray(JsonValue& out, std::size_t depth) {
    if (!consume('[')) {
        return fail("expected '['");
    }
    JsonValue::Array array;
    skipWhitespace();
    if (consume(']')) {
        out = JsonValue::array(std::move(array));
        return true;
    }
    for (;;) {
        JsonValue value;
        if (!parseValue(value, depth + 1)) {
            return false;
        }
        array.push_back(std::move(value));
        skipWhitespace();
        if (consume(']')) {
            break;
        }
        if (!consume(',')) {
            return fail("expected ',' or ']' in array");
        }
    }
    out = JsonValue::array(std::move(array));
    return true;
}

bool JsonParser::parseLiteral(std::string_view literal) {
    if (text_.substr(pos_, literal.size()) != literal) {
        return fail("invalid literal");
    }
    pos_ += literal.size();
    return true;
}

bool JsonParser::parseString(std::string& out) {
    if (!consume('"')) {
        return fail("expected string");
    }
    out.clear();
    while (!atEnd()) {
        const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
        if (c == '"') {
            return true;
        }
        if (c < 0x20U) {
            --pos_;
            return fail("unescaped control character in string");
        }
        if (c != '\\') {
            out.push_back(static_cast<char>(c));
            continue;
        }
        if (atEnd()) {
            return fail("unterminated string escape");
        }
        const char escaped = text_[pos_++];
        switch (escaped) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                auto readHex = [this](std::uint32_t& value) -> bool {
                    value = 0;
                    for (int i = 0; i < 4; ++i) {
                        if (atEnd()) {
                            return false;
                        }
                        const char digit = text_[pos_++];
                        value <<= 4U;
                        if (digit >= '0' && digit <= '9') {
                            value |= static_cast<std::uint32_t>(digit - '0');
                        } else if (digit >= 'a' && digit <= 'f') {
                            value |= static_cast<std::uint32_t>(digit - 'a' + 10);
                        } else if (digit >= 'A' && digit <= 'F') {
                            value |= static_cast<std::uint32_t>(digit - 'A' + 10);
                        } else {
                            return false;
                        }
                    }
                    return true;
                };
                std::uint32_t codepoint = 0;
                if (!readHex(codepoint)) {
                    return fail("invalid \\u escape");
                }
                if (codepoint >= 0xD800U && codepoint <= 0xDBFFU) {
                    if (text_.substr(pos_, 2) != "\\u") {
                        return fail("high surrogate without low surrogate");
                    }
                    pos_ += 2;
                    std::uint32_t low = 0;
                    if (!readHex(low) || low < 0xDC00U || low > 0xDFFFU) {
                        return fail("invalid low surrogate");
                    }
                    codepoint = 0x10000U + ((codepoint - 0xD800U) << 10U) + (low - 0xDC00U);
                } else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU) {
                    return fail("unexpected low surrogate");
                }
                if (codepoint <= 0x7FU) {
                    out.push_back(static_cast<char>(codepoint));
                } else if (codepoint <= 0x7FFU) {
                    out.push_back(static_cast<char>(0xC0U | (codepoint >> 6U)));
                    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                } else if (codepoint <= 0xFFFFU) {
                    out.push_back(static_cast<char>(0xE0U | (codepoint >> 12U)));
                    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                } else {
                    out.push_back(static_cast<char>(0xF0U | (codepoint >> 18U)));
                    out.push_back(static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3FU)));
                    out.push_back(static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU)));
                    out.push_back(static_cast<char>(0x80U | (codepoint & 0x3FU)));
                }
                break;
            }
            default:
                --pos_;
                return fail("unknown string escape");
        }
    }
    return fail("unterminated string");
}

bool JsonParser::parseNumber(double& out) {
    const std::size_t start = pos_;
    if (peek() == '-') {
        ++pos_;
    }
    if (atEnd()) {
        return fail("invalid number");
    }
    if (peek() == '0') {
        ++pos_;
        if (!atEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            return fail("leading zero in number");
        }
    } else if (peek() >= '1' && peek() <= '9') {
        while (!atEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
    } else {
        return fail("invalid number");
    }
    if (!atEnd() && text_[pos_] == '.') {
        ++pos_;
        const std::size_t digits = pos_;
        while (!atEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == digits) {
            return fail("fraction has no digits");
        }
    }
    if (!atEnd() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
        ++pos_;
        if (!atEnd() && (text_[pos_] == '+' || text_[pos_] == '-')) {
            ++pos_;
        }
        const std::size_t digits = pos_;
        while (!atEnd() && text_[pos_] >= '0' && text_[pos_] <= '9') {
            ++pos_;
        }
        if (pos_ == digits) {
            return fail("exponent has no digits");
        }
    }
    const std::string token(text_.substr(start, pos_ - start));
    char* end = nullptr;
    errno = 0;
    out = std::strtod(token.c_str(), &end);
    if (errno == ERANGE || end != token.c_str() + token.size() || !std::isfinite(out)) {
        return fail("number is out of range");
    }
    return true;
}

std::string jsonEscape(std::string_view value) {
    std::string out;
    out.reserve(value.size() + 2);
    out.push_back('"');
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char hex[] = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4U) & 0xFU]);
                    out.push_back(hex[c & 0xFU]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    out.push_back('"');
    return out;
}

std::string jsonNumber(double value) {
    if (!std::isfinite(value)) {
        return "null";
    }
    char buffer[64];
    const int length = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    return length > 0 ? std::string(buffer, static_cast<std::size_t>(length)) : "0";
}

}  // namespace rr
