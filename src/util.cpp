#include "util.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sys/stat.h>
#include <dirent.h>

namespace pcd {

std::string utc_now_iso() {
  using namespace std::chrono;
  auto now = system_clock::now();
  auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
  std::time_t t = system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday, tm.tm_hour,
                tm.tm_min, tm.tm_sec, static_cast<int>(ms.count()));
  return buf;
}

uint64_t now_unix_ms() {
  using namespace std::chrono;
  return static_cast<uint64_t>(duration_cast<milliseconds>(
      system_clock::now().time_since_epoch()).count());
}

std::string utc_now_compact() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec);
  return buf;
}

bool file_exists(const std::string& path) {
  struct stat st{};
  return ::stat(path.c_str(), &st) == 0;
}

uint64_t file_size(const std::string& path) {
  struct stat st{};
  if (::stat(path.c_str(), &st) != 0)
    die("stat failed: " + path + ": " + std::strerror(errno));
  return static_cast<uint64_t>(st.st_size);
}

void make_dirs(const std::string& path) {
  std::string cur;
  for (char c : path) {
    cur.push_back(c);
    if (c == '/' && cur.size() > 1) {
      ::mkdir(cur.c_str(), 0755);
    }
  }
  ::mkdir(path.c_str(), 0755);
}

void copy_file(const std::string& src, const std::string& dst) {
  std::ifstream in(src, std::ios::binary);
  if (!in) die("cannot open source: " + src);
  std::ofstream out(dst, std::ios::binary | std::ios::trunc);
  if (!out) die("cannot open dest: " + dst);
  out << in.rdbuf();
  if (!out) die("copy failed: " + dst);
}

void make_readonly(const std::string& path) {
  if (::chmod(path.c_str(), 0444) != 0)
    die("chmod failed: " + path);
}

void copy_dir(const std::string& src, const std::string& dst) {
  make_dirs(dst);
  DIR* d = ::opendir(src.c_str());
  if (!d) die("cannot open dir: " + src);
  while (auto* e = ::readdir(d)) {
    std::string name = e->d_name;
    if (name == "." || name == "..") continue;
    std::string s = src + "/" + name;
    std::string t = dst + "/" + name;
    struct stat st{};
    if (::stat(s.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) copy_dir(s, t);
    else copy_file(s, t);
  }
  ::closedir(d);
}

std::vector<uint8_t> read_file(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) die("cannot open: " + path);
  in.seekg(0, std::ios::end);
  auto n = in.tellg();
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> buf(static_cast<size_t>(n));
  if (n > 0) in.read(reinterpret_cast<char*>(buf.data()), n);
  if (!in && n > 0) die("read failed: " + path);
  return buf;
}

void write_file(const std::string& path, const void* data, size_t size) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) die("cannot write: " + path);
  out.write(reinterpret_cast<const char*>(data),
            static_cast<std::streamsize>(size));
  if (!out) die("write failed: " + path);
}

void write_text(const std::string& path, const std::string& text) {
  write_file(path, text.data(), text.size());
}

void append_text(const std::string& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::app);
  if (!out) die("cannot append: " + path);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string hex_encode(const uint8_t* data, size_t n) {
  static const char* kHex = "0123456789abcdef";
  std::string s;
  s.reserve(n * 2);
  for (size_t i = 0; i < n; ++i) {
    s.push_back(kHex[data[i] >> 4]);
    s.push_back(kHex[data[i] & 15]);
  }
  return s;
}

std::string json_escape(const std::string& s) {
  std::string out;
  for (char c : s) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\t': out += "\\t"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

[[noreturn]] void die(const std::string& msg) {
  std::cerr << "pcdclip: error: " << msg << "\n";
  throw std::runtime_error(msg);
}

} // namespace pcd
