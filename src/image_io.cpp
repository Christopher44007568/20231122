#include "image_io.hpp"

#include <filesystem>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

bool writePng(const std::filesystem::path& path, const FrameData& frame,
              std::string& error) {
  if (frame.width <= 0 || frame.height <= 0 ||
      frame.rgba.size() <
        static_cast<std::size_t>(frame.width) * frame.height * 4) {
    error = "Invalid frame for PNG export";
    return false;
  }
  std::error_code ec;
  if (!path.parent_path().empty()) {
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
      error = "Cannot create image directory: " + ec.message();
      return false;
    }
  }
  const int stride = frame.width * 4;
  if (stbi_write_png(path.string().c_str(), frame.width, frame.height, 4,
                     frame.rgba.data(), stride) == 0) {
    error = "PNG export failed: " + path.string();
    return false;
  }
  return true;
}

