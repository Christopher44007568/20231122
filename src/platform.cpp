#include "gbi/util.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace gbi {

namespace {

constexpr std::array<std::uint32_t, 64> kSha256K = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU,
    0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U,
    0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U,
    0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
    0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U,
    0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
    0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
    0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U, 0xd192e819U,
    0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U, 0x1e376c08U,
    0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU,
    0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
    0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

constexpr std::array<std::uint32_t, 8> kSha256Initial = {
    0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
    0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

std::uint32_t rotateRight(std::uint32_t value, std::uint32_t count) {
  return (value >> count) | (value << (32U - count));
}

void sha256Block(std::array<std::uint32_t, 8>& state,
                 const std::uint8_t* block) {
  std::array<std::uint32_t, 64> words{};
  for (std::size_t i = 0; i < 16; ++i) {
    const std::size_t base = i * 4;
    words[i] = (static_cast<std::uint32_t>(block[base]) << 24U) |
               (static_cast<std::uint32_t>(block[base + 1]) << 16U) |
               (static_cast<std::uint32_t>(block[base + 2]) << 8U) |
               static_cast<std::uint32_t>(block[base + 3]);
  }
  for (std::size_t i = 16; i < 64; ++i) {
    const std::uint32_t s0 = rotateRight(words[i - 15], 7U) ^
                             rotateRight(words[i - 15], 18U) ^
                             (words[i - 15] >> 3U);
    const std::uint32_t s1 = rotateRight(words[i - 2], 17U) ^
                             rotateRight(words[i - 2], 19U) ^
                             (words[i - 2] >> 10U);
    words[i] = words[i - 16] + s0 + words[i - 7] + s1;
  }

  std::uint32_t a = state[0];
  std::uint32_t b = state[1];
  std::uint32_t c = state[2];
  std::uint32_t d = state[3];
  std::uint32_t e = state[4];
  std::uint32_t f = state[5];
  std::uint32_t g = state[6];
  std::uint32_t h = state[7];

  for (std::size_t i = 0; i < 64; ++i) {
    const std::uint32_t s1 = rotateRight(e, 6U) ^ rotateRight(e, 11U) ^
                             rotateRight(e, 25U);
    const std::uint32_t choice = (e & f) ^ ((~e) & g);
    const std::uint32_t temp1 = h + s1 + choice + kSha256K[i] + words[i];
    const std::uint32_t s0 = rotateRight(a, 2U) ^ rotateRight(a, 13U) ^
                             rotateRight(a, 22U);
    const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
    const std::uint32_t temp2 = s0 + majority;
    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

std::string toHex(const std::array<std::uint8_t, 32>& digest) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.resize(64);
  for (std::size_t i = 0; i < digest.size(); ++i) {
    output[i * 2] = kHex[digest[i] >> 4U];
    output[i * 2 + 1] = kHex[digest[i] & 0x0fU];
  }
  return output;
}

void writeAll(int fd, const std::uint8_t* data, std::size_t size) {
  while (size > 0) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("write failed: " + posixErrorMessage(errno));
    }
    data += static_cast<std::size_t>(written);
    size -= static_cast<std::size_t>(written);
  }
}

}  // namespace

std::atomic<bool> InterruptGuard::requested_{false};

InterruptGuard::InterruptGuard() {
  requested_.store(false);
  struct sigaction action {};
  action.sa_handler = [](int) { requested_.store(true); };
  sigemptyset(&action.sa_mask);
  action.sa_flags = 0;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
}

InterruptGuard::~InterruptGuard() = default;

bool InterruptGuard::requested() {
  return requested_.load();
}

void InterruptGuard::request() {
  requested_.store(true);
}

void InterruptGuard::check(const char* stage) {
  if (requested()) {
    throw std::runtime_error(std::string("interrupted before ") + stage);
  }
}

TimeStamp nowStamp() {
  const auto now = std::chrono::system_clock::now();
  const auto unix_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          now.time_since_epoch())
          .count();
  const auto monotonic =
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now().time_since_epoch())
          .count();
  TimeStamp stamp;
  stamp.utc = formatUtc(unix_ms);
  stamp.unix_ms = unix_ms;
  stamp.monotonic_ns = static_cast<std::uint64_t>(monotonic);
  return stamp;
}

std::string formatUtc(std::int64_t unix_ms) {
  const std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000);
  const std::int64_t millis = unix_ms % 1000;
  std::tm utc {};
  gmtime_r(&seconds, &utc);
  std::ostringstream stream;
  stream << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setw(3) << std::setfill('0') << millis << 'Z';
  return stream.str();
}

