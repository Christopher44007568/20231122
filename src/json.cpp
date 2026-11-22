#include "json.h"

#include <cmath>
#include <climits>
#include <iomanip>
#include <sstream>
#include <stdexcept>

Json::Json() : type_(Type::Null), bool_value_(false), int_value_(0), real_value_(0.0) {}
Json::Json(bool value) : Json() { type_ = Type::Bool; bool_value_ = value; }
Json::Json(int value) : Json(static_cast<int64_t>(value)) {}
Json::Json(int64_t value) : Json() { type_ = Type::Int; int_value_ = value; }
Json::Json(uint32_t value) : Json() { type_ = Type::Int; int_value_ = static_cast<int64_t>(value); }
Json::Json(uint64_t value) : Json() {
    if (value > static_cast<uint64_t>(INT64_MAX)) throw std::runtime_error("JSON integer overflow");
    type_ = Type::Int;
    int_value_ = static_cast<int64_t>(value);
}
Json::Json(double value) : Json() { type_ = Type::Real; real_value_ = value; }
Json::Json(const char* value) : Json(std::string(value)) {}
Json::Json(std::string value) : Json() { type_ = Type::String; string_value_ = std::move(value); }

Json Json::array() { Json value; value.type_ = Type::Array; return value; }
Json Json::object() { Json value; value.type_ = Type::Object; return value; }

Json::Type Json::type() const { return type_; }
bool Json::isNull() const { return type_ == Type::Null; }
bool Json::isObject() const { return type_ == Type::Object; }
bool Json::isArray() const { return type_ == Type::Array; }

bool Json::asBool() const {
    if (type_ != Type::Bool) throw std::runtime_error("JSON value is not a bool");
    return bool_value_;
}

int64_t Json::asInt64() const {
    if (type_ == Type::Int) return int_value_;
    if (type_ == Type::Real && std::floor(real_value_) == real_value_) {
        return static_cast<int64_t>(real_value_);
    }
    throw std::runtime_error("JSON value is not an integer");
}

double Json::asDouble() const {
    if (type_ == Type::Real) return real_value_;
    if (type_ == Type::Int) return static_cast<double>(int_value_);
    throw std::runtime_error("JSON value is not numeric");
}

const std::string& Json::asString() const {
    if (type_ != Type::String) throw std::runtime_error("JSON value is not a string");
    return string_value_;
}

const std::vector<Json>& Json::items() const {
    if (type_ != Type::Array) throw std::runtime_error("JSON value is not an array");
    return array_value_;
}

const std::vector<std::pair<std::string, Json>>& Json::members() const {
    if (type_ != Type::Object) throw std::runtime_error("JSON value is not an object");
    return object_value_;
}

Json& Json::set(std::string key, Json value) {
    if (type_ != Type::Object) throw std::runtime_error("JSON value is not an object");
    for (auto& member : object_value_) {
        if (member.first == key) {
            member.second = std::move(value);
            return *this;
        }
    }
    object_value_.emplace_back(std::move(key), std::move(value));
    return *this;
}

Json& Json::push(Json value) {
    if (type_ != Type::Array) throw std::runtime_error("JSON value is not an array");
    array_value_.push_back(std::move(value));
    return *this;
}

const Json* Json::get(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& member : object_value_) {
        if (member.first == key) return &member.second;
    }
    return nullptr;
}

const Json& Json::at(size_t index) const {
    if (type_ != Type::Array || index >= array_value_.size()) {
        throw std::runtime_error("JSON array index out of range");
    }
    return array_value_[index];
}

namespace {

class Parser {
public:
    explicit Parser(const std::string& text) : text_(text), pos_(0) {}

    Json parse() {
        skipSpace();
        Json value = parseValue();
        skipSpace();
        if (pos_ != text_.size()) fail("trailing data");
        return value;
    }

private:
    const std::string& text_;
    size_t pos_;

    [[noreturn]] void fail(const std::string& message) const {
        throw std::runtime_error("JSON parse error at byte " + std::to_string(pos_) + ": " + message);
    }

