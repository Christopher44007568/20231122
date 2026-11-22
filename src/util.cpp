#include "util.h"
#include <array>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace tcpv {

std::string readFile(const std::string& path) {
  std::ifstream f(path, std::ios::binary);
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f << data;
  return f.good();
}

bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& data) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  if (!f) return false;
  f.write(reinterpret_cast<const char*>(data.data()),
          static_cast<std::streamsize>(data.size()));
  return f.good();
}

bool fileExists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

void makeDirs(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
}

std::string jsonEscape(const std::string& s) {
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
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += c;
        }
    }
  }
  return out;
}

std::string nowIso8601() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec);
  return buf;
}

std::string formatDouble(double v, int precision) {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", precision, v);
  return buf;
}

} // namespace tcpv
