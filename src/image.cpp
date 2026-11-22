#include "image.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>

namespace msphere {
namespace {

constexpr std::array<uint32_t, 256> make_crc_table() {
  std::array<uint32_t, 256> table{};
  for (uint32_t i = 0; i < 256U; ++i) {
    uint32_t value = i;
    for (int bit = 0; bit < 8; ++bit) {
      value = (value & 1U) != 0U ? (value >> 1U) ^ 0xedb88320U : value >> 1U;
    }
    table[i] = value;
  }
  return table;
}

constexpr std::array<uint32_t, 256> kCrcTable = make_crc_table();

void append_u32_be(std::vector<uint8_t>& out, uint32_t value) {
  out.push_back(static_cast<uint8_t>((value >> 24U) & 0xffU));
  out.push_back(static_cast<uint8_t>((value >> 16U) & 0xffU));
  out.push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
  out.push_back(static_cast<uint8_t>(value & 0xffU));
}

uint32_t crc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffU;
  for (size_t i = 0; i < size; ++i) {
    crc = kCrcTable[(crc ^ data[i]) & 0xffU] ^ (crc >> 8U);
  }
  return crc ^ 0xffffffffU;
}

void append_chunk(std::vector<uint8_t>& png, const char type[4],
                  const std::vector<uint8_t>& payload) {
  append_u32_be(png, static_cast<uint32_t>(payload.size()));
  const size_t crc_start = png.size();
  for (int i = 0; i < 4; ++i) {
    png.push_back(static_cast<uint8_t>(type[i]));
  }
  png.insert(png.end(), payload.begin(), payload.end());
  append_u32_be(png, crc32(png.data() + crc_start, 4U + payload.size()));
}

void append_deflate_stored(std::vector<uint8_t>& stream,
                           const std::vector<uint8_t>& raw) {
  stream.push_back(0x78U);
  stream.push_back(0x01U);
  size_t offset = 0;
  do {
    const size_t remaining = raw.size() - offset;
    const uint16_t length =
        static_cast<uint16_t>(std::min<size_t>(remaining, 65535U));
    const bool final_block = offset + length == raw.size();
    stream.push_back(final_block ? 0x01U : 0x00U);
    stream.push_back(static_cast<uint8_t>(length & 0xffU));
    stream.push_back(static_cast<uint8_t>((length >> 8U) & 0xffU));
    const uint16_t inverse = static_cast<uint16_t>(~length);
    stream.push_back(static_cast<uint8_t>(inverse & 0xffU));
    stream.push_back(static_cast<uint8_t>((inverse >> 8U) & 0xffU));
    stream.insert(stream.end(), raw.begin() + static_cast<std::ptrdiff_t>(offset),
                  raw.begin() + static_cast<std::ptrdiff_t>(offset + length));
    offset += length;
  } while (offset < raw.size());

  uint32_t a = 1;
  uint32_t b = 0;
  for (uint8_t byte : raw) {
    a = (a + byte) % 65521U;
    b = (b + a) % 65521U;
  }
  append_u32_be(stream, (b << 16U) | a);
}

std::vector<uint8_t> encode_png_rgb(const RgbaImage& image) {
  std::vector<uint8_t> png = {0x89U, 0x50U, 0x4eU, 0x47U, 0x0dU,
                              0x0aU, 0x1aU, 0x0aU};
  std::vector<uint8_t> ihdr;
  append_u32_be(ihdr, image.width);
  append_u32_be(ihdr, image.height);
  ihdr.insert(ihdr.end(), {8U, 2U, 0U, 0U, 0U});
  append_chunk(png, "IHDR", ihdr);

  std::vector<uint8_t> scanlines;
  scanlines.reserve((static_cast<size_t>(image.width) * 3U + 1U) *
                    image.height);
  for (uint32_t y = 0; y < image.height; ++y) {
    scanlines.push_back(0U);
    for (uint32_t x = 0; x < image.width; ++x) {
      const size_t p = (static_cast<size_t>(y) * image.width + x) * 4U;
      scanlines.push_back(image.pixels[p + 0U]);
      scanlines.push_back(image.pixels[p + 1U]);
      scanlines.push_back(image.pixels[p + 2U]);
    }
  }
  std::vector<uint8_t> compressed;
  compressed.reserve(scanlines.size() + scanlines.size() / 65535U * 5U + 16U);
  append_deflate_stored(compressed, scanlines);
  append_chunk(png, "IDAT", compressed);
  append_chunk(png, "IEND", {});
  return png;
}

}  // namespace

