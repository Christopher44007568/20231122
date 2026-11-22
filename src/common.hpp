#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace offscreen {

using Bytes = std::vector<std::uint8_t>;

std::string readText(const std::filesystem::path& path);
Bytes readBytes(const std::filesystem::path& path);
void writeText(const std::filesystem::path& path, std::string_view text);
void writeBytes(const std::filesystem::path& path, const Bytes& bytes);
void ensureDirectory(const std::filesystem::path& path);

std::string jsonEscape(std::string_view value);
std::string hex32(std::uint32_t value);
std::string hex64(std::uint64_t value);
std::uint32_t parseColor(std::string_view value);
std::string colorString(std::uint32_t value);
std::vector<std::string> split(std::string_view text, char delimiter);
std::string trim(std::string_view text);
std::string utcNow();

}  // namespace offscreen
