#include "gbi/json.hpp"

#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace gbi {

Json::Json() : value_(nullptr) {}
Json::Json(std::nullptr_t) : value_(nullptr) {}
Json::Json(bool value) : value_(value) {}
Json::Json(double value) : value_(value) {}
Json::Json(int value) : value_(static_cast<double>(value)) {}
Json::Json(std::uint32_t value) : value_(static_cast<double>(value)) {}
Json::Json(std::uint64_t value) : value_(static_cast<double>(value)) {}
Json::Json(std::int64_t value) : value_(static_cast<double>(value)) {}
Json::Json(const char* value) : value_(std::string(value)) {}
Json::Json(std::string value) : value_(std::move(value)) {}
Json::Json(Array value) : value_(std::move(value)) {}
Json::Json(Object value) : value_(std::move(value)) {}

Json Json::array() {
  return Json(Array{});
}

Json Json::object() {
  return Json(Object{});
}

bool Json::isNull() const {
  return std::holds_alternative<std::nullptr_t>(value_);
}

bool Json::isBool() const {
  return std::holds_alternative<bool>(value_);
}

bool Json::isNumber() const {
  return std::holds_alternative<double>(value_);
}

bool Json::isString() const {
  return std::holds_alternative<std::string>(value_);
}

bool Json::isArray() const {
  return std::holds_alternative<Array>(value_);
}

bool Json::isObject() const {
  return std::holds_alternative<Object>(value_);
}

bool Json::asBool(bool fallback) const {
  if (const auto* value = std::get_if<bool>(&value_)) {
    return *value;
  }
  return fallback;
}

double Json::asDouble(double fallback) const {
  if (const auto* value = std::get_if<double>(&value_)) {
    return *value;
  }
  return fallback;
}

std::int64_t Json::asInt(std::int64_t fallback) const {
  if (const auto* value = std::get_if<double>(&value_)) {
    return static_cast<std::int64_t>(*value);
  }
  return fallback;
}

std::uint64_t Json::asUint(std::uint64_t fallback) const {
  if (const auto* value = std::get_if<double>(&value_)) {
    if (*value < 0.0) {
      return fallback;
    }
    return static_cast<std::uint64_t>(*value);
  }
  return fallback;
}

const std::string& Json::asString() const {
  if (const auto* value = std::get_if<std::string>(&value_)) {
    return *value;
  }
  throw std::runtime_error("JSON value is not a string");
}

const Json::Array& Json::asArray() const {
  if (const auto* value = std::get_if<Array>(&value_)) {
    return *value;
  }
  throw std::runtime_error("JSON value is not an array");
}

const Json::Object& Json::asObject() const {
  if (const auto* value = std::get_if<Object>(&value_)) {
    return *value;
  }
  throw std::runtime_error("JSON value is not an object");
}

Json::Array& Json::asArray() {
  if (auto* value = std::get_if<Array>(&value_)) {
    return *value;
  }
  throw std::runtime_error("JSON value is not an array");
}

Json::Object& Json::asObject() {
  if (auto* value = std::get_if<Object>(&value_)) {
    return *value;
  }
  throw std::runtime_error("JSON value is not an object");
}

bool Json::contains(const std::string& key) const {
  if (!isObject()) {
    return false;
  }
  return asObject().find(key) != asObject().end();
}

const Json& Json::at(const std::string& key) const {
  const Json::Object& object = asObject();
  const auto iterator = object.find(key);
  if (iterator == object.end()) {
    throw std::runtime_error("missing JSON key: " + key);
  }
  return iterator->second;
}

Json& Json::operator[](const std::string& key) {
  if (isNull()) {
    value_ = Object{};
  }
  return asObject()[key];
}

const Json& Json::operator[](const std::string& key) const {
  return at(key);
}

Json& Json::operator[](std::size_t index) {
  return asArray().at(index);
}

const Json& Json::operator[](std::size_t index) const {
  return asArray().at(index);
}

void Json::push(Json value) {
  if (isNull()) {
    value_ = Array{};
  }
  asArray().push_back(std::move(value));
}

std::string Json::value(const std::string& key, const std::string& fallback)
    const {
  if (!contains(key) || !at(key).isString()) {
    return fallback;
  }
  return at(key).asString();
}

std::uint64_t Json::u64(const std::string& key, std::uint64_t fallback) const {
  if (!contains(key) || !at(key).isNumber()) {
    return fallback;
  }
  return at(key).asUint(fallback);
}

std::int64_t Json::i64(const std::string& key, std::int64_t fallback) const {
  if (!contains(key) || !at(key).isNumber()) {
    return fallback;
  }
  return at(key).asInt(fallback);
}

double Json::number(const std::string& key, double fallback) const {
  if (!contains(key) || !at(key).isNumber()) {
    return fallback;
  }
  return at(key).asDouble(fallback);
}

