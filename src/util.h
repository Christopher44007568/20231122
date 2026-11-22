#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "json.h"

struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba;

    Image() = default;
    Image(int w, int h) : width(w), height(h), rgba(static_cast<size_t>(w) * h * 4, 0) {}
    bool empty() const { return width <= 0 || height <= 0 || rgba.empty(); }
};

std::string joinPath(const std::string& a, const std::string& b);
void ensureDirectory(const std::filesystem::path& path);
std::string readTextFile(const std::string& path);
std::vector<uint8_t> readBinaryFile(const std::string& path);
void writeTextFile(const std::string& path, const std::string& text);
void writeBinaryFile(const std::string& path, const std::vector<uint8_t>& bytes);
void writeJsonFile(const std::string& path, const Json& value);
void copyFile(const std::string& source, const std::string& destination);
void writePpm(const std::string& path, const Image& image);
Image readPpm(const std::string& path);
std::string utcTimestamp();
std::string executableDirectory();
std::string relativePathOrSelf(const std::string& path, const std::string& root);
std::string formatBytes(uint64_t bytes);
std::string hex32(uint32_t value);
uint32_t hash32(uint32_t value);
uint64_t framePeriodNs();