    void skipSpace() {
        while (pos_ < text_.size()) {
            char c = text_[pos_];
            if (c != ' ' && c != '\n' && c != '\r' && c != '\t') break;
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

    Json parseValue() {
        if (pos_ >= text_.size()) fail("unexpected end");
        char c = text_[pos_];
        if (c == '{') return parseObject();
        if (c == '[') return parseArray();
        if (c == '"') return Json(parseString());
        if (c == 't') return parseLiteral("true", Json(true));
        if (c == 'f') return parseLiteral("false", Json(false));
        if (c == 'n') return parseLiteral("null", Json());
        return parseNumber();
    }

    Json parseLiteral(const char* literal, Json value) {
        size_t length = std::char_traits<char>::length(literal);
        if (text_.compare(pos_, length, literal) != 0) fail("invalid literal");
        pos_ += length;
        return value;
    }

    Json parseObject() {
        ++pos_;
        Json result = Json::object();
        skipSpace();
        if (consume('}')) return result;
        while (true) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"') fail("object key expected");
            std::string key = parseString();
            skipSpace();
            if (!consume(':')) fail("colon expected");
            skipSpace();
            result.set(std::move(key), parseValue());
            skipSpace();
            if (consume('}')) break;
            if (!consume(',')) fail("comma expected");
        }
        return result;
    }

    Json parseArray() {
        ++pos_;
        Json result = Json::array();
        skipSpace();
        if (consume(']')) return result;
        while (true) {
            skipSpace();
            result.push(parseValue());
            skipSpace();
            if (consume(']')) break;
            if (!consume(',')) fail("comma expected");
        }
        return result;
    }

    std::string parseString() {
        if (!consume('"')) fail("string expected");
        std::string result;
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return result;
            if (c != '\\') {
                result.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) fail("escaped string ended");
            char escaped = text_[pos_++];
            switch (escaped) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': result.append(parseUnicode()); break;
                default: fail("invalid escape");
            }
        }
        fail("unterminated string");
    }

    std::string parseUnicode() {
        if (pos_ + 4 > text_.size()) fail("short unicode escape");
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9') value += static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') value += static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') value += static_cast<unsigned>(c - 'A' + 10);
            else fail("invalid unicode escape");
        }
        std::string out;
        if (value <= 0x7F) {
            out.push_back(static_cast<char>(value));
        } else if (value <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (value >> 6)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (value >> 12)));
            out.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (value & 0x3F)));
        }
        return out;
    }

    Json parseNumber() {
        size_t start = pos_;
        if (consume('-')) {}
        if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') fail("number expected");
        while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        bool real = false;
        if (consume('.')) {
            real = true;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
            real = true;
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) ++pos_;
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') ++pos_;
        }
        std::string token = text_.substr(start, pos_ - start);
        try {
            if (real) return Json(std::stod(token));
            return Json(static_cast<int64_t>(std::stoll(token)));
        } catch (const std::exception&) {
            fail("invalid number");
        }
    }
};

void appendEscaped(std::ostringstream& out, const std::string& value) {
    out << '"';
    for (unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    out << '"';
}

void writeJson(std::ostringstream& out, const Json& value, bool pretty, int depth) {
    auto indent = [&](int level) {
        if (pretty) out << std::string(static_cast<size_t>(level) * 2, ' ');
    };
    switch (value.type()) {
        case Json::Type::Null: out << "null"; break;
        case Json::Type::Bool: out << (value.asBool() ? "true" : "false"); break;
        case Json::Type::Int: out << value.asInt64(); break;
        case Json::Type::Real: {
            std::ostringstream number;
            number << std::setprecision(17) << value.asDouble();
            std::string text = number.str();
            if (text.find_first_of(".eE") == std::string::npos) text += ".0";
            out << text;
            break;
        }
        case Json::Type::String: appendEscaped(out, value.asString()); break;
        case Json::Type::Array: {
            if (value.items().empty()) {
                out << "[]";
                break;
            }
            out << '[';
            if (pretty) out << '\n';
            for (size_t i = 0; i < value.items().size(); ++i) {
                indent(depth + 1);
                writeJson(out, value.items()[i], pretty, depth + 1);
                if (i + 1 < value.items().size()) out << ',';
                if (pretty) out << '\n';
            }
            indent(depth);
            out << ']';
            break;
        }
        case Json::Type::Object: {
            if (value.members().empty()) {
                out << "{}";
                break;
            }
            out << '{';
            if (pretty) out << '\n';
            for (size_t i = 0; i < value.members().size(); ++i) {
                indent(depth + 1);
                appendEscaped(out, value.members()[i].first);
                out << (pretty ? ": " : ":");
                writeJson(out, value.members()[i].second, pretty, depth + 1);
                if (i + 1 < value.members().size()) out << ',';
                if (pretty) out << '\n';
            }
            indent(depth);
            out << '}';
            break;
        }
    }
}

}  // namespace

Json parseJson(const std::string& text) {
    return Parser(text).parse();
}

std::string dumpJson(const Json& value, bool pretty) {
    std::ostringstream out;
    writeJson(out, value, pretty, 0);
    if (pretty) out << '\n';
    return out.str();
}

