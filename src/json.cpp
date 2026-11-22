#include "skincheck/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace skincheck {
namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}

    Json parse() {
        skip_space();
        Json value = parse_value();
        skip_space();
        if (pos_ != text_.size()) {
            fail("trailing input");
        }
        return value;
    }

private:
    const std::string& text_;
    std::size_t pos_ = 0;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON error at byte " + std::to_string(pos_) + ": " + message);
    }

    void skip_space() {
        while (pos_ < text_.size()) {
            const char ch = text_[pos_];
            if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') {
                break;
            }
            ++pos_;
        }
    }

    bool consume(char expected) {
        if (pos_ < text_.size() && text_[pos_] == expected) {
            ++pos_;
            return true;
        }
        return false;
    }

    void expect(char expected) {
        if (!consume(expected)) {
            fail(std::string("expected '") + expected + "'");
        }
    }

    Json parse_value() {
        if (pos_ >= text_.size()) {
            fail("unexpected end of input");
        }
        switch (text_[pos_]) {
            case 'n': return parse_literal("null", Json());
            case 't': return parse_literal("true", Json(true));
            case 'f': return parse_literal("false", Json(false));
            case '"': return Json(parse_string());
            case '[': return parse_array();
            case '{': return parse_object();
            default:
                if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
                    return parse_number();
                }
                fail("unexpected token");
        }
    }

    Json parse_literal(std::string_view literal, Json value) {
        if (text_.compare(pos_, literal.size(), literal) != 0) {
            fail("invalid literal");
        }
        pos_ += literal.size();
        return value;
    }

    Json parse_number() {
        const std::size_t begin = pos_;
        consume('-');
        if (pos_ >= text_.size()) {
            fail("incomplete number");
        }
        if (text_[pos_] == '0') {
            ++pos_;
        } else if (text_[pos_] >= '1' && text_[pos_] <= '9') {
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        } else {
            fail("invalid number");
        }
        if (consume('.')) {
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                fail("missing fraction digits");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
                fail("missing exponent digits");
            }
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        const std::string token = text_.substr(begin, pos_ - begin);
        try {
            std::size_t used = 0;
            const double number = std::stod(token, &used);
            if (used != token.size() || !std::isfinite(number)) {
                fail("number is not finite");
            }
        } catch (const std::exception&) {
            fail("number out of range");
        }
        return Json::number_from_token(token);
    }

    std::string parse_string() {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            const unsigned char ch = static_cast<unsigned char>(text_[pos_++]);
            if (ch == '"') return out;
            if (ch < 0x20) fail("unescaped control character");
            if (ch != '\\') {
                out.push_back(static_cast<char>(ch));
                continue;
            }
            if (pos_ >= text_.size()) fail("unterminated escape");
            switch (text_[pos_++]) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': append_unicode(out); break;
                default: fail("unknown escape");
            }
        }
        fail("unterminated string");
    }

    unsigned parse_hex4() {
        if (pos_ + 4 > text_.size()) fail("short unicode escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            const char ch = text_[pos_++];
            value <<= 4;
            if (ch >= '0' && ch <= '9') value += static_cast<unsigned>(ch - '0');
            else if (ch >= 'a' && ch <= 'f') value += static_cast<unsigned>(ch - 'a' + 10);
            else if (ch >= 'A' && ch <= 'F') value += static_cast<unsigned>(ch - 'A' + 10);
            else fail("invalid unicode escape");
        }
        return value;
    }

    void append_utf8(std::string& out, unsigned code_point) {
        if (code_point <= 0x7F) {
            out.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else if (code_point <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
        }
    }

    void append_unicode(std::string& out) {
        unsigned code_point = parse_hex4();
        if (code_point >= 0xD800 && code_point <= 0xDBFF) {
            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' || text_[pos_ + 1] != 'u') {
                fail("missing low surrogate");
            }
            pos_ += 2;
            const unsigned low = parse_hex4();
            if (low < 0xDC00 || low > 0xDFFF) fail("invalid low surrogate");
            code_point = 0x10000 + ((code_point - 0xD800) << 10) + (low - 0xDC00);
        } else if (code_point >= 0xDC00 && code_point <= 0xDFFF) {
            fail("unexpected low surrogate");
        }
        append_utf8(out, code_point);
    }

    Json parse_array() {
        expect('[');
        Json out = Json::array();
        skip_space();
        if (consume(']')) return out;
        while (true) {
            skip_space();
            out.push(parse_value());
            skip_space();
            if (consume(']')) return out;
            expect(',');
        }
    }

    Json parse_object() {
        expect('{');
        Json out = Json::object();
        skip_space();
        if (consume('}')) return out;
        while (true) {
            skip_space();
            if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected object key");
            const std::string key = parse_string();
            if (out.contains(key)) fail("duplicate object key: " + key);
            skip_space();
            expect(':');
            skip_space();
            out.set(key, parse_value());
            skip_space();
            if (consume('}')) return out;
            expect(',');
        }
    }
};

void append_indent(std::string& out, int depth) {
    out.append(static_cast<std::size_t>(depth) * 2, ' ');
}

void append_escaped(std::string& out, const std::string& value) {
    out.push_back('"');
    static const char* hex = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (ch < 0x20) {
                    out += "\\u00";
                    out.push_back(hex[ch >> 4]);
                    out.push_back(hex[ch & 0x0F]);
                } else {
                    out.push_back(static_cast<char>(ch));
                }
        }
    }
    out.push_back('"');
}

}  // namespace

