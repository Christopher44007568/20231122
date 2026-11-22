#include "util.h"

#include <cstdarg>
#include <filesystem>
#include <iostream>

namespace tar {

uint32_t Crc32(const void* ptr, size_t size) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  const auto* p = static_cast<const uint8_t*>(ptr);
  uint32_t crc = 0xFFFFFFFFu;
  for (size_t i = 0; i < size; ++i) crc = table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
  return crc ^ 0xFFFFFFFFu;
}

uint32_t Adler32(const void* ptr, size_t size) {
  const auto* p = static_cast<const uint8_t*>(ptr);
  uint32_t a = 1, b = 0;
  for (size_t i = 0; i < size; ++i) {
    a = (a + p[i]) % 65521u;
    b = (b + a) % 65521u;
  }
  return (b << 16) | a;
}

std::vector<uint8_t> ReadFile(const std::string& path) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) throw PipelineError("cannot open file: " + path);
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> out(static_cast<size_t>(n));
  if (n > 0 && std::fread(out.data(), 1, out.size(), f) != out.size()) {
    std::fclose(f);
    throw PipelineError("short read: " + path);
  }
  std::fclose(f);
  return out;
}

void WriteFile(const std::string& path, const void* data, size_t size) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) throw PipelineError("cannot write file: " + path);
  if (size > 0 && std::fwrite(data, 1, size, f) != size) {
    std::fclose(f);
    throw PipelineError("short write: " + path);
  }
  std::fclose(f);
}

bool FileExists(const std::string& path) {
  std::error_code ec;
  return std::filesystem::exists(path, ec);
}

void MakeDirs(const std::string& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) throw PipelineError("mkdir failed: " + path + ": " + ec.message());
}

void RemoveTree(const std::string& path) {
  std::error_code ec;
  std::filesystem::remove_all(path, ec);
}

std::string JoinPath(const std::string& a, const std::string& b) {
  if (a.empty()) return b;
  if (a.back() == '/') return a + b;
  return a + "/" + b;
}

std::string DirName(const std::string& path) {
  auto pos = path.find_last_of('/');
  return pos == std::string::npos ? "." : path.substr(0, pos);
}

void Log::Info(const std::string& msg) { Emit("INFO", msg); }
void Log::Warn(const std::string& msg) { Emit("WARN", msg); }
void Log::Error(const std::string& msg) { Emit("ERROR", msg); }

void Log::Emit(const char* level, const std::string& msg) {
  std::string line = std::string("[") + level + "] " + msg;
  lines.push_back(line);
  std::fprintf(stderr, "%s\n", line.c_str());
}

}  // namespace tar
