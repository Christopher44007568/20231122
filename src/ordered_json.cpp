#include "ordered_json.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace ph {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    bool parse(Json& out, std::string& error) {
        skipSpace();
        if (!parseValue(out, 0)) {
            error = error_;
            return false;
        }
        skipSpace();
        if (pos_ != text_.size()) {
            error_ = "trailing data at byte " + std::to_string(pos_);
            error = error_;
            return false;
        }
        return true;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;
    std::string error_;

    void skipSpace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
                break;
            }
            ++pos_;
        }
    }

    bool fail(const std::string& message) {
        if (error_.empty()) {
            error_ = message + " at byte " + std::to_string(pos_);
        }
        return false;
    }

    bool consume(char expected) {
        if (pos_ >= text_.size() || text_[pos_] != expected) {
            return fail(std::string("expected '") + expected + "'");
        }
        ++pos_;
        return true;
    }

    bool parseValue(Json& out, int depth) {
        if (depth > 128) {
            return fail("nesting too deep");
        }
        skipSpace();
        if (pos_ >= text_.size()) {
            return fail("unexpected end of input");
        }
        const char c = text_[pos_];
        if (c == '{') {
            return parseObject(out, depth + 1);
        }
        if (c == '[') {
            return parseArray(out, depth + 1);
        }
        if (c == '"') {
            std::string value;
            if (!parseString(value)) {
                return false;
            }
            out = Json(std::move(value));
            return true;
        }
        if (c == 't' && text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            out = Json(true);
            return true;
        }
        if (c == 'f' && text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            out = Json(false);
            return true;
        }
        if (c == 'n' && text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            out = Json();
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            return parseNumber(out);
        }
        return fail("unexpected character");
    }

    bool parseObject(Json& out, int depth) {
        if (!consume('{')) {
            return false;
        }
        out = Json::object();
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        while (true) {
            skipSpace();
            std::string key;
            if (!parseString(key)) {
                return false;
            }
            skipSpace();
            if (!consume(':')) {
                return false;
            }
            Json value;
            if (!parseValue(value, depth)) {
                return false;
            }
            if (out.find(key) != nullptr) {
                return fail("duplicate object key '" + key + "'");
            }
            out.set(std::move(key), std::move(value));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    bool parseArray(Json& out, int depth) {
        if (!consume('[')) {
            return false;
        }
        out = Json::array();
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        while (true) {
            Json value;
            if (!parseValue(value, depth)) {
                return false;
            }
            out.push(std::move(value));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            if (!consume(',')) {
                return false;
            }
        }
    }

    static void appendUtf8(std::string& out, uint32_t cp) {
        if (cp <= 0x7f) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7ff) {
            out.push_back(static_cast<char>(0xc0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else if (cp <= 0xffff) {
            out.push_back(static_cast<char>(0xe0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        } else {
            out.push_back(static_cast<char>(0xf0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3f)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3f)));
        }
    }

    bool parseHex4(uint32_t& value) {
        if (pos_ + 4 > text_.size()) {
            return fail("truncated unicode escape");
        }
        value = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') {
                value |= static_cast<uint32_t>(c - '0');
            } else if (c >= 'a' && c <= 'f') {
                value |= static_cast<uint32_t>(c - 'a' + 10);
            } else if (c >= 'A' && c <= 'F') {
                value |= static_cast<uint32_t>(c - 'A' + 10);
            } else {
                return fail("invalid unicode escape");
            }
        }
        return true;
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            return false;
        }
        out.clear();
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"') {
                return true;
            }
            if (c < 0x20) {
                return fail("control character in string");
            }
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos_ >= text_.size()) {
                return fail("truncated escape");
            }
            const char esc = text_[pos_++];
            switch (esc) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    uint32_t cp = 0;
                    if (!parseHex4(cp)) {
                        return false;
                    }
                    if (cp >= 0xd800 && cp <= 0xdbff) {
                        if (pos_ + 2 > text_.size() || text_[pos_] != '\\' ||
                            text_[pos_ + 1] != 'u') {
                            return fail("missing low surrogate");
                        }
                        pos_ += 2;
                        uint32_t low = 0;
                        if (!parseHex4(low) || low < 0xdc00 || low > 0xdfff) {
                            return fail("invalid low surrogate");
                        }
                        cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
                    } else if (cp >= 0xdc00 && cp <= 0xdfff) {
                        return fail("unexpected low surrogate");
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    return fail("unknown escape");
            }
        }
        return fail("unterminated string");
    }

    bool parseNumber(Json& out) {
        const size_t start = pos_;
        if (text_[pos_] == '-') {
            ++pos_;
        }
        if (pos_ >= text_.size()) {
            return fail("truncated number");
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
        } else {
            return fail("invalid number");
        }
        if (pos_ < text_.size() && text_[pos_] == '.') {
            ++pos_;
            const size_t fraction = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            if (fraction == pos_) {
                return fail("missing fraction digits");
            }
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
                ++pos_;
            }
            const size_t exponent = pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
                ++pos_;
            }
            if (exponent == pos_) {
                return fail("missing exponent digits");
            }
        }
        const std::string token = text_.substr(start, pos_ - start);
        errno = 0;
        char* end = nullptr;
        const double value = std::strtod(token.c_str(), &end);
        if (errno == ERANGE || end == nullptr || *end != '\0' || !std::isfinite(value)) {
            return fail("number out of range");
        }
        out = Json::makeNumberToken(token);
        return true;
    }
};

}  // namespace

