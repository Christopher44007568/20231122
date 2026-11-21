#include "core/io.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace vfcr {

namespace {

std::tm utcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t timestamp = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &timestamp);
#else
    gmtime_r(&timestamp, &utc);
#endif
    return utc;
}

void setError(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

}  // namespace

std::string nowIso8601Utc() {
    const std::tm utc = utcNow();
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

std::string compactTimestampUtc() {
    const std::tm utc = utcNow();
    char buffer[24];
    std::strftime(buffer, sizeof(buffer), "%Y%m%dT%H%M%SZ", &utc);
    return buffer;
}

bool readTextFile(const std::string& path, std::string& contents, std::string* error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        setError(error, "cannot open " + path);
        return false;
    }
    std::ostringstream stream;
    stream << input.rdbuf();
    contents = stream.str();
    return true;
}

bool writeTextFileAtomic(const std::string& path, const std::string& contents,
                         std::string* error) {
    return writeBinaryFileAtomic(path, contents.data(), contents.size(), error);
}

bool writeBinaryFileAtomic(const std::string& path, const void* data, std::size_t size,
                           std::string* error) {
    const std::string directory = parentPath(path);
    if (!directory.empty() && !ensureDirectory(directory, error)) {
        return false;
    }
    const std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output) {
            setError(error, "cannot write " + temporary);
            return false;
        }
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        output.flush();
        if (!output) {
            setError(error, "write failed for " + temporary);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        std::filesystem::remove(path, ec);
        ec.clear();
        std::filesystem::rename(temporary, path, ec);
    }
    if (ec) {
        setError(error, "atomic rename failed for " + path + ": " + ec.message());
        return false;
    }
    return true;
}

bool ensureDirectory(const std::string& path, std::string* error) {
    if (path.empty()) {
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    if (ec) {
        setError(error, "cannot create " + path + ": " + ec.message());
        return false;
    }
    return true;
}

bool pathExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::exists(path, ec);
}

std::string pathJoin(const std::string& left, const std::string& right) {
    if (left.empty()) {
        return right;
    }
    if (!left.empty() && left.back() == '/') {
        return left + right;
    }
    return left + "/" + right;
}

std::string parentPath(const std::string& path) {
    return std::filesystem::path(path).parent_path().string();
}

std::string fileName(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

}  // namespace vfcr
