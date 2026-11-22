#include "util.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <unistd.h>

namespace {

std::string readPpmToken(std::istream& input) {
    std::string token;
    while (input >> token) {
        if (!token.empty() && token[0] == '#') {
            std::string ignored;
            std::getline(input, ignored);
            continue;
        }
        return token;
    }
    throw std::runtime_error("truncated PPM header");
}

}  // namespace

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    return (std::filesystem::path(a) / b).string();
}

void ensureDirectory(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    if (error) throw std::runtime_error("cannot create directory " + path.string() + ": " + error.message());
}

std::string readTextFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file for reading: " + path);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

std::vector<uint8_t> readBinaryFile(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open file for reading: " + path);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(input)),
                                std::istreambuf_iterator<char>());
}

void writeTextFile(const std::string& path, const std::string& text) {
    ensureDirectory(std::filesystem::path(path).parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open file for writing: " + path);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) throw std::runtime_error("failed writing: " + path);
}

void writeBinaryFile(const std::string& path, const std::vector<uint8_t>& bytes) {
    ensureDirectory(std::filesystem::path(path).parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("cannot open file for writing: " + path);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("failed writing: " + path);
}

void writeJsonFile(const std::string& path, const Json& value) {
    writeTextFile(path, dumpJson(value, true));
}

void copyFile(const std::string& source, const std::string& destination) {
    ensureDirectory(std::filesystem::path(destination).parent_path());
    std::error_code error;
    std::filesystem::copy_file(source, destination, std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        throw std::runtime_error("cannot copy " + source + " to " + destination + ": " + error.message());
    }
}

void writePpm(const std::string& path, const Image& image) {
    if (image.empty()) throw std::runtime_error("cannot write empty PPM");
    std::ostringstream header;
    header << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    const std::string headerText = header.str();
    std::vector<uint8_t> bytes;
    bytes.reserve(headerText.size() + static_cast<size_t>(image.width) * image.height * 3);
    bytes.insert(bytes.end(), headerText.begin(), headerText.end());
    for (size_t i = 0; i + 3 < image.rgba.size(); i += 4) {
        bytes.push_back(image.rgba[i]);
        bytes.push_back(image.rgba[i + 1]);
        bytes.push_back(image.rgba[i + 2]);
    }
    writeBinaryFile(path, bytes);
}

Image readPpm(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open PPM: " + path);
    std::string magic = readPpmToken(input);
    if (magic != "P6") throw std::runtime_error("only binary P6 PPM is supported");
    int width = std::stoi(readPpmToken(input));
    int height = std::stoi(readPpmToken(input));
    int maximum = std::stoi(readPpmToken(input));
    if (width <= 0 || height <= 0 || maximum != 255) throw std::runtime_error("invalid PPM header");
    input.get();
    Image image(width, height);
    std::vector<uint8_t> rgb(static_cast<size_t>(width) * height * 3);
    input.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    if (input.gcount() != static_cast<std::streamsize>(rgb.size())) {
        throw std::runtime_error("truncated PPM pixel data");
    }
    for (size_t i = 0; i < rgb.size() / 3; ++i) {
        image.rgba[i * 4] = rgb[i * 3];
        image.rgba[i * 4 + 1] = rgb[i * 3 + 1];
        image.rgba[i * 4 + 2] = rgb[i * 3 + 2];
        image.rgba[i * 4 + 3] = 255;
    }
    return image;
}

std::string utcTimestamp() {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t seconds = system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

std::string executableDirectory() {
    std::vector<char> buffer(4096);
    ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0) return ".";
    buffer[static_cast<size_t>(length)] = '\0';
    return std::filesystem::path(buffer.data()).parent_path().string();
}

std::string relativePathOrSelf(const std::string& path, const std::string& root) {
    std::error_code error;
    auto relative = std::filesystem::relative(path, root, error);
    if (error || relative.empty()) return path;
    return relative.generic_string();
}

std::string formatBytes(uint64_t bytes) {
    std::ostringstream out;
    if (bytes < 1024) {
        out << bytes << " B";
    } else if (bytes < 1024 * 1024) {
        out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / 1024.0 << " KiB";
    } else {
        out << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024.0 * 1024.0) << " MiB";
    }
    return out.str();
}

std::string hex32(uint32_t value) {
    std::ostringstream out;
    out << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

uint32_t hash32(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

uint64_t framePeriodNs() {
    return 200000000ULL;
}

