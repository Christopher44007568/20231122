#include "rtsync/json_util.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace rtsync {

bool read_file(const std::string& path, std::string* out, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error) *error = "cannot open " + path;
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input.good() && !input.eof()) {
    if (error) *error = "read failed for " + path;
    return false;
  }
  *out = buffer.str();
  return true;
}

bool write_file(const std::string& path, const std::string& bytes,
                std::string* error) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    if (error) *error = "cannot write " + path;
    return false;
  }
  output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!output) {
    if (error) *error = "write failed for " + path;
    return false;
  }
  return true;
}

bool write_text(const std::string& path, const std::string& text,
                std::string* error) {
  return write_file(path, text, error);
}

bool load_json(const std::string& path, Json* out, std::string* error) {
  std::string bytes;
  if (!read_file(path, &bytes, error)) return false;
  try {
    *out = Json::parse(bytes, nullptr, true, true);
  } catch (const std::exception& e) {
    if (error) *error = "JSON parse error in " + path + ": " + e.what();
    return false;
  }
  return true;
}

bool save_json(const std::string& path, const Json& value, std::string* error) {
  return write_text(path, value.dump(2) + "\n", error);
}

std::string canonical_json(const Json& value) { return value.dump(); }

bool same_ordered_shape(const Json& left, const Json& right,
                        std::string* difference) {
  if (left.type() != right.type()) {
    if (difference) *difference = "JSON value type differs";
    return false;
  }
  if (left.is_object()) {
    if (left.size() != right.size()) {
      if (difference) *difference = "object field count differs";
      return false;
    }
    for (auto l = left.begin(), r = right.begin(); l != left.end();
         ++l, ++r) {
      if (l.key() != r.key()) {
        if (difference) {
          *difference = "field order differs: '" + l.key() + "' vs '" +
                        r.key() + "'";
        }
        return false;
      }
      if (!same_ordered_shape(l.value(), r.value(), difference)) return false;
    }
    return true;
  }
  if (left.is_array()) {
    if (left.size() != right.size()) {
      if (difference) *difference = "array length differs";
      return false;
    }
    for (std::size_t i = 0; i < left.size(); ++i) {
      if (!same_ordered_shape(left[i], right[i], difference)) return false;
    }
    return true;
  }
  if (left != right) {
    if (difference) *difference = "scalar value differs";
    return false;
  }
  return true;
}

std::string utc_now_iso8601() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t tt = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &tt);
#else
  gmtime_r(&tt, &tm);
#endif
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

}  // namespace rtsync

