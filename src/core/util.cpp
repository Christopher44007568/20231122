#include "core/util.h"
#include "core/sha256.h"

#include <sys/stat.h>
#include <time.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace ssor {

std::string utcNowMicros() {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    std::tm tm{};
    gmtime_r(&ts.tv_sec, &tm);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer),
                  "%04d-%02d-%02dT%02d:%02d:%02d.%06dZ",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ts.tv_nsec / 1000));
    return buffer;
}

std::int64_t epochMillis() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::int64_t monotonicNanos() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string sha256Hex(const void* data, std::size_t size) {
    Sha256 hash;
    hash.update(data, size);
    return hash.finishHex();
}

std::string sha256Hex(const std::string& data) {
    return sha256Hex(data.data(), data.size());
}

std::string sha256File(const std::string& path, bool* ok) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (ok) *ok = false;
        return {};
    }
    Sha256 hash;
    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        if (got > 0) hash.update(buffer, static_cast<std::size_t>(got));
    }
    if (ok) *ok = true;
    return hash.finishHex();
}

std::string shortHash(const std::string& hex, std::size_t chars) {
    return hex.substr(0, std::min(chars, hex.size()));
}

std::string hex64(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

std::string jsonEscape(const std::string& value) {
    std::ostringstream out;
    for (unsigned char c : value) {
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
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(c);
                } else {
                    out << static_cast<char>(c);
                }
        }
    }
    return out.str();
}

std::string upperHex(std::uint32_t value) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << value;
    return out.str();
}

std::string readFile(const std::string& path, std::size_t maxBytes,
                     bool* tooLarge, bool* ok) {
    if (tooLarge) *tooLarge = false;
    if (ok) *ok = false;
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) return {};
    const std::streamoff size = in.tellg();
    if (size < 0) return {};
    if (static_cast<std::uint64_t>(size) > maxBytes) {
        if (tooLarge) *tooLarge = true;
        return {};
    }
    std::string data(static_cast<std::size_t>(size), '\0');
    in.seekg(0);
    if (!data.empty()) {
        in.read(&data[0], static_cast<std::streamsize>(data.size()));
        if (!in) return {};
    }
    if (ok) *ok = true;
    return data;
}

bool writeFile(const std::string& path, const void* data, std::size_t size) {
    const std::string parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty()) makeDirs(parent);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    if (size) out.write(static_cast<const char*>(data),
                        static_cast<std::streamsize>(size));
    return static_cast<bool>(out);
}

bool writeText(const std::string& path, const std::string& text) {
    return writeFile(path, text.data(), text.size());
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

std::uint64_t fileSize(const std::string& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0 : static_cast<std::uint64_t>(size);
}

bool copyFileReadOnly(const std::string& src, const std::string& dst) {
    const std::string parent = std::filesystem::path(dst).parent_path().string();
    if (!parent.empty()) makeDirs(parent);
    std::ifstream in(src, std::ios::binary);
    if (!in) return false;
    std::ofstream out(dst, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    char buffer[65536];
    while (in) {
        in.read(buffer, sizeof(buffer));
        const std::streamsize got = in.gcount();
        if (got > 0) out.write(buffer, got);
    }
    if (!in.eof() || !out) return false;
    out.close();
    ::chmod(dst.c_str(), 0444);
    return true;
}

std::string pathJoin(const std::string& a, const std::string& b) {
    return (std::filesystem::path(a) / b).string();
}

std::string pathBase(const std::string& path) {
    return std::filesystem::path(path).filename().string();
}

bool makeDirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

bool writePpm(const std::string& path, const ImageRgba8& image) {
    if (image.width == 0 || image.height == 0 ||
        image.pixels.size() < static_cast<std::size_t>(image.width) * image.height * 4) {
        return false;
    }
    const std::string parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty()) makeDirs(parent);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "P6\n" << image.width << " " << image.height << "\n255\n";
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        out.put(static_cast<char>(image.pixels[i + 0]));
        out.put(static_cast<char>(image.pixels[i + 1]));
        out.put(static_cast<char>(image.pixels[i + 2]));
    }
    return static_cast<bool>(out);
}

ImageRgba8 readPpm(const std::string& path) {
    ImageRgba8 image;
    std::ifstream in(path, std::ios::binary);
    if (!in) return image;
    std::string magic;
    int maxValue = 0;
    if (!(in >> magic >> image.width >> image.height >> maxValue) ||
        magic != "P6" || maxValue != 255) {
        return image;
    }
    in.get();
    image.pixels.resize(static_cast<std::size_t>(image.width) * image.height * 4);
    for (std::size_t i = 0; i < image.pixels.size(); i += 4) {
        char rgb[3];
        in.read(rgb, 3);
        image.pixels[i + 0] = static_cast<std::uint8_t>(rgb[0]);
        image.pixels[i + 1] = static_cast<std::uint8_t>(rgb[1]);
        image.pixels[i + 2] = static_cast<std::uint8_t>(rgb[2]);
        image.pixels[i + 3] = 255;
    }
    return image;
}

}  // namespace ssor
