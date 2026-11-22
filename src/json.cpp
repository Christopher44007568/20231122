#include "json.hpp"

#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace kfc {

namespace {

class Parser {
public:
  Parser(std::string_view text, std::string* error)
      : text_(text), error_(error) {}

  Json parse() {
    skipSpace();
    Json value = parseValue();
    skipSpace();
    if (pos_ != text_.size()) {
      fail("unexpected trailing content");
    }
    return value;
  }

private:
  std::string_view text_;
  std::string* error_;
  std::size_t pos_ = 0;

  [[noreturn]] void fail(const std::string& message) {
    std::size_t line = 1;
    std::size_t column = 1;
    for (std::size_t i = 0; i < pos_ && i < text_.size(); ++i) {
      if (text_[i] == '\n') {
        ++line;
        column = 1;
      } else {
        ++column;
      }
    }
    std::ostringstream out;
    out << message << " at line " << line << ", column " << column;
    throw std::runtime_error(out.str());
  }

  void skipSpace() {
    while (pos_ < text_.size()) {
      const char c = text_[pos_];
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool consume(char c) {
    if (pos_ < text_.size() && text_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }

  void expect(char c) {
    if (!consume(c)) {
      fail(std::string("expected '") + c + "'");
    }
  }

  Json parseValue() {
    if (pos_ >= text_.size()) fail("unexpected end of input");
    switch (text_[pos_]) {
      case '{':
        return parseObject();
      case '[':
        return parseArray();
      case '"':
        return Json(parseString());
      case 't':
        expectLiteral("true");
        return Json(true);
      case 'f':
        expectLiteral("false");
        return Json(false);
      case 'n':
        expectLiteral("null");
        return Json(nullptr);
      default:
        if (text_[pos_] == '-' || (text_[pos_] >= '0' && text_[pos_] <= '9')) {
          return parseNumber();
        }
        fail("unexpected character");
    }
  }

  void expectLiteral(std::string_view literal) {
    if (text_.substr(pos_, literal.size()) != literal) {
      fail("invalid literal");
    }
    pos_ += literal.size();
  }

  Json parseObject() {
    expect('{');
    Json value = Json::object();
    skipSpace();
    if (consume('}')) return value;
    while (true) {
      skipSpace();
      if (pos_ >= text_.size() || text_[pos_] != '"') fail("expected object key");
      std::string key = parseString();
      skipSpace();
      expect(':');
      skipSpace();
      value[key] = parseValue();
      skipSpace();
      if (consume('}')) break;
      expect(',');
    }
    return value;
  }

  Json parseArray() {
    expect('[');
    Json value = Json::array();
    skipSpace();
    if (consume(']')) return value;
    while (true) {
      skipSpace();
      value.push(parseValue());
      skipSpace();
      if (consume(']')) break;
      expect(',');
    }
    return value;
  }

  static int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }

  std::uint32_t parseHex4() {
    if (pos_ + 4 > text_.size()) fail("truncated unicode escape");
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const int digit = hexDigit(text_[pos_++]);
      if (digit < 0) fail("invalid unicode escape");
      value = (value << 4U) | static_cast<std::uint32_t>(digit);
    }
    return value;
  }

  static void appendUtf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7FU) {
      out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FFU) {
      out.push_back(static_cast<char>(0xC0U | (cp >> 6U)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else if (cp <= 0xFFFFU) {
      out.push_back(static_cast<char>(0xE0U | (cp >> 12U)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    } else {
      out.push_back(static_cast<char>(0xF0U | (cp >> 18U)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 12U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | ((cp >> 6U) & 0x3FU)));
      out.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
    }
  }

  std::string parseString() {
    expect('"');
    std::string out;
    while (pos_ < text_.size()) {
      const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
      if (c == '"') return out;
      if (c < 0x20U) fail("control character in string");
      if (c != '\\') {
        out.push_back(static_cast<char>(c));
        continue;
      }
      if (pos_ >= text_.size()) fail("truncated escape");
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
          std::uint32_t cp = parseHex4();
          if (cp >= 0xD800U && cp <= 0xDBFFU) {
            if (pos_ + 2 > text_.size() || text_[pos_] != '\\' ||
                text_[pos_ + 1] != 'u') {
              fail("missing low surrogate");
            }
            pos_ += 2;
            const std::uint32_t low = parseHex4();
            if (low < 0xDC00U || low > 0xDFFFU) fail("invalid low surrogate");
            cp = 0x10000U + ((cp - 0xD800U) << 10U) + (low - 0xDC00U);
          }
          appendUtf8(out, cp);
          break;
        }
        default:
          fail("invalid escape");
      }
    }
    fail("unterminated string");
  }

  Json parseNumber() {
    const std::size_t begin = pos_;
    if (consume('-')) {}
    if (pos_ >= text_.size()) fail("truncated number");
    if (text_[pos_] == '0') {
      ++pos_;
    } else {
      if (text_[pos_] < '1' || text_[pos_] > '9') fail("invalid number");
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    bool floating = false;
    if (consume('.')) {
      floating = true;
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("invalid fraction");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
      floating = true;
      ++pos_;
      if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-')) {
        ++pos_;
      }
      if (pos_ >= text_.size() || text_[pos_] < '0' || text_[pos_] > '9') {
        fail("invalid exponent");
      }
      while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9') {
        ++pos_;
      }
    }
    const std::string token(text_.substr(begin, pos_ - begin));
    errno = 0;
    if (!floating) {
      char* end = nullptr;
      const long long signedValue = std::strtoll(token.c_str(), &end, 10);
      if (errno == 0 && end != nullptr && *end == '\0' &&
          signedValue >= std::numeric_limits<std::int64_t>::min() &&
          signedValue <= std::numeric_limits<std::int64_t>::max()) {
        if (signedValue >= 0) {
          return Json(static_cast<std::uint64_t>(signedValue));
        }
        return Json(static_cast<std::int64_t>(signedValue));
      }
    }
    char* end = nullptr;
    const double value = std::strtod(token.c_str(), &end);
    if (end == nullptr || *end != '\0' || !std::isfinite(value)) {
      fail("invalid number");
    }
    return Json(value);
  }
};

}  // namespace

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

Json Json::parse(std::string_view text, std::string* error) {
  try {
    return Parser(text, error).parse();
  } catch (const std::exception& ex) {
    if (error != nullptr) *error = ex.what();
    return Json(nullptr);
  }
}

bool Json::asBool(bool fallback) const {
  return type_ == Type::Bool ? bool_ : fallback;
}

std::int64_t Json::asInt(std::int64_t fallback) const {
  switch (type_) {
    case Type::Int: return int_;
    case Type::UInt:
      return uint_ <= static_cast<std::uint64_t>(
                         std::numeric_limits<std::int64_t>::max())
                 ? static_cast<std::int64_t>(uint_)
                 : fallback;
    case Type::Double: return static_cast<std::int64_t>(double_);
    default: return fallback;
  }
}

double Json::asDouble(double fallback) const {
  switch (type_) {
    case Type::Int: return static_cast<double>(int_);
    case Type::UInt: return static_cast<double>(uint_);
    case Type::Double: return double_;
    default: return fallback;
  }
}

const std::string& Json::asString() const {
  static const std::string empty;
  return type_ == Type::String ? string_ : empty;
}

const Json::Array& Json::asArray() const {
  static const Array empty;
  return type_ == Type::Array ? array_ : empty;
}

Json::Array& Json::asArray() {
  if (type_ != Type::Array) throw std::runtime_error("JSON value is not an array");
  return array_;
}

const Json::Object& Json::asObject() const {
  static const Object empty;
  return type_ == Type::Object ? object_ : empty;
}

Json::Object& Json::asObject() {
  if (type_ != Type::Object) throw std::runtime_error("JSON value is not an object");
  return object_;
}

bool Json::contains(std::string_view key) const {
  return get(key) != nullptr;
}

const Json* Json::get(std::string_view key) const {
  if (type_ != Type::Object) return nullptr;
  for (const auto& item : object_) {
    if (item.first == key) return &item.second;
  }
  return nullptr;
}

Json* Json::get(std::string_view key) {
  if (type_ != Type::Object) return nullptr;
  for (auto& item : object_) {
    if (item.first == key) return &item.second;
  }
  return nullptr;
}

const Json& Json::at(std::string_view key) const {
  const Json* value = get(key);
  if (value == nullptr) throw std::runtime_error("missing JSON field: " + std::string(key));
  return *value;
}

Json& Json::operator[](std::string key) {
  if (type_ != Type::Object) {
    type_ = Type::Object;
    object_.clear();
  }
  for (auto& item : object_) {
    if (item.first == key) return item.second;
  }
  object_.emplace_back(std::move(key), Json(nullptr));
  return object_.back().second;
}

Json& Json::push(Json value) {
  if (type_ != Type::Array) {
    type_ = Type::Array;
    array_.clear();
  }
  array_.push_back(std::move(value));
  return array_.back();
}

std::size_t Json::size() const {
  if (type_ == Type::Array) return array_.size();
  if (type_ == Type::Object) return object_.size();
  if (type_ == Type::String) return string_.size();
  return 0;
}

const std::string& Json::keyAt(std::size_t index) const {
  if (type_ != Type::Object || index >= object_.size()) {
    throw std::runtime_error("JSON object index out of range");
  }
  return object_[index].first;
}

const Json& Json::valueAt(std::size_t index) const {
  if (type_ != Type::Object || index >= object_.size()) {
    throw std::runtime_error("JSON object index out of range");
  }
  return object_[index].second;
}

const std::string& Json::typeName() const {
  static const std::string names[] = {"null",   "boolean", "integer", "unsigned",
                                      "number", "string",  "array",   "object"};
  return names[static_cast<std::size_t>(type_)];
}

void Json::appendEscaped(std::string& out, std::string_view value) {
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
        if (c < 0x20U) {
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
}

std::string Json::dump(int indent) const {
  std::string out;
  dumpTo(out, indent, 0);
  return out;
}

void Json::dumpTo(std::string& out, int indent, int depth) const {
  switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Int: out += std::to_string(int_); break;
    case Type::UInt: out += std::to_string(uint_); break;
    case Type::Double: {
      if (!std::isfinite(double_)) {
        out += "null";
        break;
      }
      std::ostringstream stream;
      stream << std::setprecision(15) << double_;
      std::string number = stream.str();
      if (number.find('.') != std::string::npos &&
          number.find('e') == std::string::npos &&
          number.find('E') == std::string::npos) {
        while (!number.empty() && number.back() == '0') number.pop_back();
        if (!number.empty() && number.back() == '.') number.pop_back();
      }
      out += number.empty() ? "0" : number;
      break;
    }
    case Type::String: appendEscaped(out, string_); break;
    case Type::Array: {
      out.push_back('[');
      if (array_.empty()) {
        out.push_back(']');
        break;
      }
      for (std::size_t i = 0; i < array_.size(); ++i) {
        if (i != 0) out.push_back(',');
        if (indent > 0) {
          out.push_back('\n');
          out.append(static_cast<std::size_t>(indent * (depth + 1)), ' ');
        }
        array_[i].dumpTo(out, indent, depth + 1);
      }
      if (indent > 0) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * depth), ' ');
      }
      out.push_back(']');
      break;
    }
    case Type::Object: {
      out.push_back('{');
      if (object_.empty()) {
        out.push_back('}');
        break;
      }
      for (std::size_t i = 0; i < object_.size(); ++i) {
        if (i != 0) out.push_back(',');
        if (indent > 0) {
          out.push_back('\n');
          out.append(static_cast<std::size_t>(indent * (depth + 1)), ' ');
        }
        appendEscaped(out, object_[i].first);
        out.push_back(':');
        if (indent > 0) out.push_back(' ');
        object_[i].second.dumpTo(out, indent, depth + 1);
      }
      if (indent > 0) {
        out.push_back('\n');
        out.append(static_cast<std::size_t>(indent * depth), ' ');
      }
      out.push_back('}');
      break;
    }
  }
}

std::string readTextFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open for reading: " + path);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

void writeTextFile(const std::string& path, std::string_view text) {
  const std::filesystem::path target(path);
  if (target.has_parent_path()) {
    std::filesystem::create_directories(target.parent_path());
  }
  const std::string temporary = path + ".tmp";
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open for writing: " + temporary);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed while writing: " + temporary);
  }
  std::filesystem::rename(temporary, target);
}

void ensureDirectory(const std::string& path) {
  if (!path.empty()) std::filesystem::create_directories(path);
}

std::string pathJoin(const std::string& a, const std::string& b) {
  return (std::filesystem::path(a) / b).string();
}

bool fileExists(const std::string& path) {
  std::error_code error;
  return std::filesystem::is_regular_file(path, error);
}

}  // namespace kfc