std::string hexEncode(const std::uint8_t* data, std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  std::string output;
  output.resize(size * 2);
  for (std::size_t i = 0; i < size; ++i) {
    output[i * 2] = kHex[data[i] >> 4U];
    output[i * 2 + 1] = kHex[data[i] & 0x0fU];
  }
  return output;
}

std::string hexEncode(const Bytes& bytes) {
  return hexEncode(bytes.data(), bytes.size());
}

std::string sha256Hex(const std::uint8_t* data, std::size_t size) {
  std::array<std::uint32_t, 8> state = kSha256Initial;
  std::size_t offset = 0;
  while (size - offset >= 64) {
    sha256Block(state, data + offset);
    offset += 64;
  }

  std::array<std::uint8_t, 128> tail{};
  const std::size_t remaining = size - offset;
  if (remaining > 0) {
    std::memcpy(tail.data(), data + offset, remaining);
  }
  tail[remaining] = 0x80U;
  const std::size_t tail_blocks = remaining < 56 ? 1 : 2;
  const std::uint64_t bit_length = static_cast<std::uint64_t>(size) * 8U;
  for (std::size_t i = 0; i < 8; ++i) {
    tail[tail_blocks * 64 - 1 - i] =
        static_cast<std::uint8_t>((bit_length >> (i * 8U)) & 0xffU);
  }
  sha256Block(state, tail.data());
  if (tail_blocks == 2) {
    sha256Block(state, tail.data() + 64);
  }

  std::array<std::uint8_t, 32> digest{};
  for (std::size_t i = 0; i < 8; ++i) {
    digest[i * 4] = static_cast<std::uint8_t>(state[i] >> 24U);
    digest[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16U);
    digest[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8U);
    digest[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
  }
  return toHex(digest);
}

std::string sha256Hex(const Bytes& bytes) {
  return sha256Hex(bytes.data(), bytes.size());
}

std::string sha256File(const fs::path& path) {
  const auto bytes = readFile(path);
  return sha256Hex(bytes);
}

std::string randomToken(std::size_t bytes) {
  Bytes random(bytes);
  const int fd = ::open("/dev/urandom", O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    const auto stamp = nowStamp();
    const std::string fallback = std::to_string(stamp.monotonic_ns) +
                                 std::to_string(stamp.unix_ms);
    return sha256Hex(reinterpret_cast<const std::uint8_t*>(fallback.data()),
                     fallback.size())
        .substr(0, bytes * 2);
  }
  std::size_t offset = 0;
  while (offset < bytes) {
    const ssize_t result = ::read(fd, random.data() + offset, bytes - offset);
    if (result <= 0) {
      break;
    }
    offset += static_cast<std::size_t>(result);
  }
  ::close(fd);
  return hexEncode(random).substr(0, bytes * 2);
}

std::string sanitizeName(const std::string& value) {
  std::string output;
  output.reserve(value.size());
  for (const unsigned char character : value) {
    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9') || character == '-' ||
        character == '_' || character == '.') {
      output.push_back(static_cast<char>(character));
    } else {
      output.push_back('_');
    }
  }
  if (output.empty() || output == "." || output == "..") {
    return "unnamed";
  }
  return output;
}

Bytes readFile(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    throw std::runtime_error("cannot open " + path.string() + ": " +
                             posixErrorMessage(errno));
  }
  struct stat info {};
  if (::fstat(fd, &info) != 0) {
    const int saved = errno;
    ::close(fd);
    throw std::runtime_error("cannot stat " + path.string() + ": " +
                             posixErrorMessage(saved));
  }
  Bytes bytes(static_cast<std::size_t>(info.st_size));
  std::size_t offset = 0;
  while (offset < bytes.size()) {
    const ssize_t count =
        ::read(fd, bytes.data() + offset, bytes.size() - offset);
    if (count < 0) {
      if (errno == EINTR) {
        continue;
      }
      const int saved = errno;
      ::close(fd);
      throw std::runtime_error("cannot read " + path.string() + ": " +
                               posixErrorMessage(saved));
    }
    if (count == 0) {
      break;
    }
    offset += static_cast<std::size_t>(count);
  }
  ::close(fd);
  bytes.resize(offset);
  return bytes;
}

std::string readText(const fs::path& path) {
  const Bytes bytes = readFile(path);
  return std::string(bytes.begin(), bytes.end());
}

void writeFile(const fs::path& path, const Bytes& bytes) {
  ensureDir(path.parent_path());
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error("cannot create " + path.string() + ": " +
                             posixErrorMessage(errno));
  }
  try {
    writeAll(fd, bytes.data(), bytes.size());
    if (::fsync(fd) != 0) {
      throw std::runtime_error("fsync failed for " + path.string() + ": " +
                               posixErrorMessage(errno));
    }
  } catch (...) {
    ::close(fd);
    throw;
  }
  ::close(fd);
}

