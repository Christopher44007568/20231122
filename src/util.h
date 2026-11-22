#pragma once

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace tar {

// Error thrown for any recoverable, per-object failure. The review pipeline
// catches these, records them as evidence, and keeps processing.
struct PipelineError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

uint32_t Crc32(const void* data, size_t size);
uint32_t Adler32(const void* data, size_t size);

std::vector<uint8_t> ReadFile(const std::string& path);
void WriteFile(const std::string& path, const void* data, size_t size);
bool FileExists(const std::string& path);
void MakeDirs(const std::string& path);
void RemoveTree(const std::string& path);
std::string JoinPath(const std::string& a, const std::string& b);
std::string DirName(const std::string& path);

// Minimal logger that also mirrors lines into an in-memory evidence log.
struct Log {
  std::vector<std::string> lines;
  void Info(const std::string& msg);
  void Warn(const std::string& msg);
  void Error(const std::string& msg);
 private:
  void Emit(const char* level, const std::string& msg);
};

}  // namespace tar
