#include "util.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace inspect {
namespace {

std::string formatUtc(std::chrono::system_clock::time_point point, bool with_ms) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(point);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            point - seconds)
                            .count();
    const std::time_t raw = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &raw);
#else
    gmtime_r(&raw, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S");
    if (with_ms) {
        out << '.' << std::setw(3) << std::setfill('0') << millis;
    }
    out << 'Z';
    return out.str();
}

unsigned long currentProcessId() {
#if defined(_WIN32)
    return static_cast<unsigned long>(_getpid());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

}  // namespace

ClockStamp nowStamp() {
    const auto wall = std::chrono::system_clock::now();
    const auto monotonic = std::chrono::steady_clock::now();
    ClockStamp stamp;
    stamp.utc = formatUtc(wall, true);
    stamp.epoch_ms = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            wall.time_since_epoch())
            .count());
    stamp.monotonic_ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            monotonic.time_since_epoch())
            .count());
    return stamp;
}

std::string makeRunId() {
    const auto stamp = nowStamp();
    std::string compact = stamp.utc;
    for (char& value : compact) {
        if (value == '-' || value == ':' || value == '.') {
            value = '_';
        }
    }
    return "run-" + compact + "-p" + std::to_string(currentProcessId());
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char byte : value) {
        switch (byte) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (byte < 0x20U) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned>(byte)
                        << std::dec;
                } else {
                    out << static_cast<char>(byte);
                }
                break;
        }
    }
    return out.str();
}

std::string quoteJson(const std::string& value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string hexU64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

bool readTextFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    out = buffer.str();
    return input.good() || input.eof();
}

bool readBinaryFile(const std::filesystem::path& path,
                    std::vector<std::uint8_t>& out) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        return false;
    }
    const auto end = input.tellg();
    if (end < 0) {
        return false;
    }
    out.resize(static_cast<std::size_t>(end));
    input.seekg(0, std::ios::beg);
    if (!out.empty()) {
        input.read(reinterpret_cast<char*>(out.data()),
                   static_cast<std::streamsize>(out.size()));
    }
    return input.good() || input.eof();
}

bool writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

bool writeBinaryFile(const std::filesystem::path& path,
                     const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
    }
    return output.good();
}

bool appendTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    if (!output) {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    return output.good();
}

bool copyFileExact(const std::filesystem::path& source,
                   const std::filesystem::path& destination) {
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error) {
        return false;
    }
    std::filesystem::copy_file(source, destination,
                               std::filesystem::copy_options::overwrite_existing,
                               error);
    return !error;
}

std::string trim(const std::string& value) {
    const auto begin = value.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = value.find_last_not_of(" \t\r\n");
    return value.substr(begin, end - begin + 1);
}

std::string basename(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

}  // namespace inspect
