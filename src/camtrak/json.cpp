#include "camtrak/json.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace camtrak {

JsonValue JsonValue::makeBool(bool value) {
  JsonValue result(Type::Bool);
  result.bool_value_ = value;
  return result;
}

JsonValue JsonValue::makeNumber(double value) {
  JsonValue result(Type::Number);
  result.number_value_ = value;
  return result;
}

JsonValue JsonValue::makeString(std::string value) {
  JsonValue result(Type::String);
  result.string_value_ = std::move(value);
  return result;
}

JsonValue JsonValue::makeArray() { return JsonValue(Type::Array); }

JsonValue JsonValue::makeObject() { return JsonValue(Type::Object); }

bool JsonValue::asBool() const {
  return type_ == Type::Bool && bool_value_;
}

double JsonValue::asNumber() const { return number_value_; }

const std::string& JsonValue::asString() const { return string_value_; }

bool JsonValue::has(const std::string& key) const {
  return type_ == Type::Object &&
         object_index_.find(key) != object_index_.end();
}

const JsonValue& JsonValue::get(const std::string& key) const {
  static const JsonValue null_value;
  if (type_ != Type::Object) return null_value;
  auto found = object_index_.find(key);
  return found == object_index_.end() ? null_value
                                      : array_value_[found->second];
}

JsonValue& JsonValue::get(const std::string& key) {
  static JsonValue null_value;
  if (type_ != Type::Object) return null_value;
  auto found = object_index_.find(key);
  return found == object_index_.end() ? null_value
                                      : array_value_[found->second];
}

void JsonValue::set(const std::string& key, JsonValue value) {
  auto found = object_index_.find(key);
  if (found == object_index_.end()) {
    object_index_[key] = array_value_.size();
    object_order_.push_back(key);
    array_value_.push_back(std::move(value));
  } else {
    array_value_[found->second] = std::move(value);
  }
}

const std::vector<std::string>& JsonValue::keys() const {
  return object_order_;
}

std::size_t JsonValue::size() const { return array_value_.size(); }

const JsonValue& JsonValue::at(std::size_t index) const {
  return array_value_.at(index);
}

JsonValue& JsonValue::at(std::size_t index) {
  return array_value_.at(index);
}

void JsonValue::push(JsonValue value) {
  array_value_.push_back(std::move(value));
}

