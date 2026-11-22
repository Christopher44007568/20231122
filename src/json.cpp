#include "json.hpp"

#include <cctype>
#include <sstream>

namespace minijson {

namespace {

class Parser {
public:
    Parser(const std::string& text) : text_(text) {}

    Value parse(std::string* error) {
        skipWs();
        Value value = parseValue(error);
        if (error && !error->empty()) return Value{};
        skipWs();
        if (pos_ != text_.size()) fail(error, "尾部存在多余字符");
        return value;
    }

private:
    const std::string& text_;
    size_t pos_ = 0;

    void fail(std::string* error, const std::string& message) {
        if (error && error->empty()) {
            std::ostringstream oss;
            oss << "JSON 解析失败(位置 " << pos_ << "): " << message;
            *error = oss.str();
        }
    }

    char peek() const { return pos_ < text_.size() ? text_[pos_] : '\0'; }

    void skipWs() {
        while (pos_ < text_.size() &&
               std::isspace(static_cast<unsigned char>(text_[pos_]))) {
            ++pos_;
        }
    }

    bool consume(char expected, std::string* error) {
        if (peek() != expected) {
            fail(error, std::string("缺少 '") + expected + "'");
            return false;
        }
        ++pos_;
        return true;
    }

    Value parseValue(std::string* error) {
        skipWs();
        char c = peek();
        if (c == '{') return parseObject(error);
        if (c == '[') return parseArray(error);
        if (c == '"') {
            Value v;
            v.type = Value::Type::String;
            v.str = parseString(error);
            return v;
        }
        if (c == 't' || c == 'f') return parseBool(error);
        if (c == 'n') return parseNull(error);
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c))) {
            return parseNumber(error);
        }
        fail(error, "无法识别的 token");
        return Value{};
    }

    Value parseObject(std::string* error) {
        Value v;
        v.type = Value::Type::Object;
        ++pos_;  // {
        skipWs();
        if (peek() == '}') { ++pos_; return v; }
        while (true) {
            skipWs();
            if (peek() != '"') { fail(error, "对象键必须是字符串"); return v; }
            std::string key = parseString(error);
            skipWs();
            if (!consume(':', error)) return v;
            Value child = parseValue(error);
            if (error && !error->empty()) return v;
            v.object.emplace(std::move(key), std::move(child));
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (!consume('}', error)) return v;
            return v;
        }
    }

    Value parseArray(std::string* error) {
        Value v;
        v.type = Value::Type::Array;
        ++pos_;  // [
        skipWs();
        if (peek() == ']') { ++pos_; return v; }
        while (true) {
            Value child = parseValue(error);
            if (error && !error->empty()) return v;
            v.array.push_back(std::move(child));
            skipWs();
            if (peek() == ',') { ++pos_; continue; }
            if (!consume(']', error)) return v;
            return v;
        }
    }

    std::string parseString(std::string* error) {
        std::string out;
        ++pos_;  // 起始引号
        while (pos_ < text_.size()) {
            char c = text_[pos_++];
            if (c == '"') return out;
            if (c == '\\') {
                if (pos_ >= text_.size()) break;
                char e = text_[pos_++];
                switch (e) {
                    case '"': out.push_back('"'); break;
                    case '\\': out.push_back('\\'); break;
                    case '/': out.push_back('/'); break;
                    case 'b': out.push_back('\b'); break;
                    case 'f': out.push_back('\f'); break;
                    case 'n': out.push_back('\n'); break;
                    case 'r': out.push_back('\r'); break;
                    case 't': out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > text_.size()) { fail(error, "非法 \\u 转义"); return out; }
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = text_[pos_++];
                            code <<= 4;
                            if (h >= '0' && h <= '9') code |= static_cast<unsigned>(h - '0');
                            else if (h >= 'a' && h <= 'f') code |= static_cast<unsigned>(h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') code |= static_cast<unsigned>(h - 'A' + 10);
                            else { fail(error, "非法十六进制位"); return out; }
                        }
                        // 仅处理 BMP；配置场景足够，surrogate 对按替换字符处理
                        if (code < 0x80) out.push_back(static_cast<char>(code));
                        else if (code < 0x800) {
                            out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out.push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default: fail(error, "非法转义字符"); return out;
                }
            } else {
                out.push_back(c);
            }
        }
        fail(error, "字符串未闭合");
        return out;
    }

    Value parseBool(std::string* error) {
        Value v;
        v.type = Value::Type::Bool;
        if (text_.compare(pos_, 4, "true") == 0) {
            v.boolean = true;
            pos_ += 4;
        } else if (text_.compare(pos_, 5, "false") == 0) {
            v.boolean = false;
            pos_ += 5;
        } else {
            fail(error, "非法布尔值");
        }
        return v;
    }

    Value parseNull(std::string* error) {
        Value v;
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
        } else {
            fail(error, "非法 null");
        }
        return v;
    }

    Value parseNumber(std::string* error) {
        size_t start = pos_;
        if (peek() == '-') ++pos_;
        while (std::isdigit(static_cast<unsigned char>(peek())) ||
               peek() == '.' || peek() == 'e' || peek() == 'E' ||
               peek() == '+' || peek() == '-') {
            ++pos_;
        }
        Value v;
        v.type = Value::Type::Number;
        try {
            v.number = std::stod(text_.substr(start, pos_ - start));
        } catch (...) {
            fail(error, "非法数字");
        }
        return v;
    }
};

}  // namespace

Value parse(const std::string& text, std::string* error) {
    if (error) error->clear();
    return Parser(text).parse(error);
}

const Value& Value::get(const std::string& key) const {
    static const Value kNull;
    auto it = object.find(key);
    return it == object.end() ? kNull : it->second;
}

std::string Value::asString(const std::string& fallback) const {
    return type == Type::String ? str : fallback;
}

double Value::asNumber(double fallback) const {
    return type == Type::Number ? number : fallback;
}

bool Value::asBool(bool fallback) const {
    return type == Type::Bool ? boolean : fallback;
}

std::string escape(const std::string& raw) {
    std::string out;
    out.reserve(raw.size() + 2);
    for (char c : raw) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

}  // namespace minijson