Json::Json() = default;
Json::Json(bool value) : type_(Type::Bool), bool_(value) {}
Json::Json(const char* value) : type_(Type::String), string_(value == nullptr ? "" : value) {}
Json::Json(std::string value) : type_(Type::String), string_(std::move(value)) {}
Json::Json(int value) : Json(static_cast<int64_t>(value)) {}
Json::Json(int64_t value) : type_(Type::Number), number_(std::to_string(value)) {}
Json::Json(uint32_t value) : Json(static_cast<uint64_t>(value)) {}
Json::Json(uint64_t value) : type_(Type::Number), number_(std::to_string(value)) {}

Json::Json(double value, int precision) : type_(Type::Number) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument("JSON cannot encode non-finite numbers");
    }
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << value;
    std::string text = stream.str();
    while (text.size() > 1 && text.back() == '0') {
        text.pop_back();
    }
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    if (text == "-0") {
        text = "0";
    }
    number_ = std::move(text);
}

Json Json::array() {
    Json value;
    value.type_ = Type::Array;
    return value;
}

Json Json::object() {
    Json value;
    value.type_ = Type::Object;
    return value;
}

Json Json::makeNumberToken(std::string token) {
    Json value;
    value.type_ = Type::Number;
    value.number_ = std::move(token);
    return value;
}

int64_t Json::asInt() const {
    if (!isNumber()) {
        throw std::runtime_error("JSON value is not a number");
    }
    size_t consumed = 0;
    const long long value = std::stoll(number_, &consumed, 10);
    if (consumed != number_.size()) {
        throw std::runtime_error("JSON number is not an integer: " + number_);
    }
    return static_cast<int64_t>(value);
}

uint64_t Json::asUInt() const {
    const int64_t value = asInt();
    if (value < 0) {
        throw std::runtime_error("negative JSON number used as unsigned");
    }
    return static_cast<uint64_t>(value);
}

double Json::asDouble() const {
    if (!isNumber()) {
        throw std::runtime_error("JSON value is not a number");
    }
    return std::stod(number_);
}

Json& Json::set(std::string key, Json value) {
    if (type_ != Type::Object) {
        type_ = Type::Object;
        object_.clear();
    }
    for (auto& field : object_) {
        if (field.first == key) {
            field.second = std::move(value);
            return *this;
        }
    }
    object_.emplace_back(std::move(key), std::move(value));
    return *this;
}