void writeText(const fs::path& path, const std::string& text) {
  writeFile(path, Bytes(text.begin(), text.end()));
}

void atomicWriteFile(const fs::path& path, const Bytes& bytes) {
  ensureDir(path.parent_path());
  const fs::path temporary =
      path.parent_path() /
      ("." + path.filename().string() + ".tmp-" + randomToken(4));
  writeFile(temporary, bytes);
  if (::rename(temporary.c_str(), path.c_str()) != 0) {
    const int saved = errno;
    std::error_code ignored;
    fs::remove(temporary, ignored);
    throw std::runtime_error("cannot rename " + temporary.string() + " to " +
                             path.string() + ": " +
                             posixErrorMessage(saved));
  }
  fsyncDirectory(path.parent_path());
}

void atomicWriteText(const fs::path& path, const std::string& text) {
  atomicWriteFile(path, Bytes(text.begin(), text.end()));
}

void ensureDir(const fs::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code error;
  fs::create_directories(path, error);
  if (error) {
    throw std::runtime_error("cannot create directory " + path.string() + ": " +
                             error.message());
  }
}

void makeReadOnly(const fs::path& path) {
  std::error_code error;
  fs::permissions(path,
                  fs::perms::owner_read | fs::perms::group_read |
                      fs::perms::others_read,
                  fs::perm_options::replace, error);
  if (error) {
    throw std::runtime_error("cannot make read-only " + path.string() + ": " +
                             error.message());
  }
}

void fsyncPath(const fs::path& path) {
  const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
  if (fd < 0) {
    throw std::runtime_error("cannot open for fsync " + path.string() + ": " +
                             posixErrorMessage(errno));
  }
  const int result = ::fsync(fd);
  const int saved = errno;
  ::close(fd);
  if (result != 0) {
    throw std::runtime_error("fsync failed for " + path.string() + ": " +
                             posixErrorMessage(saved));
  }
}

void fsyncDirectory(const fs::path& path) {
  std::error_code error;
  if (!fs::exists(path, error)) {
    return;
  }
  fsyncPath(path);
}

void appendJournalLine(const fs::path& path, const std::string& line) {
  ensureDir(path.parent_path());
  const int fd =
      ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
  if (fd < 0) {
    throw std::runtime_error("cannot open journal " + path.string() + ": " +
                             posixErrorMessage(errno));
  }
  if (::flock(fd, LOCK_EX) != 0) {
    const int saved = errno;
    ::close(fd);
    throw std::runtime_error("cannot lock journal: " +
                             posixErrorMessage(saved));
  }
  try {
    const std::string record = line + "\n";
    writeAll(fd, reinterpret_cast<const std::uint8_t*>(record.data()),
             record.size());
    if (::fsync(fd) != 0) {
      throw std::runtime_error("journal fsync failed: " +
                               posixErrorMessage(errno));
    }
  } catch (...) {
    ::flock(fd, LOCK_UN);
    ::close(fd);
    throw;
  }
  ::flock(fd, LOCK_UN);
  ::close(fd);
}

std::string posixErrorMessage(int error_number) {
  return std::string(std::strerror(error_number));
}

std::vector<std::string> split(const std::string& value, char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= value.size()) {
    const std::size_t end = value.find(delimiter, start);
    if (end == std::string::npos) {
      parts.push_back(value.substr(start));
      break;
    }
    parts.push_back(value.substr(start, end - start));
    start = end + 1;
  }
  return parts;
}

std::string trim(const std::string& value) {
  const std::size_t first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return {};
  }
  const std::size_t last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::string toLower(std::string value) {
  for (char& character : value) {
    if (character >= 'A' && character <= 'Z') {
      character = static_cast<char>(character - 'A' + 'a');
    }
  }
  return value;
}

bool startsWith(const std::string& value, const std::string& prefix) {
  return value.size() >= prefix.size() &&
         value.compare(0, prefix.size(), prefix) == 0;
}

bool parseUint(const std::string& text, std::uint32_t& value) {
  try {
    std::size_t consumed = 0;
    const unsigned long parsed = std::stoul(text, &consumed, 0);
    if (consumed != text.size() || parsed > 0xffffffffUL) {
      return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
  } catch (...) {
    return false;
  }
}

bool parseDouble(const std::string& text, double& value) {
  try {
    std::size_t consumed = 0;
    value = std::stod(text, &consumed);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

}  // namespace gbi
