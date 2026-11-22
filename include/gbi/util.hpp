#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace gbi {

namespace fs = std::filesystem;
using Bytes = std::vector<std::uint8_t>;

struct TimeStamp {
  std::string utc;
  std::int64_t unix_ms = 0;
  std::uint64_t monotonic_ns = 0;
};

TimeStamp nowStamp();
std::string formatUtc(std::int64_t unix_ms);
std::string hexEncode(const std::uint8_t* data, std::size_t size);
std::string hexEncode(const Bytes& bytes);
std::string sha256Hex(const std::uint8_t* data, std::size_t size);
std::string sha256Hex(const Bytes& bytes);
std::string sha256File(const fs::path& path);
std::string randomToken(std::size_t bytes = 5);
std::string sanitizeName(const std::string& value);

Bytes readFile(const fs::path& path);
std::string readText(const fs::path& path);
void writeFile(const fs::path& path, const Bytes& bytes);
void writeText(const fs::path& path, const std::string& text);
void atomicWriteFile(const fs::path& path, const Bytes& bytes);
void atomicWriteText(const fs::path& path, const std::string& text);
void ensureDir(const fs::path& path);
void makeReadOnly(const fs::path& path);
void fsyncPath(const fs::path& path);
void fsyncDirectory(const fs::path& path);
void appendJournalLine(const fs::path& path, const std::string& line);
std::string posixErrorMessage(int error_number);

std::vector<std::string> split(const std::string& value, char delimiter);
std::string trim(const std::string& value);
std::string toLower(std::string value);
bool startsWith(const std::string& value, const std::string& prefix);
bool parseUint(const std::string& text, std::uint32_t& value);
bool parseDouble(const std::string& text, double& value);

class InterruptGuard {
 public:
  InterruptGuard();
  ~InterruptGuard();

  static bool requested();
  static void request();
  static void check(const char* stage);

 private:
  static std::atomic<bool> requested_;
};

}  // namespace gbi
