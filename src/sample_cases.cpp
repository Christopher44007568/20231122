#include "sample_cases.hpp"

#include <fstream>
#include <string>

#ifndef RR_SOURCE_DIR
#define RR_SOURCE_DIR "."
#endif

namespace rr {

std::filesystem::path bundledFixturesDirectory() {
    return std::filesystem::path(RR_SOURCE_DIR) / "fixtures";
}

bool writeEmptyInput(const std::filesystem::path& path, std::string& error) {
    std::error_code code;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot create empty input: " + path.string();
        return false;
    }
    return true;
}

bool writeOversizedInput(const std::filesystem::path& path,
                         std::size_t minimumBytes,
                         std::string& error) {
    std::error_code code;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot create oversized input: " + path.string();
        return false;
    }
    const std::string prefix =
        "{\"schema\":\"raster-regression/v1\",\"name\":\"oversized\","
        "\"padding\":\"";
    const std::string suffix = "\"}\n";
    stream.write(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    const std::size_t target =
        minimumBytes > prefix.size() + suffix.size()
            ? minimumBytes + 4096U
            : prefix.size() + suffix.size() + 1U;
    std::size_t remaining = target - prefix.size() - suffix.size();
    const char block[] = "0123456789abcdef";
    while (remaining > 0U) {
        const std::size_t count =
            remaining < sizeof(block) - 1U ? remaining : sizeof(block) - 1U;
        stream.write(block, static_cast<std::streamsize>(count));
        remaining -= count;
    }
    stream.write(suffix.data(), static_cast<std::streamsize>(suffix.size()));
    if (!stream) {
        error = "cannot write oversized input: " + path.string();
        return false;
    }
    return true;
}

bool writeLargeValidInput(const std::filesystem::path& path,
                          std::size_t referenceCount,
                          std::string& error) {
    std::error_code code;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), code);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot create large input: " + path.string();
        return false;
    }
    stream << "{\n"
           << "  \"schema\": \"raster-regression/v1\",\n"
           << "  \"name\": \"large-valid-input\",\n"
           << "  \"framebuffer\": {\"width\": 32, \"height\": 32},\n"
           << "  \"clear_color\": [0, 0, 0, 255],\n"
           << "  \"vertices\": [\n"
           << "    {\"position\": [-1, -1, 0, 1], \"rgba\": [255, 0, 0, 255]},\n"
           << "    {\"position\": [3, -1, 0, 1], \"rgba\": [255, 0, 0, 255]},\n"
           << "    {\"position\": [-1, 3, 0, 1], \"rgba\": [255, 0, 0, 255]}\n"
           << "  ],\n"
           << "  \"indices\": [0, 1, 2],\n"
           << "  \"rules\": [{\"id\": \"fill\", \"cull_mode\": \"none\"}],\n"
           << "  \"draws\": [{\"id\": \"cover\", \"first_index\": 0, \"index_count\": 3}],\n"
           << "  \"passes\": [{\"id\": \"large\", \"rule\": \"fill\", \"draws\": [\"cover\"]}],\n"
           << "  \"reference_pixels\": [\n";
    for (std::size_t i = 0; i < referenceCount; ++i) {
        stream << "    {\"pass\": \"large\", \"x\": 16, \"y\": 16, "
               << "\"rgba\": [255, 0, 0, 255], \"tolerance\": 0}";
        if (i + 1U != referenceCount) {
            stream << ',';
        }
        stream << '\n';
    }
    stream << "  ]\n}\n";
    if (!stream) {
        error = "cannot write large input: " + path.string();
        return false;
    }
    return true;
}

}  // namespace rr
