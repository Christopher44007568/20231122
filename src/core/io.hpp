#pragma once

#include <cstddef>
#include <string>

namespace vfcr {

std::string nowIso8601Utc();
std::string compactTimestampUtc();
bool readTextFile(const std::string& path, std::string& contents, std::string* error = nullptr);
bool writeTextFileAtomic(const std::string& path, const std::string& contents,
                         std::string* error = nullptr);
bool writeBinaryFileAtomic(const std::string& path, const void* data, std::size_t size,
                           std::string* error = nullptr);
bool ensureDirectory(const std::string& path, std::string* error = nullptr);
bool pathExists(const std::string& path);
std::string pathJoin(const std::string& left, const std::string& right);
std::string parentPath(const std::string& path);
std::string fileName(const std::string& path);

}  // namespace vfcr