bool Json::boolean(const std::string& key, bool fallback) const {
  if (!contains(key) || !at(key).isBool()) {
    return fallback;
  }
  return at(key).asBool(fallback);
}

namespace {

void dumpString(const std::string& value, std::string& output) {
  static constexpr char kHex[] = "0123456789abcdef";
  output.push_back('"');
  for (const unsigned char character : value) {
    switch (character) {
      case '"':
        output += "\\\"";
        break;
      case '\\':
        output += "\\\\";
        break;
      case '\b':
        output += "\\b";
        break;
      case '\f':
        output += "\\f";
        break;
      case '\n':
        output += "\\n";
        break;
      case '\r':
        output += "\\r";
        break;
      case '\t':
        output += "\\t";
        break;
      default:
        if (character < 0x20U) {
          output += "\\u00";
          output.push_back(kHex[character >> 4U]);
          output.push_back(kHex[character & 0x0fU]);
        } else {
          output.push_back(static_cast<char>(character));
        }
        break;
    }
  }
  output.push_back('"');
}

void dumpValue(const Json& json, std::string& output, int indent, int level) {
  const auto newline = [&](int next_level) {
    if (indent <= 0) {
      return;
    }
    output.push_back('\n');
    output.append(static_cast<std::size_t>(next_level * indent), ' ');
  };

  if (json.isNull()) {
    output += "null";
  } else if (json.isBool()) {
    output += json.asBool() ? "true" : "false";
  } else if (json.isNumber()) {
    std::ostringstream stream;
    const double value = json.asDouble();
    if (!std::isfinite(value)) {
      stream << "null";
    } else if (std::floor(value) == value &&
               std::abs(value) <=
                   static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
      stream << static_cast<std::int64_t>(value);
    } else {
      stream << std::setprecision(17) << value;
    }
    output += stream.str();
  } else if (json.isString()) {
    dumpString(json.asString(), output);
  } else if (json.isArray()) {
    const Json::Array& array = json.asArray();
    output.push_back('[');
    for (std::size_t i = 0; i < array.size(); ++i) {
      if (i != 0) {
        output.push_back(',');
      }
      if (indent > 0) {
        newline(level + 1);
      }
      dumpValue(array[i], output, indent, level + 1);
    }
    if (!array.empty()) {
      newline(level);
    }
    output.push_back(']');
  } else {
    const Json::Object& object = json.asObject();
    output.push_back('{');
    std::size_t index = 0;
    for (const auto& entry : object) {
      if (index++ != 0) {
        output.push_back(',');
      }
      if (indent > 0) {
        newline(level + 1);
      }
      dumpString(entry.first, output);
      output += indent > 0 ? ": " : ":";
      dumpValue(entry.second, output, indent, level + 1);
    }
    if (!object.empty()) {
      newline(level);
    }
    output.push_back('}');
  }
}

class Parser {
 public:
  explicit Parser(const std::string& text) : text_(text) {}

  Json parse() {
    skipWhitespace();
    Json value = parseValue();
    skipWhitespace();
    if (position_ != text_.size()) {
      fail("trailing data");
    }
    return value;
  }

 private:
  [[noreturn]] void fail(const std::string& message) const {
    std::ostringstream stream;
    stream << "JSON parse error at byte " << position_ << ": " << message;
    throw std::runtime_error(stream.str());
  }

  void skipWhitespace() {
    while (position_ < text_.size()) {
      const char character = text_[position_];
      if (character != ' ' && character != '\t' && character != '\r' &&
          character != '\n') {
        break;
      }
      ++position_;
    }
  }

  bool consume(char expected) {
    if (position_ < text_.size() && text_[position_] == expected) {
      ++position_;
      return true;
    }
    return false;
  }

  Json parseValue() {
    if (position_ >= text_.size()) {
      fail("unexpected end of input");
    }
    switch (text_[position_]) {
      case '{':
        return parseObject();
      case '[':
        return parseArray();
      case '"':
        return Json(parseString());
      case 't':
        expectWord("true");
        return Json(true);
      case 'f':
        expectWord("false");
        return Json(false);
      case 'n':
        expectWord("null");
        return Json(nullptr);
      default:
        return parseNumber();
    }
  }

  void expectWord(const char* word) {
    const std::string expected(word);
    if (text_.compare(position_, expected.size(), expected) != 0) {
      fail("expected " + expected);
    }
    position_ += expected.size();
  }

  Json parseObject() {
    consume('{');
    Json result = Json::object();
    skipWhitespace();
    if (consume('}')) {
      return result;
    }
    while (true) {
      skipWhitespace();
      if (position_ >= text_.size() || text_[position_] != '"') {
        fail("expected object key");
      }
      std::string key = parseString();
      skipWhitespace();
      if (!consume(':')) {
        fail("expected ':'");
      }
      skipWhitespace();
      result[key] = parseValue();
      skipWhitespace();
      if (consume('}')) {
        break;
      }
      if (!consume(',')) {
        fail("expected ',' or '}'");
      }
    }
    return result;
  }

