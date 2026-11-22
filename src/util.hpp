#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace inspect {

struct ClockStamp {
    std::string utc;
    std::uint64_t epoch_ms = 0;
    std::uint64_t monotonic_ns = 0;
};

ClockStamp nowStamp();
std::string makeRunId();
std::string jsonEscape(const std::string& value);
std::string quoteJson(const std::string& value);
std::string hexU64(std::uint64_t value);

bool readTextFile(const std::filesystem::path& path, std::string& out);
bool readBinaryFile(const std::filesystem::path& path, std::vector<std::uint8_t>& out);
bool writeTextFile(const std::filesystem::path& path, const std::string& text);
bool writeBinaryFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes);
bool appendTextFile(const std::filesystem::path& path, const std::string& text);
bool copyFileExact(const std::filesystem::path& source,
                   const std::filesystem::path& destination);

std::string trim(const std::string& value);
std::string basename(const std::string& path);

}  // namespace inspect
