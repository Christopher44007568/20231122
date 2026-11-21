#include "mi/json.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace mi {

std::string json_escape(const std::string& value) {
  std::ostringstream out;
  for (unsigned char ch : value) {
    switch (ch) {
      case '"': out << "\\\""; break;
      case '\\': out << "\\\\"; break;
      case '\b': out << "\\b"; break;
      case '\f': out << "\\f"; break;
      case '\n': out << "\\n"; break;
      case '\r': out << "\\r"; break;
      case '\t': out << "\\t"; break;
      default:
        if (ch < 0x20) {
          out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  return out.str();
}

std::string json_string(const std::string& value) {
  return "\"" + json_escape(value) + "\"";
}

std::string json_number(double value) {
  if (!std::isfinite(value)) {
    throw std::runtime_error("non-finite number in JSON");
  }
  std::ostringstream out;
  out << std::setprecision(9) << value;
  return out.str();
}

std::string json_bool(bool value) { return value ? "true" : "false"; }

namespace {

std::size_t value_start(const std::string& object, const std::string& key) {
  const std::string needle = "\"" + key + "\":";
  const std::size_t pos = object.find(needle);
  if (pos == std::string::npos) {
    return std::string::npos;
  }
  return pos + needle.size();
}

}  // namespace

std::string get_json_string(const std::string& object, const std::string& key,
                            std::string fallback) {
  std::size_t pos = value_start(object, key);
  if (pos == std::string::npos || pos >= object.size() || object[pos] != '"') {
    return fallback;
  }
  ++pos;
  std::string out;
  while (pos < object.size()) {
    char ch = object[pos++];
    if (ch == '"') {
      return out;
    }
    if (ch == '\\' && pos < object.size()) {
      char escaped = object[pos++];
      switch (escaped) {
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        case 't': out.push_back('\t'); break;
        case 'b': out.push_back('\b'); break;
        case 'f': out.push_back('\f'); break;
        default: out.push_back(escaped); break;
      }
    } else {
      out.push_back(ch);
    }
  }
  return fallback;
}

std::uint64_t get_json_u64(const std::string& object, const std::string& key,
                           std::uint64_t fallback) {
  const std::size_t pos = value_start(object, key);
  if (pos == std::string::npos) {
    return fallback;
  }
  try {
    return std::stoull(object.substr(pos));
  } catch (const std::exception&) {
    return fallback;
  }
}

double get_json_double(const std::string& object, const std::string& key,
                       double fallback) {
  const std::size_t pos = value_start(object, key);
  if (pos == std::string::npos) {
    return fallback;
  }
  try {
    return std::stod(object.substr(pos));
  } catch (const std::exception&) {
    return fallback;
  }
}

}  // namespace mi
