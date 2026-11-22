#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtsync {

struct Image {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<std::uint8_t> rgb;
};

bool load_ppm(const std::string& path, Image* image, std::string* error);
bool write_ppm(const std::string& path, const Image& image, std::string* error);
Image make_diff_image(const Image& golden, const Image& observed);

}  // namespace rtsync

