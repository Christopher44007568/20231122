#include "image.h"

#include "trace.h"  // fnv1a64

#include <cstdio>

namespace rpsc {

uint64_t Image::hash() const {
  return fnv1a64(rgba.data(), rgba.size());
}

bool savePpm(const std::string& path, const Image& img, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    *err = "cannot open for write: " + path;
    return false;
  }
  std::fprintf(f, "P6\n%u %u\n255\n", img.width, img.height);
  // PPM is RGB; drop alpha.
  for (size_t i = 0; i + 3 < img.rgba.size(); i += 4) {
    std::fputc(img.rgba[i], f);
    std::fputc(img.rgba[i + 1], f);
    std::fputc(img.rgba[i + 2], f);
  }
  std::fclose(f);
  return true;
}

bool loadPpm(const std::string& path, Image* img, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    *err = "cannot open: " + path;
    return false;
  }
  char magic[3] = {0, 0, 0};
  uint32_t w = 0, h = 0;
  int maxv = 0;
  if (std::fscanf(f, "%2s", magic) != 1 || std::string(magic) != "P6" ||
      std::fscanf(f, "%u %u", &w, &h) != 2 || std::fscanf(f, "%d", &maxv) != 1 ||
      maxv != 255) {
    std::fclose(f);
    *err = "bad PPM header: " + path;
    return false;
  }
  std::fgetc(f);  // single whitespace after maxval
  img->width = w;
  img->height = h;
  img->rgba.resize(size_t(w) * h * 4);
  for (size_t i = 0; i < img->rgba.size(); i += 4) {
    int r = std::fgetc(f), g = std::fgetc(f), b = std::fgetc(f);
    if (r == EOF || g == EOF || b == EOF) {
      std::fclose(f);
      *err = "truncated PPM: " + path;
      return false;
    }
    img->rgba[i] = uint8_t(r);
    img->rgba[i + 1] = uint8_t(g);
    img->rgba[i + 2] = uint8_t(b);
    img->rgba[i + 3] = 255;
  }
  std::fclose(f);
  return true;
}

DiffStats diffImages(const Image& a, const Image& b, Image* diffOut) {
  DiffStats s;
  if (a.width != b.width || a.height != b.height) {
    s.sizeMismatch = true;
    return s;
  }
  if (diffOut) {
    diffOut->width = a.width;
    diffOut->height = a.height;
    diffOut->rgba.resize(a.rgba.size());
  }
  uint64_t sum = 0;
  for (size_t i = 0; i < a.rgba.size(); i += 4) {
    uint32_t px = 0;
    for (int c = 0; c < 3; ++c) {
      uint32_t d = a.rgba[i + c] > b.rgba[i + c] ? a.rgba[i + c] - b.rgba[i + c]
                                                 : b.rgba[i + c] - a.rgba[i + c];
      px += d;
      if (d > s.maxDelta) s.maxDelta = d;
      if (diffOut) {
        uint32_t v = d * 4;
        diffOut->rgba[i + c] = uint8_t(v > 255 ? 255 : v);
      }
    }
    if (diffOut) diffOut->rgba[i + 3] = 255;
    if (px > 0) ++s.diffPixels;
    sum += px;
  }
  uint64_t samples = uint64_t(a.width) * a.height * 3;
  s.meanDelta = samples ? double(sum) / double(samples) : 0.0;
  return s;
}

}  // namespace rpsc