Json::Json() = default;
Json::Json(bool value) : type_(Type::Bool), bool_(value) {}
Json::Json(int value) : type_(Type::Number), scalar_(std::to_string(value)) {}
Json::Json(std::int64_t value) : type_(Type::Number), scalar_(std::to_string(value)) {}

Json::Json(double value) : type_(Type::Number) {
    std::ostringstream stream;
    stream << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
    scalar_ = stream.str();
}

Json::Json(const char* value) : type_(Type::String), scalar_(value) {}
Json::Json(std::string value) : type_(Type::String), scalar_(std::move(value)) {}

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

Json Json::number_from_token(std::string token) {
    Json value;
    value.type_ = Type::Number;
    value.scalar_ = std::move(token);
    return value;
}

Json::Type Json::type() const { return type_; }
bool Json::is_null() const { return type_ == Type::Null; }
bool Json::is_bool() const { return type_ == Type::Bool; }
bool Json::is_number() const { return type_ == Type::Number; }
bool Json::is_string() const { return type_ == Type::String; }
bool Json::is_array() const { return type_ == Type::Array; }
bool Json::is_object() const { return type_ == Type::Object; }

bool Json::as_bool() const {
    if (!is_bool()) throw std::logic_error("JSON value is not a bool");
    return bool_;
}

std::int64_t Json::as_int64() const {
    if (!is_number()) throw std::logic_error("JSON value is not a number");
    try {
        std::size_t used = 0;
        const long long value = std::stoll(scalar_, &used);
        if (used != scalar_.size()) throw std::invalid_argument("fraction");
        return static_cast<std::int64_t>(value);
    } catch (const std::exception&) {
        throw std::logic_error("JSON number is not an int64: " + scalar_);
    }
}

double Json::as_double() const {
    if (!is_number()) throw std::logic_error("JSON value is not a number");
    return std::stod(scalar_);
}

const std::string& Json::as_string() const {
    if (!is_string()) throw std::logic_error("JSON value is not a string");
    return scalar_;
}

std::vector<Json>& Json::items() {
    if (!is_array()) throw std::logic_error("JSON value is not an array");
    return items_;
}

const std::vector<Json>& Json::items() const {
    if (!is_array()) throw std::logic_error("JSON value is not an array");
    return items_;
}

std::vector<std::pair<std::string, Json>>& Json::fields() {
    if (!is_object()) throw std::logic_error("JSON value is not an object");
    return fields_;
}

const std::vector<std::pair<std::string, Json>>& Json::fields() const {
    if (!is_object()) throw std::logic_error("JSON value is not an object");
    return fields_;
}

std::vector<std::string> Json::keys() const {
    std::vector<std::string> result;
    result.reserve(fields_.size());
    for (const auto& field : fields_) result.push_back(field.first);
    return result;
}

Json& Json::push(Json value) {
    if (!is_array()) throw std::logic_error("JSON value is not an array");
    items_.push_back(std::move(value));
    return items_.back();
}

Json& Json::set(std::string key, Json value) {
    if (!is_object()) throw std::logic_error("JSON value is not an object");
    for (auto& field : fields_) {
        if (field.first == key) {
            field.second = std::move(value);
            return field.second;
        }
    }
    fields_.emplace_back(std::move(key), std::move(value));
    return fields_.back().second;
}

const Json* Json::find(std::string_view key) const {
    if (!is_object()) return nullptr;
    for (const auto& field : fields_) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

Json* Json::find(std::string_view key) {
    if (!is_object()) return nullptr;
    for (auto& field : fields_) {
        if (field.first == key) return &field.second;
    }
    return nullptr;
}

bool Json::contains(std::string_view key) const { return find(key) != nullptr; }

std::string Json::dump(int indent) const {
    std::string out;
    const bool pretty = indent > 0;

    const auto write_value = [&](const Json& value, auto&& self, int depth) -> void {
        switch (value.type_) {
            case Type::Null:
                out += "null";
                break;
            case Type::Bool:
                out += value.bool_ ? "true" : "false";
                break;
            case Type::Number:
                out += value.scalar_.empty() ? "0" : value.scalar_;
                break;
            case Type::String:
                append_escaped(out, value.scalar_);
                break;
            case Type::Array: {
                out.push_back('[');
                if (!value.items_.empty()) {
                    if (pretty) out.push_back('\n');
                    for (std::size_t i = 0; i < value.items_.size(); ++i) {
                        if (i != 0) {
                            out.push_back(',');
                            if (pretty) out.push_back('\n');
                        }
                        if (pretty) append_indent(out, depth + 1);
                        self(value.items_[i], self, depth + 1);
                    }
                    if (pretty) {
                        out.push_back('\n');
                        append_indent(out, depth);
                    }
                }
                out.push_back(']');
                break;
            }
            case Type::Object: {
                out.push_back('{');
                if (!value.fields_.empty()) {
                    if (pretty) out.push_back('\n');
                    for (std::size_t i = 0; i < value.fields_.size(); ++i) {
                        if (i != 0) {
                            out.push_back(',');
                            if (pretty) out.push_back('\n');
                        }
                        if (pretty) append_indent(out, depth + 1);
                        append_escaped(out, value.fields_[i].first);
                        out += pretty ? ": " : ":";
                        self(value.fields_[i].second, self, depth + 1);
                    }
                    if (pretty) {
                        out.push_back('\n');
                        append_indent(out, depth);
                    }
                }
                out.push_back('}');
                break;
            }
        }
    };

    write_value(*this, write_value, 0);
    return out;
}

Json Json::parse(const std::string& text, std::string* error) {
    try {
        return Parser(text).parse();
    } catch (const std::exception& exception) {
        if (error != nullptr) *error = exception.what();
        return Json();
    }
}

}  // namespace skincheck
