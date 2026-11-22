#include "rtsync/image.hpp"

#include "rtsync/json_util.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace rtsync {
namespace {

bool next_token(std::istream& input, std::string* token) {
  *token = "";
  char c = '\0';
  while (input.get(c)) {
    if (std::isspace(static_cast<unsigned char>(c))) continue;
    if (c == '#') {
      std::string ignored;
      std::getline(input, ignored);
      continue;
    }
    token->push_back(c);
    break;
  }
  if (token->empty()) return false;
  while (input.get(c)) {
    if (std::isspace(static_cast<unsigned char>(c))) break;
    token->push_back(c);
  }
  return true;
}

}  // namespace

bool load_ppm(const std::string& path, Image* image, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error) *error = "cannot open image " + path;
    return false;
  }
  std::string magic;
  std::string width_token;
  std::string height_token;
  std::string max_token;
  if (!next_token(input, &magic) || !next_token(input, &width_token) ||
      !next_token(input, &height_token) || !next_token(input, &max_token)) {
    if (error) *error = "invalid PPM header in " + path;
    return false;
  }
  if (magic != "P6" && magic != "P3") {
    if (error) *error = "only PPM P3/P6 is supported: " + path;
    return false;
  }
  const long width = std::strtol(width_token.c_str(), nullptr, 10);
  const long height = std::strtol(height_token.c_str(), nullptr, 10);
  const long max_value = std::strtol(max_token.c_str(), nullptr, 10);
  if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
      max_value <= 0 || max_value > 255) {
    if (error) *error = "invalid PPM dimensions/range in " + path;
    return false;
  }
  image->width = static_cast<std::uint32_t>(width);
  image->height = static_cast<std::uint32_t>(height);
  image->rgb.assign(static_cast<std::size_t>(width) *
                        static_cast<std::size_t>(height) * 3,
                    0);
  if (magic == "P3") {
    for (std::uint8_t& channel : image->rgb) {
      std::string token;
      if (!next_token(input, &token)) {
        if (error) *error = "short P3 pixel data in " + path;
        return false;
      }
      channel = static_cast<std::uint8_t>(
          std::clamp(std::strtol(token.c_str(), nullptr, 10), 0L, 255L));
    }
    return true;
  }
  input.read(reinterpret_cast<char*>(image->rgb.data()),
             static_cast<std::streamsize>(image->rgb.size()));
  if (static_cast<std::size_t>(input.gcount()) != image->rgb.size()) {
    if (error) *error = "short P6 pixel data in " + path;
    return false;
  }
  return true;
}

bool write_ppm(const std::string& path, const Image& image, std::string* error) {
  if (image.width == 0 || image.height == 0 ||
      image.rgb.size() != static_cast<std::size_t>(image.width) *
                               static_cast<std::size_t>(image.height) * 3) {
    if (error) *error = "invalid image for PPM output";
    return false;
  }
  std::ostringstream header;
  header << "P6\n" << image.width << " " << image.height << "\n255\n";
  std::string bytes = header.str();
  bytes.append(reinterpret_cast<const char*>(image.rgb.data()),
               image.rgb.size());
  return write_file(path, bytes, error);
}

Image make_diff_image(const Image& golden, const Image& observed) {
  Image diff;
  diff.width = std::min(golden.width, observed.width);
  diff.height = std::min(golden.height, observed.height);
  diff.rgb.assign(static_cast<std::size_t>(diff.width) *
                      static_cast<std::size_t>(diff.height) * 3,
                  0);
  for (std::uint32_t y = 0; y < diff.height; ++y) {
    for (std::uint32_t x = 0; x < diff.width; ++x) {
      const std::size_t index =
          (static_cast<std::size_t>(y) * diff.width + x) * 3;
      std::uint8_t delta = 0;
      for (int channel = 0; channel < 3; ++channel) {
        const int a = golden.rgb[index + static_cast<std::size_t>(channel)];
        const int b = observed.rgb[index + static_cast<std::size_t>(channel)];
        delta = std::max(delta, static_cast<std::uint8_t>(std::abs(a - b)));
      }
      diff.rgb[index] = delta;
      diff.rgb[index + 1] = static_cast<std::uint8_t>(delta / 3);
      diff.rgb[index + 2] = static_cast<std::uint8_t>(delta / 3);
    }
  }
  return diff;
}

}  // namespace rtsync