bool write_png_rgb(const std::filesystem::path& path, const RgbaImage& image,
                   std::string* error) {
  if (!image.valid()) {
    if (error != nullptr) {
      *error = "invalid RGBA image";
    }
    return false;
  }
  const std::vector<uint8_t> png = encode_png_rgb(image);
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot open PNG for writing";
    }
    return false;
  }
  stream.write(reinterpret_cast<const char*>(png.data()),
               static_cast<std::streamsize>(png.size()));
  return static_cast<bool>(stream);
}

bool write_raw_rgba(const std::filesystem::path& path, const RgbaImage& image,
                    std::string* error) {
  if (!image.valid()) {
    if (error != nullptr) {
      *error = "invalid RGBA image";
    }
    return false;
  }
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot open raw framebuffer for writing";
    }
    return false;
  }
  stream.write(reinterpret_cast<const char*>(image.pixels.data()),
               static_cast<std::streamsize>(image.pixels.size()));
  return static_cast<bool>(stream);
}

DiffStats compare_images(const RgbaImage& baseline, const RgbaImage& anomaly) {
  DiffStats stats;
  if (!baseline.valid() || !anomaly.valid() ||
      baseline.width != anomaly.width || baseline.height != anomaly.height) {
    return stats;
  }
  uint64_t total_delta = 0;
  uint64_t channels = 0;
  for (uint32_t y = 0; y < baseline.height; ++y) {
    for (uint32_t x = 0; x < baseline.width; ++x) {
      const size_t p = (static_cast<size_t>(y) * baseline.width + x) * 4U;
      bool first_for_pixel = true;
      for (size_t channel = 0; channel < 4U; ++channel) {
        const uint8_t a = baseline.pixels[p + channel];
        const uint8_t b = anomaly.pixels[p + channel];
        const uint32_t delta =
            a > b ? static_cast<uint32_t>(a - b) : static_cast<uint32_t>(b - a);
        total_delta += delta;
        ++channels;
        stats.max_channel_delta = std::max(stats.max_channel_delta, delta);
        if (delta != 0U) {
          ++stats.differing_bytes;
          if (first_for_pixel) {
            if (stats.differing_pixels == 0) {
              stats.first_differing_pixel =
                  static_cast<uint64_t>(y) * baseline.width + x;
              stats.first_differing_raw_offset = p;
            }
            ++stats.differing_pixels;
            first_for_pixel = false;
          }
          if (!stats.bounds.valid) {
            stats.bounds = {x, y, x, y, true};
          } else {
            stats.bounds.min_x = std::min(stats.bounds.min_x, x);
            stats.bounds.min_y = std::min(stats.bounds.min_y, y);
            stats.bounds.max_x = std::max(stats.bounds.max_x, x);
            stats.bounds.max_y = std::max(stats.bounds.max_y, y);
          }
        }
      }
    }
  }
  if (channels != 0U) {
    stats.mean_channel_delta =
        static_cast<double>(total_delta) / static_cast<double>(channels);
  }
  return stats;
}

RgbaImage make_diff_image(const RgbaImage& baseline, const RgbaImage& anomaly,
                          uint32_t gain) {
  RgbaImage diff;
  if (!baseline.valid() || !anomaly.valid() ||
      baseline.width != anomaly.width || baseline.height != anomaly.height) {
    return diff;
  }
  diff.width = baseline.width;
  diff.height = baseline.height;
  diff.pixels.resize(baseline.pixels.size());
  for (size_t p = 0; p < baseline.pixels.size(); p += 4U) {
    const uint8_t r = static_cast<uint8_t>(
        std::min<uint32_t>((baseline.pixels[p] > anomaly.pixels[p]
                                ? baseline.pixels[p] - anomaly.pixels[p]
                                : anomaly.pixels[p] - baseline.pixels[p]) *
                               gain,
                           255U));
    const uint8_t g = static_cast<uint8_t>(
        std::min<uint32_t>((baseline.pixels[p + 1U] > anomaly.pixels[p + 1U]
                                ? baseline.pixels[p + 1U] - anomaly.pixels[p + 1U]
                                : anomaly.pixels[p + 1U] - baseline.pixels[p + 1U]) *
                               gain,
                           255U));
    const uint8_t b = static_cast<uint8_t>(
        std::min<uint32_t>((baseline.pixels[p + 2U] > anomaly.pixels[p + 2U]
                                ? baseline.pixels[p + 2U] - anomaly.pixels[p + 2U]
                                : anomaly.pixels[p + 2U] - baseline.pixels[p + 2U]) *
                               gain,
                           255U));
    diff.pixels[p + 0U] = r;
    diff.pixels[p + 1U] = g;
    diff.pixels[p + 2U] = b;
    diff.pixels[p + 3U] = 255U;
  }
  return diff;
}

}  // namespace msphere
