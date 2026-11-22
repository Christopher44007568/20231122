#include "common.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace offscreen {

std::string readText(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot open " + path.string());
    }
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

Bytes readBytes(const std::filesystem::path& path) {
    const std::string text = readText(path);
    return Bytes(text.begin(), text.end());
}

void writeText(const std::filesystem::path& path, std::string_view text) {
    ensureDirectory(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("short write to " + path.string());
    }
}

void writeBytes(const std::filesystem::path& path, const Bytes& bytes) {
    ensureDirectory(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("short write to " + path.string());
    }
}

void ensureDirectory(const std::filesystem::path& path) {
    if (!path.empty()) {
        std::filesystem::create_directories(path);
    }
}

std::string jsonEscape(std::string_view value) {
    std::ostringstream out;
    for (const unsigned char c : value) {
        switch (c) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (c < 0x20) {
                    out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                        << static_cast<int>(c) << std::dec;
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string hex32(std::uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

std::uint32_t parseColor(std::string_view value) {
    std::string text = trim(value);
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() != 6) {
        throw std::runtime_error("invalid color: " + std::string(value));
    }
    std::uint32_t result = 0;
    for (const char c : text) {
        result <<= 4;
        if (c >= '0' && c <= '9') result |= static_cast<std::uint32_t>(c - '0');
        else if (c >= 'a' && c <= 'f') result |= static_cast<std::uint32_t>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') result |= static_cast<std::uint32_t>(c - 'A' + 10);
        else throw std::runtime_error("invalid color: " + std::string(value));
    }
    return result;
}

std::string colorString(std::uint32_t value) {
    std::ostringstream out;
    out << '#' << std::hex << std::setw(6) << std::setfill('0') << (value & 0xffffffu);
    return out.str();
}

std::vector<std::string> split(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t begin = 0;
    while (true) {
        const std::size_t end = text.find(delimiter, begin);
        parts.emplace_back(text.substr(begin, end == std::string_view::npos ? end : end - begin));
        if (end == std::string_view::npos) break;
        begin = end + 1;
    }
    return parts;
}

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &time);
#else
    gmtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

}  // namespace offscreen