std::string jsonEscape(const std::string& raw) {
  std::string out;
  out.reserve(raw.size() + 2);
  out.push_back('"');
  for (char ch : raw) {
    const unsigned char byte = static_cast<unsigned char>(ch);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (byte < 0x20) {
          char buffer[8];
          std::snprintf(buffer, sizeof(buffer), "\\u%04x", byte);
          out += buffer;
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
  return out;
}

namespace {

std::string formatNumber(double value) {
  if (std::isfinite(value) && value == std::floor(value) &&
      std::abs(value) < 1e15) {
    long long integral = static_cast<long long>(value);
    return std::to_string(integral);
  }
  std::ostringstream stream;
  stream.precision(17);
  stream << value;
  return stream.str();
}

void appendIndent(std::string& out, int indent, int depth) {
  if (indent <= 0) return;
  out.append(static_cast<std::size_t>(indent) * depth, ' ');
}

}  // namespace

std::string JsonValue::dump(int indent) const {
  std::string out;
  dumpInto(out, indent, 0);
  out.push_back('\n');
  return out;
}

void JsonValue::dumpInto(std::string& out, int indent, int depth) const {
  switch (type_) {
    case Type::Null:
      out += "null";
      break;
    case Type::Bool:
      out += bool_value_ ? "true" : "false";
      break;
    case Type::Number:
      out += formatNumber(number_value_);
      break;
    case Type::String:
      out += jsonEscape(string_value_);
      break;
    case Type::Array: {
      if (array_value_.empty()) {
        out += "[]";
        break;
      }
      out.push_back('[');
      for (std::size_t i = 0; i < array_value_.size(); ++i) {
        if (i != 0) out.push_back(',');
        if (indent > 0) out.push_back('\n');
        appendIndent(out, indent, depth + 1);
        array_value_[i].dumpInto(out, indent, depth + 1);
      }
      if (indent > 0) {
        out.push_back('\n');
        appendIndent(out, indent, depth);
      }
      out.push_back(']');
      break;
    }
    case Type::Object: {
      if (object_order_.empty()) {
        out += "{}";
        break;
      }
      out.push_back('{');
      for (std::size_t i = 0; i < object_order_.size(); ++i) {
        if (i != 0) out.push_back(',');
        if (indent > 0) out.push_back('\n');
        appendIndent(out, indent, depth + 1);
        const std::string& key = object_order_[i];
        out += jsonEscape(key);
        out += indent > 0 ? ": " : ":";
        array_value_[i].dumpInto(out, indent, depth + 1);
      }
      if (indent > 0) {
        out.push_back('\n');
        appendIndent(out, indent, depth);
      }
      out.push_back('}');
      break;
    }
  }
}

namespace {

class Parser {
 public:
  Parser(const std::string& text, JsonParseError* error)
      : text_(text), error_(error) {}

  JsonValue parse() {
    skipWhitespace();
    JsonValue root = parseValue();
    if (!failed_) {
      skipWhitespace();
      if (position_ != text_.size()) fail("trailing characters after value");
    }
    return root;
  }

 private:
  const std::string& text_;
  JsonParseError* error_;
  std::size_t position_ = 0;
  bool failed_ = false;

  int line() const {
    int result = 1;
    for (std::size_t i = 0; i < position_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') ++result;
    }
    return result;
  }

  int column() const {
    int result = 1;
    for (std::size_t i = 0; i < position_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') result = 1;
      else ++result;
    }
    return result;
  }

  void fail(const std::string& message) {
    if (!failed_ && error_ != nullptr) {
      error_->message = message;
      error_->line = line();
      error_->column = column();
    }
    failed_ = true;
  }

  char peek() const {
    return position_ < text_.size() ? text_[position_] : '\0';
  }

  void skipWhitespace() {
    while (position_ < text_.size()) {
      const char ch = text_[position_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++position_;
      } else {
        break;
      }
    }
  }

  JsonValue parseValue() {
    if (failed_) return JsonValue::makeNull();
    skipWhitespace();
    const char ch = peek();
    if (ch == '{') return parseObject();
    if (ch == '[') return parseArray();
    if (ch == '"') {
      std::string value = parseString();
      return failed_ ? JsonValue::makeNull() : JsonValue::makeString(value);
    }
    if (ch == 't' || ch == 'f') return parseBool();
    if (ch == 'n') return parseNull();
    if (ch == '-' || (ch >= '0' && ch <= '9')) return parseNumber();
    fail("unexpected character");
    return JsonValue::makeNull();
  }

  JsonValue parseArray() {
    JsonValue array = JsonValue::makeArray();
    ++position_;
    skipWhitespace();
    if (peek() == ']') {
      ++position_;
      return array;
    }
    while (!failed_) {
      JsonValue value = parseValue();
      if (failed_) break;
      array.push(std::move(value));
      skipWhitespace();
      const char next = peek();
      if (next == ',') {
        ++position_;
        continue;
      }
      if (next == ']') {
        ++position_;
        return array;
      }
      fail("expected ',' or ']' in array");
      break;
    }
    return JsonValue::makeNull();
  }

  std::string parseString() {
    std::string result;
    ++position_;  // opening quote
    while (!failed_) {
      if (position_ >= text_.size()) {
        fail("unterminated string");
        break;
      }
      const char ch = text_[position_++];
      if (ch == '"') return result;
      if (ch == '\\') {
        if (position_ >= text_.size()) {
          fail("unterminated escape");
          break;
        }
        const char escape = text_[position_++];
        switch (escape) {
          case '"': result.push_back('"'); break;
          case '\\': result.push_back('\\'); break;
          case '/': result.push_back('/'); break;
          case 'b': result.push_back('\b'); break;
          case 'f': result.push_back('\f'); break;
          case 'n': result.push_back('\n'); break;
          case 'r': result.push_back('\r'); break;
          case 't': result.push_back('\t'); break;
          case 'u': {
            unsigned code = 0;
            for (int i = 0; i < 4; ++i) {
              if (position_ >= text_.size()) {
                fail("bad unicode escape");
                break;
              }
              const char hex = text_[position_++];
              code <<= 4;
              if (hex >= '0' && hex <= '9') code |= hex - '0';
              else if (hex >= 'a' && hex <= 'f') code |= hex - 'a' + 10;
              else if (hex >= 'A' && hex <= 'F') code |= hex - 'A' + 10;
              else { fail("bad unicode escape"); break; }
            }
            if (code < 0x80) {
              result.push_back(static_cast<char>(code));
            } else if (code < 0x800) {
              result.push_back(static_cast<char>(0xc0 | (code >> 6)));
              result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            } else {
              result.push_back(static_cast<char>(0xe0 | (code >> 12)));
              result.push_back(
                  static_cast<char>(0x80 | ((code >> 6) & 0x3f)));
              result.push_back(static_cast<char>(0x80 | (code & 0x3f)));
            }
            break;
          }
          default:
            fail("unknown escape character");
        }
      } else {
        result.push_back(ch);
      }
    }
    return result;
  }

  JsonValue parseBool() {
    if (text_.compare(position_, 4, "true") == 0) {
      position_ += 4;
      return JsonValue::makeBool(true);
    }
    if (text_.compare(position_, 5, "false") == 0) {
      position_ += 5;
      return JsonValue::makeBool(false);
    }
    fail("invalid literal");
    return JsonValue::makeNull();
  }

  JsonValue parseNull() {
    if (text_.compare(position_, 4, "null") == 0) {
      position_ += 4;
      return JsonValue::makeNull();
    }
    fail("invalid literal");
    return JsonValue::makeNull();
  }

  JsonValue parseNumber() {
    const std::size_t start = position_;
    if (peek() == '-') ++position_;
    while (peek() >= '0' && peek() <= '9') ++position_;
    if (peek() == '.') {
      ++position_;
      while (peek() >= '0' && peek() <= '9') ++position_;
    }
    if (peek() == 'e' || peek() == 'E') {
      ++position_;
      if (peek() == '+' || peek() == '-') ++position_;
      while (peek() >= '0' && peek() <= '9') ++position_;
    }
    try {
      return JsonValue::makeNumber(std::stod(text_.substr(start,
                                                          position_ - start)));
    } catch (...) {
      fail("invalid number");
      return JsonValue::makeNull();
    }
  }
  JsonValue parseObject() {
    JsonValue object = JsonValue::makeObject();
    ++position_;
    skipWhitespace();
    if (peek() == '}') {
      ++position_;
      return object;
    }
    while (!failed_) {
      skipWhitespace();
      if (peek() != '"') {
        fail("expected string key in object");
        break;
      }
      std::string key = parseString();
      if (failed_) break;
      if (object.has(key)) {
        fail("duplicate object key: " + key);
        break;
      }
      skipWhitespace();
      if (peek() != ':') {
        fail("expected ':' after object key");
        break;
      }
      ++position_;
      JsonValue value = parseValue();
      if (failed_) break;
      object.set(key, std::move(value));
      skipWhitespace();
      const char next = peek();
      if (next == ',') {
        ++position_;
        continue;
      }
      if (next == '}') {
        ++position_;
        return object;
      }
      fail("expected ',' or '}' in object");
      break;
    }
    return JsonValue::makeNull();
  }
};

}  // namespace

JsonValue parseJson(const std::string& text, JsonParseError* error) {
  Parser parser(text, error);
  return parser.parse();
}

}  // namespace camtrak