Json& Json::push(Json value) {
    if (type_ != Type::Array) {
        type_ = Type::Array;
        array_.clear();
    }
    array_.push_back(std::move(value));
    return *this;
}

const Json* Json::find(const std::string& key) const {
    if (!isObject()) {
        return nullptr;
    }
    for (const auto& field : object_) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

Json* Json::find(const std::string& key) {
    if (!isObject()) {
        return nullptr;
    }
    for (auto& field : object_) {
        if (field.first == key) {
            return &field.second;
        }
    }
    return nullptr;
}

bool Json::erase(const std::string& key) {
    if (!isObject()) {
        return false;
    }
    const auto it = std::find_if(object_.begin(), object_.end(),
                                 [&](const auto& field) { return field.first == key; });
    if (it == object_.end()) {
        return false;
    }
    object_.erase(it);
    return true;
}

size_t Json::size() const {
    if (isArray()) {
        return array_.size();
    }
    if (isObject()) {
        return object_.size();
    }
    return 0;
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> result;
    result.reserve(object_.size());
    for (const auto& field : object_) {
        result.push_back(field.first);
    }
    return result;
}

std::string escapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size() + 8);
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
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out.push_back(hex[(c >> 4) & 0xf]);
                    out.push_back(hex[c & 0xf]);
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

void Json::dumpCompactTo(std::string& out) const {
    switch (type_) {
        case Type::Null: out += "null"; break;
        case Type::Bool: out += bool_ ? "true" : "false"; break;
        case Type::Number: out += number_; break;
        case Type::String: out.push_back('"'); out += escapeJson(string_); out.push_back('"'); break;
        case Type::Array:
            out.push_back('[');
            for (size_t i = 0; i < array_.size(); ++i) {
                if (i != 0) out.push_back(',');
                array_[i].dumpCompactTo(out);
            }
            out.push_back(']');
            break;
        case Type::Object:
            out.push_back('{');
            for (size_t i = 0; i < object_.size(); ++i) {
                if (i != 0) out.push_back(',');
                out.push_back('"');
                out += escapeJson(object_[i].first);
                out += "\":";
                object_[i].second.dumpCompactTo(out);
            }
            out.push_back('}');
            break;
    }
}

void Json::dumpPrettyTo(std::string& out, int indent, int depth) const {
    const auto newline = [&](int level) {
        out.push_back('\n');
        out.append(static_cast<size_t>(indent * level), ' ');
    };
    switch (type_) {
        case Type::Null:
        case Type::Bool:
        case Type::Number:
        case Type::String:
            dumpCompactTo(out);
            break;
        case Type::Array:
            if (array_.empty()) {
                out += "[]";
                break;
            }
            out.push_back('[');
            for (size_t i = 0; i < array_.size(); ++i) {
                if (i != 0) out.push_back(',');
                newline(depth + 1);
                array_[i].dumpPrettyTo(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back(']');
            break;
        case Type::Object:
            if (object_.empty()) {
                out += "{}";
                break;
            }
            out.push_back('{');
            for (size_t i = 0; i < object_.size(); ++i) {
                if (i != 0) out.push_back(',');
                newline(depth + 1);
                out.push_back('"');
                out += escapeJson(object_[i].first);
                out += "\": ";
                object_[i].second.dumpPrettyTo(out, indent, depth + 1);
            }
            newline(depth);
            out.push_back('}');
            break;
    }
}

std::string Json::dumpCompact() const {
    std::string out;
    dumpCompactTo(out);
    return out;
}

std::string Json::dumpPretty(int indent) const {
    std::string out;
    out.reserve(4096);
    dumpPrettyTo(out, indent, 0);
    out.push_back('\n');
    return out;
}

bool Json::parse(const std::string& text, Json& out, std::string& error) {
    Parser parser(text);
    return parser.parse(out, error);
}

}  // namespace ph
