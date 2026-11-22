#include "textures.h"
#include <cmath>

namespace tcpv {
namespace {

uint32_t hashU32(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352du;
  x ^= x >> 15;
  x *= 0x846ca68bu;
  x ^= x >> 16;
  return x;
}

} // namespace

Image makeSourceTexture(const std::string& kind, uint32_t w, uint32_t h) {
  Image img(w, h);
  for (uint32_t y = 0; y < h; y++) {
    for (uint32_t x = 0; x < w; x++) {
      uint8_t* p = img.px(x, y);
      float u = float(x) / float(w > 1 ? w - 1 : 1);
      float v = float(y) / float(h > 1 ? h - 1 : 1);
      if (kind == "checker") {
        bool on = ((x / 8) + (y / 8)) % 2 == 0;
        uint8_t c = on ? 235 : 20;
        p[0] = c;
        p[1] = uint8_t(on ? 180 : 60);
        p[2] = uint8_t(255 - c);
        p[3] = 255;
      } else if (kind == "noise") {
        uint32_t r = hashU32(x * 2654435761u ^ y * 40503u);
        p[0] = uint8_t(r);
        p[1] = uint8_t(r >> 8);
        p[2] = uint8_t(r >> 16);
        p[3] = 255;
      } else if (kind == "photo") {
        // Smooth blobs + fine detail: stresses BC7 more than BC1.
        float a = std::sin(u * 6.28318f * 2.0f) * std::cos(v * 6.28318f * 3.0f);
        float b = std::sin((u + v) * 6.28318f * 5.0f);
        float d = std::sin(u * 6.28318f * 31.0f) * std::sin(v * 6.28318f * 29.0f);
        p[0] = uint8_t(128 + 100 * a + 20 * d);
        p[1] = uint8_t(128 + 90 * b - 15 * d);
        p[2] = uint8_t(128 + 80 * a * b + 25 * d);
        p[3] = uint8_t(200 + 55 * a);
      } else { // "gradient"
        p[0] = uint8_t(u * 255.0f);
        p[1] = uint8_t(v * 255.0f);
        p[2] = uint8_t((1.0f - u) * (1.0f - v) * 255.0f);
        p[3] = 255;
      }
    }
  }
  return img;
}

} // namespace tcpv
