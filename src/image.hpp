#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace msphere {

struct RgbaImage {
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<uint8_t> pixels;

  bool valid() const {
    return width != 0 && height != 0 &&
           pixels.size() == static_cast<size_t>(width) * height * 4U;
  }
};

struct DiffBounds {
  uint32_t min_x = 0;
  uint32_t min_y = 0;
  uint32_t max_x = 0;
  uint32_t max_y = 0;
  bool valid = false;
};

struct DiffStats {
  uint64_t differing_pixels = 0;
  uint64_t differing_bytes = 0;
  uint32_t max_channel_delta = 0;
  double mean_channel_delta = 0.0;
  uint64_t first_differing_pixel = 0;
  uint64_t first_differing_raw_offset = 0;
  DiffBounds bounds;
};

bool write_png_rgb(const std::filesystem::path& path, const RgbaImage& image,
                   std::string* error);
bool write_raw_rgba(const std::filesystem::path& path, const RgbaImage& image,
                    std::string* error);

DiffStats compare_images(const RgbaImage& baseline, const RgbaImage& anomaly);
RgbaImage make_diff_image(const RgbaImage& baseline, const RgbaImage& anomaly,
                          uint32_t gain);

}  // namespace msphere
