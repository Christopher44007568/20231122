#include "util.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace nmts {

std::string iso_utc_now() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto secs = time_point_cast<seconds>(now);
  auto ms = duration_cast<milliseconds>(now - secs).count();
  std::time_t t = system_clock::to_time_t(secs);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03lldZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec,
                static_cast<long long>(ms));
  return buf;
}

std::string to_hex(const std::uint8_t* data, std::size_t len) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.resize(len * 2);
  for (std::size_t i = 0; i < len; ++i) {
    s[i * 2] = kHex[data[i] >> 4];
    s[i * 2 + 1] = kHex[data[i] & 0xF];
  }
  return s;
}

std::string to_hex_lower(const std::vector<std::uint8_t>& data) {
  return to_hex(data.data(), data.size());
}

bool ensure_dir(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  return !ec;
}

bool file_exists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec) && !ec;
}

std::string join_path(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

bool write_atomic(const std::string& path,
                  const std::vector<std::uint8_t>& bytes) {
  std::string tmp = path + ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    if (!f) return false;
  }
  std::error_code ec;
  std::filesystem::rename(tmp, path, ec);
  return !ec;
}

bool write_text_atomic(const std::string& path, const std::string& text) {
  return write_atomic(
      path, std::vector<std::uint8_t>(text.begin(), text.end()));
}

bool read_file(const std::string& path, std::vector<std::uint8_t>& out) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return false;
  std::streamsize size = f.tellg();
  if (size < 0) return false;
  f.seekg(0);
  out.resize(static_cast<std::size_t>(size));
  return bool(f.read(reinterpret_cast<char*>(out.data()), size));
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x",
                        static_cast<unsigned char>(c));
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string format_double(double v, int digits) {
  std::ostringstream os;
  os << std::setprecision(digits) << v;
  return os.str();
}

}  // namespace nmts
