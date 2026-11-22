#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace inst {

namespace fs = std::filesystem;

class InputFormatError : public std::runtime_error {
public:
    explicit InputFormatError(const std::string& message)
        : std::runtime_error(message) {}
};

class RenderError : public std::runtime_error {
public:
    explicit RenderError(const std::string& message)
        : std::runtime_error(message) {}
};

inline uint64_t fnv1a64(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

inline uint64_t fnv1a64(const std::string& text) {
    return fnv1a64(text.data(), text.size());
}

inline std::string hex64(uint64_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(16) << std::setfill('0') << value;
    return out.str();
}

inline std::string jsonEscape(const std::string& text) {
    std::ostringstream out;
    for (unsigned char ch : text) {
        switch (ch) {
            case '"': out << "\\\""; break;
            case '\\': out << "\\\\"; break;
            case '\b': out << "\\b"; break;
            case '\f': out << "\\f"; break;
            case '\n': out << "\\n"; break;
            case '\r': out << "\\r"; break;
            case '\t': out << "\\t"; break;
            default:
                if (ch < 0x20) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<int>(ch);
                } else {
                    out << static_cast<char>(ch);
                }
        }
    }
    return out.str();
}

inline std::string jsonString(const std::string& text) {
    return "\"" + jsonEscape(text) + "\"";
}

inline std::string jsonBool(bool value) {
    return value ? "true" : "false";
}

inline std::string jsonNumber(double value) {
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

template <typename T>
std::string jsonInteger(T value) {
    return std::to_string(static_cast<long long>(value));
}

inline std::string joinJson(const std::vector<std::string>& fields) {
    std::ostringstream out;
    out << '{';
    for (size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) out << ',';
        out << fields[i];
    }
    out << '}';
    return out.str();
}

inline std::string field(const std::string& key, const std::string& value) {
    return jsonString(key) + ":" + value;
}

template <typename T,
          typename std::enable_if<std::is_integral<T>::value &&
                                      !std::is_same<T, bool>::value,
                                  int>::type = 0>
inline std::string field(const std::string& key, T value) {
    return jsonString(key) + ":" + std::to_string(value);
}

inline std::string field(const std::string& key, bool value) {
    return jsonString(key) + ":" + jsonBool(value);
}

inline std::string nowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    localtime_r(&time, &local);
    char buffer[40]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &local);
    return buffer;
}

inline std::vector<uint8_t> readFileBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw InputFormatError("cannot open " + path.string());
    const auto size = static_cast<size_t>(in.tellg());
    std::vector<uint8_t> bytes(size);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(size));
    if (!in && size != 0) {
        throw InputFormatError("cannot read " + path.string());
    }
    return bytes;
}

inline std::string fileHash64(const fs::path& path) {
    const std::vector<uint8_t> bytes = readFileBytes(path);
    return hex64(fnv1a64(bytes.data(), bytes.size()));
}

inline void writeTextFile(const fs::path& path, const std::string& text) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << text;
}

} // namespace inst
