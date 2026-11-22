#include "image.h"

#include <algorithm>
#include <fstream>
#include <stdexcept>

void savePpm(const Image& img, const std::string& path) {
  std::ofstream os(path, std::ios::binary | std::ios::trunc);
  if (!os) throw std::runtime_error("cannot write image: " + path);
  os << "P6\n" << img.width << " " << img.height << "\n255\n";
  for (size_t i = 0; i + 2 < img.rgba.size(); i += 4)
    os.write(reinterpret_cast<const char*>(&img.rgba[i]), 3);
  if (!os) throw std::runtime_error("failed while writing image: " + path);
}

DiffResult diffImages(const Image& a, const Image& b, uint8_t tolerance) {
  DiffResult r;
  if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size())
    throw std::runtime_error("image size mismatch during diff");
  r.diffImage.width = a.width;
  r.diffImage.height = a.height;
  r.diffImage.rgba.resize(a.rgba.size());
  r.minX = a.width;
  r.minY = a.height;
  for (uint32_t y = 0; y < a.height; ++y) {
    for (uint32_t x = 0; x < a.width; ++x) {
      size_t o = (static_cast<size_t>(y) * a.width + x) * 4;
      uint32_t delta = 0;
      for (int c = 0; c < 3; ++c) {
        int d = static_cast<int>(a.rgba[o + c]) - static_cast<int>(b.rgba[o + c]);
        delta = std::max(delta, static_cast<uint32_t>(d < 0 ? -d : d));
      }
      bool differs = delta > tolerance;
      if (differs) {
        ++r.differingPixels;
        r.maxChannelDelta = std::max<uint64_t>(r.maxChannelDelta, delta);
        r.minX = std::min(r.minX, x);
        r.minY = std::min(r.minY, y);
        r.maxX = std::max(r.maxX, x);
        r.maxY = std::max(r.maxY, y);
        r.diffImage.rgba[o + 0] = 255;
        r.diffImage.rgba[o + 1] = 0;
        r.diffImage.rgba[o + 2] = 0;
      } else {
        for (int c = 0; c < 3; ++c) r.diffImage.rgba[o + c] = a.rgba[o + c] / 4;
      }
      r.diffImage.rgba[o + 3] = 255;
    }
  }
  return r;
}
