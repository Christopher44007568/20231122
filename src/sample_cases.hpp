#pragma once

#include <filesystem>
#include <string>

namespace rr {

std::filesystem::path bundledFixturesDirectory();

bool writeEmptyInput(const std::filesystem::path& path, std::string& error);
bool writeOversizedInput(const std::filesystem::path& path,
                         std::size_t minimumBytes,
                         std::string& error);
bool writeLargeValidInput(const std::filesystem::path& path,
                          std::size_t referenceCount,
                          std::string& error);

}  // namespace rr
