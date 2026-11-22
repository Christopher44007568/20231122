#include "png_writer.hpp"

#include <array>
#include <cstring>
#include <stdexcept>

extern "C" {
using exp_Bytef = unsigned char;
using exp_uLong = unsigned long;
using exp_uLongf = unsigned long;
unsigned long compressBound(unsigned long sourceLen);
int compress2(exp_Bytef* dest, exp_uLongf* destLen, const exp_Bytef* source,
              exp_uLong sourceLen, int level);
}

namespace offscreen {
namespace {

void appendU32(Bytes& out, std::uint32_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 24));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}

std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void appendChunk(Bytes& out, const char type[5], const Bytes& payload) {
    appendU32(out, static_cast<std::uint32_t>(payload.size()));
    Bytes crcData;
    crcData.insert(crcData.end(), type, type + 4);
    crcData.insert(crcData.end(), payload.begin(), payload.end());
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), payload.begin(), payload.end());
    appendU32(out, crc32(crcData.data(), crcData.size()));
}

}  // namespace

void writeRgbaPng(const std::filesystem::path& path, std::uint32_t width,
                  std::uint32_t height, const Bytes& pixels) {
    const std::size_t expected = static_cast<std::size_t>(width) * height * 4;
    if (pixels.size() != expected) {
        throw std::runtime_error("PNG input size does not match dimensions");
    }

    Bytes scanlines;
    scanlines.reserve(expected + height);
    for (std::uint32_t y = 0; y < height; ++y) {
        scanlines.push_back(0);
        const auto begin = pixels.begin() + static_cast<std::ptrdiff_t>(y) * width * 4;
        scanlines.insert(scanlines.end(), begin,
                         begin + static_cast<std::ptrdiff_t>(width) * 4);
    }

    exp_uLongf compressedSize = compressBound(static_cast<exp_uLong>(scanlines.size()));
    Bytes compressed(compressedSize);
    const int rc = compress2(compressed.data(), &compressedSize, scanlines.data(),
                             static_cast<exp_uLong>(scanlines.size()), 9);
    if (rc != 0) {
        throw std::runtime_error("zlib compression failed");
    }
    compressed.resize(compressedSize);

    Bytes png{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    Bytes ihdr;
    appendU32(ihdr, width);
    appendU32(ihdr, height);
    ihdr.push_back(8);
    ihdr.push_back(6);
    ihdr.insert(ihdr.end(), {0, 0, 0});
    appendChunk(png, "IHDR", ihdr);
    appendChunk(png, "IDAT", compressed);
    appendChunk(png, "IEND", {});
    writeBytes(path, png);
}

}  // namespace offscreen