  Json parseArray() {
    consume('[');
    Json result = Json::array();
    skipWhitespace();
    if (consume(']')) {
      return result;
    }
    while (true) {
      skipWhitespace();
      result.push(parseValue());
      skipWhitespace();
      if (consume(']')) {
        break;
      }
      if (!consume(',')) {
        fail("expected ',' or ']'");
      }
    }
    return result;
  }

  static void appendUtf8(std::string& output, std::uint32_t codepoint) {
    if (codepoint <= 0x7fU) {
      output.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7ffU) {
      output.push_back(static_cast<char>(0xc0U | (codepoint >> 6U)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else if (codepoint <= 0xffffU) {
      output.push_back(static_cast<char>(0xe0U | (codepoint >> 12U)));
      output.push_back(
          static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    } else {
      output.push_back(static_cast<char>(0xf0U | (codepoint >> 18U)));
      output.push_back(
          static_cast<char>(0x80U | ((codepoint >> 12U) & 0x3fU)));
      output.push_back(
          static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3fU)));
      output.push_back(static_cast<char>(0x80U | (codepoint & 0x3fU)));
    }
  }

  std::uint32_t parseHex4() {
    if (position_ + 4 > text_.size()) {
      fail("truncated unicode escape");
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char character = text_[position_++];
      value <<= 4U;
      if (character >= '0' && character <= '9') {
        value |= static_cast<std::uint32_t>(character - '0');
      } else if (character >= 'a' && character <= 'f') {
        value |= static_cast<std::uint32_t>(character - 'a' + 10);
      } else if (character >= 'A' && character <= 'F') {
        value |= static_cast<std::uint32_t>(character - 'A' + 10);
      } else {
        fail("invalid unicode escape");
      }
    }
    return value;
  }

  std::string parseString() {
    if (!consume('"')) {
      fail("expected string");
    }
    std::string output;
    while (position_ < text_.size()) {
      const char character = text_[position_++];
      if (character == '"') {
        return output;
      }
      if (character != '\\') {
        output.push_back(character);
        continue;
      }
      if (position_ >= text_.size()) {
        fail("truncated escape");
      }
      const char escaped = text_[position_++];
      switch (escaped) {
        case '"':
        case '\\':
        case '/':
          output.push_back(escaped);
          break;
        case 'b':
          output.push_back('\b');
          break;
        case 'f':
          output.push_back('\f');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case 'r':
          output.push_back('\r');
          break;
        case 't':
          output.push_back('\t');
          break;
        case 'u': {
          std::uint32_t codepoint = parseHex4();
          if (codepoint >= 0xd800U && codepoint <= 0xdbffU) {
            if (position_ + 2 > text_.size() || text_[position_] != '\\' ||
                text_[position_ + 1] != 'u') {
              fail("unpaired high surrogate");
            }
            position_ += 2;
            const std::uint32_t low = parseHex4();
            if (low < 0xdc00U || low > 0xdfffU) {
              fail("invalid low surrogate");
            }
            codepoint =
                0x10000U + ((codepoint - 0xd800U) << 10U) + (low - 0xdc00U);
          }
          appendUtf8(output, codepoint);
          break;
        }
        default:
          fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  Json parseNumber() {
    const std::size_t start = position_;
    if (consume('-')) {
      if (position_ >= text_.size()) {
        fail("truncated number");
      }
    }
    if (consume('0')) {
      if (position_ < text_.size() && text_[position_] >= '0' &&
          text_[position_] <= '9') {
        fail("leading zero in number");
      }
    } else {
      if (position_ >= text_.size() || text_[position_] < '1' ||
          text_[position_] > '9') {
        fail("invalid number");
      }
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
    }
    if (consume('.')) {
      if (position_ >= text_.size() || text_[position_] < '0' ||
          text_[position_] > '9') {
        fail("invalid fraction");
      }
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
    }
    if (position_ < text_.size() &&
        (text_[position_] == 'e' || text_[position_] == 'E')) {
      ++position_;
      if (position_ < text_.size() &&
          (text_[position_] == '+' || text_[position_] == '-')) {
        ++position_;
      }
      if (position_ >= text_.size() || text_[position_] < '0' ||
          text_[position_] > '9') {
        fail("invalid exponent");
      }
      while (position_ < text_.size() && text_[position_] >= '0' &&
             text_[position_] <= '9') {
        ++position_;
      }
    }
    try {
      return Json(std::stod(text_.substr(start, position_ - start)));
    } catch (...) {
      fail("number out of range");
    }
  }

  const std::string& text_;
  std::size_t position_ = 0;
};

}  // namespace

Json Json::parse(const std::string& text) {
  return Parser(text).parse();
}

std::string Json::dump(int indent) const {
  std::string output;
  dumpValue(*this, output, indent, 0);
  if (indent > 0) {
    output.push_back('\n');
  }
  return output;
}

}  // namespace gbi
