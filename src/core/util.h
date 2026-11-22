#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ssor {

std::string utcNowMicros();
std::int64_t epochMillis();
std::int64_t monotonicNanos();

std::string sha256Hex(const void* data, std::size_t size);
std::string sha256Hex(const std::string& data);
std::string sha256File(const std::string& path, bool* ok = nullptr);
std::string shortHash(const std::string& hex, std::size_t chars = 12);

std::string hex64(std::uint64_t value);
std::string jsonEscape(const std::string& value);
std::string upperHex(std::uint32_t value);

std::string readFile(const std::string& path, std::size_t maxBytes,
                     bool* tooLarge = nullptr, bool* ok = nullptr);
bool writeFile(const std::string& path, const void* data, std::size_t size);
bool writeText(const std::string& path, const std::string& text);
bool fileExists(const std::string& path);
std::uint64_t fileSize(const std::string& path);
bool copyFileReadOnly(const std::string& src, const std::string& dst);

std::string pathJoin(const std::string& a, const std::string& b);
std::string pathBase(const std::string& path);
bool makeDirs(const std::string& path);

struct PixelRgba8 {
    std::uint8_t r = 0, g = 0, b = 0, a = 255;
};

struct ImageRgba8 {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> pixels;
};

bool writePpm(const std::string& path, const ImageRgba8& image);
ImageRgba8 readPpm(const std::string& path);

}  // namespace ssor
