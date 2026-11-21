#include "core/json.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

namespace vfcr {

namespace {

void appendIndent(std::string& out, int depth) {
    out.append(static_cast<std::size_t>(depth) * 2U, ' ');
}

void appendUtf8(std::string& out, std::uint32_t codePoint) {
    if (codePoint <= 0x7FU) {
        out.push_back(static_cast<char>(codePoint));
    } else if (codePoint <= 0x7FFU) {
        out.push_back(static_cast<char>(0xC0U | (codePoint >> 6U)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else if (codePoint <= 0xFFFFU) {
        out.push_back(static_cast<char>(0xE0U | (codePoint >> 12U)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    } else {
        out.push_back(static_cast<char>(0xF0U | (codePoint >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }
}

struct Parser {
    std::string_view text;
    std::size_t pos = 0;
    std::string error;

    bool fail(const std::string& message) {
        if (error.empty()) {
            error = message + " at byte " + std::to_string(pos);
        }
        return false;
    }

    void skipSpace() {
        while (pos < text.size()) {
            const char c = text[pos];
            if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
                ++pos;
            } else {
                break;
            }
        }
    }

    bool parseHex4(std::uint32_t& value) {
        if (pos + 4U > text.size()) {
            return fail("truncated unicode escape");
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text[pos++];
            value <<= 4U;
            if (c >= '0' && c <= '9') {
                value |= static_cast<std::uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return fail("invalid unicode escape");
            }
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (pos >= text.size() || text[pos] != '"') {
            return fail("expected string");
        }
        ++pos;
        while (pos < text.size()) {
            const char c = text[pos++];
            if (c == '"') {
                return true;
            }
            if (static_cast<unsigned char>(c) < 0x20U) {
                return fail("control character in string");
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos >= text.size()) {
                return fail("truncated escape");
            }
            const char escaped = text[pos++];
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
                    std::uint32_t cp = 0;
                    if (!parseHex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xD800U && cp <= 0xDBFFU && pos + 6U <= text.size() &&
                        text[pos] == '\\' && text[pos + 1U] == 'u') {
                        pos += 2U;
                        std::uint32_t low = 0;
                        if (!parseHex4(low)) {
                            return false;
                        }
                        if (low >= 0xDC00U && low <= 0xDFFFU) {
                            cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
                        }
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail("invalid escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseValue(Json& out, int depth) {
        if (depth > 128) {
            return fail("nesting too deep");
        }
        skipSpace();
        if (pos >= text.size()) {
            return fail("unexpected end of input");
        }
        const char c = text[pos];
        if (c == '{') {
            ++pos;
            Json object = Json::object();
            skipSpace();
            if (pos < text.size() && text[pos] == '}') {
                ++pos;
                out = std::move(object);
                return true;
            }
            while (true) {
                skipSpace();
                std::string key;
                if (!parseString(key)) {
                    return false;
                }
                skipSpace();
                if (pos >= text.size() || text[pos] != ':') {
                    return fail("expected ':'");
                }
                ++pos;
                Json value;
                if (!parseValue(value, depth + 1)) {
                    return false;
                }
                object.set(std::move(key), std::move(value));
                skipSpace();
                if (pos >= text.size()) {
                    return fail("unterminated object");
                }
                if (text[pos] == '}') {
                    ++pos;
                    out = std::move(object);
                    return true;
                }
                if (text[pos] != ',') {
                    return fail("expected ',' or '}'");
                }
                ++pos;
            }
        }
        if (c == '[') {
            ++pos;
            Json array = Json::array();
            skipSpace();
            if (pos < text.size() && text[pos] == ']') {
                ++pos;
                out = std::move(array);
                return true;
            }
            while (true) {
                Json value;
                if (!parseValue(value, depth + 1)) {
                    return false;
                }
                array.push(std::move(value));
                skipSpace();
                if (pos >= text.size()) {
                    return fail("unterminated array");
                }
                if (text[pos] == ']') {
                    ++pos;
                    out = std::move(array);
                    return true;
                }
                if (text[pos] != ',') {
                    return fail("expected ',' or ']'");
                }
                ++pos;
            }
        }
        if (c == '"') {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = Json(std::move(value));
            return true;
        }
        if (text.compare(pos, 4U, "true") == 0) {
            pos += 4U;
            out = Json(true);
            return true;
        }
        if (text.compare(pos, 5U, "false") == 0) {
            pos += 5U;
            out = Json(false);
            return true;
        }
        if (text.compare(pos, 4U, "null") == 0) {
            pos += 4U;
            out = Json(nullptr);
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos;
            if (text[pos] == '-') {
                ++pos;
            }
            while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                ++pos;
            }
            if (pos < text.size() && text[pos] == '.') {
                ++pos;
                while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                    ++pos;
                }
            }
            if (pos < text.size() && (text[pos] == 'e' || text[pos] == 'E')) {
                ++pos;
                if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
                    ++pos;
                }
                while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
                    ++pos;
                }
            }
            const std::string number(text.substr(start, pos - start));
            try {
                out = Json(std::stod(number));
            } catch (const std::exception&) {
                return fail("invalid number");
            }
            return true;
        }
        return fail("unexpected token");
    }
};

}  // namespace

Json::Json() : storage_(nullptr) {}
Json::Json(std::nullptr_t) : storage_(nullptr) {}
Json::Json(bool value) : storage_(value) {}
Json::Json(int value) : storage_(static_cast<double>(value)) {}
Json::Json(std::int64_t value) : storage_(static_cast<double>(value)) {}
Json::Json(double value) : storage_(value) {}
Json::Json(const char* value) : storage_(std::string(value == nullptr ? "" : value)) {}
Json::Json(std::string value) : storage_(std::move(value)) {}

Json Json::object() {
    Json value;
    value.storage_ = Object{};
    return value;
}

Json Json::array() {
    Json value;
    value.storage_ = Array{};
    return value;
}

bool Json::isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
bool Json::isBool() const { return std::holds_alternative<bool>(storage_); }
bool Json::isNumber() const { return std::holds_alternative<double>(storage_); }
bool Json::isString() const { return std::holds_alternative<std::string>(storage_); }
bool Json::isArray() const { return std::holds_alternative<Array>(storage_); }
bool Json::isObject() const { return std::holds_alternative<Object>(storage_); }

bool Json::asBool(bool fallback) const {
    const auto* value = std::get_if<bool>(&storage_);
    return value == nullptr ? fallback : *value;
}

double Json::asNumber(double fallback) const {
    const auto* value = std::get_if<double>(&storage_);
    return value == nullptr ? fallback : *value;
}

std::int64_t Json::asInt(std::int64_t fallback) const {
    return isNumber() ? static_cast<std::int64_t>(asNumber()) : fallback;
}

const std::string& Json::asString() const {
    static const std::string empty;
    const auto* value = std::get_if<std::string>(&storage_);
    return value == nullptr ? empty : *value;
}

std::string Json::stringOr(const std::string& fallback) const {
    return isString() ? asString() : fallback;
}

const Json::Array& Json::arrayItems() const {
    static const Array empty;
    const auto* value = std::get_if<Array>(&storage_);
    return value == nullptr ? empty : *value;
}

Json::Array& Json::arrayItems() {
    return std::get<Array>(storage_);
}

const Json::Object& Json::objectItems() const {
    static const Object empty;
    const auto* value = std::get_if<Object>(&storage_);
    return value == nullptr ? empty : *value;
}

Json::Object& Json::objectItems() {
    return std::get<Object>(storage_);
}

Json& Json::set(std::string key, Json value) {
    Object& object = std::get<Object>(storage_);
    for (auto& item : object) {
        if (item.first == key) {
            item.second = std::move(value);
            return item.second;
        }
    }
    object.emplace_back(std::move(key), std::move(value));
    return object.back().second;
}

Json& Json::push(Json value) {
    Array& array = std::get<Array>(storage_);
    array.push_back(std::move(value));
    return array.back();
}

const Json* Json::find(std::string_view key) const {
    if (!isObject()) {
        return nullptr;
    }
    for (const auto& item : objectItems()) {
        if (item.first == key) {
            return &item.second;
        }
    }
    return nullptr;
}

Json* Json::find(std::string_view key) {
    if (!isObject()) {
        return nullptr;
    }
    for (auto& item : objectItems()) {
        if (item.first == key) {
            return &item.second;
        }
    }
    return nullptr;
}

bool Json::contains(std::string_view key) const {
    return find(key) != nullptr;
}

bool Json::erase(std::string_view key) {
    if (!isObject()) {
        return false;
    }
    auto& object = objectItems();
    for (auto it = object.begin(); it != object.end(); ++it) {
        if (it->first == key) {
            object.erase(it);
            return true;
        }
    }
    return false;
}

std::size_t Json::size() const {
    if (isArray()) {
        return arrayItems().size();
    }
    if (isObject()) {
        return objectItems().size();
    }
    return 0;
}

void Json::dumpTo(std::string& out, bool pretty, int depth) const {
    if (isNull()) {
        out += "null";
        return;
    }
    if (isBool()) {
        out += asBool() ? "true" : "false";
        return;
    }
    if (isNumber()) {
        const double value = asNumber();
        if (std::isfinite(value) && std::floor(value) == value &&
            value >= static_cast<double>(std::numeric_limits<std::int64_t>::min()) &&
            value <= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
            out += std::to_string(static_cast<std::int64_t>(value));
        } else {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.15g", value);
            out += buffer;
        }
        return;
    }
    if (isString()) {
        out.push_back('"');
        out += jsonEscape(asString());
        out.push_back('"');
        return;
    }
    if (isArray()) {
        out.push_back('[');
        const auto& items = arrayItems();
        for (std::size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                out.push_back(',');
            }
            if (pretty) {
                out.push_back('\n');
                appendIndent(out, depth + 1);
            }
            items[i].dumpTo(out, pretty, depth + 1);
        }
        if (pretty && !items.empty()) {
            out.push_back('\n');
            appendIndent(out, depth);
        }
        out.push_back(']');
        return;
    }
    out.push_back('{');
    const auto& items = objectItems();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (i != 0) {
            out.push_back(',');
        }
        if (pretty) {
            out.push_back('\n');
            appendIndent(out, depth + 1);
        }
        out.push_back('"');
        out += jsonEscape(items[i].first);
        out += pretty ? "\": " : "\":";
        items[i].second.dumpTo(out, pretty, depth + 1);
    }
    if (pretty && !items.empty()) {
        out.push_back('\n');
        appendIndent(out, depth);
    }
    out.push_back('}');
}

std::string Json::dump(bool pretty) const {
    std::string out;
    dumpTo(out, pretty, 0);
    return out;
}

Json Json::parse(std::string_view text, std::string* error) {
    Parser parser{text, 0, {}};
    Json value;
    if (!parser.parseValue(value, 0)) {
        if (error != nullptr) {
            *error = parser.error;
        }
        return Json();
    }
    parser.skipSpace();
    if (parser.pos != text.size()) {
        if (error != nullptr) {
            *error = "trailing content at byte " + std::to_string(parser.pos);
        }
        return Json();
    }
    if (error != nullptr) {
        error->clear();
    }
    return value;
}

std::string jsonEscape(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 8U);
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20U) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                    out += buffer;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    return out;
}

std::string canonicalJson(const Json& value) {
    return value.dump(false);
}

}  // namespace vfcr
