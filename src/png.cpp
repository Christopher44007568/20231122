#include "png.h"
#include "trace.h"

#include <fstream>

namespace {

void putBE32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back((v >> 24) & 0xFF); b.push_back((v >> 16) & 0xFF);
  b.push_back((v >> 8) & 0xFF); b.push_back(v & 0xFF);
}

void chunk(std::vector<uint8_t>& b, const char* type,
           const std::vector<uint8_t>& payload) {
  putBE32(b, static_cast<uint32_t>(payload.size()));
  size_t crcStart = b.size();
  b.insert(b.end(), type, type + 4);
  b.insert(b.end(), payload.begin(), payload.end());
  putBE32(b, trace::crc32(b.data() + crcStart, b.size() - crcStart));
}

uint32_t adler32(const uint8_t* p, size_t n) {
  uint32_t a = 1, s = 0;
  for (size_t i = 0; i < n; ++i) {
    a = (a + p[i]) % 65521;
    s = (s + a) % 65521;
  }
  return (s << 16) | a;
}

}  // namespace

bool writePng(const std::string& path, const Image& img) {
  if (!img.width || !img.height) return false;
  // Raw scanlines, filter byte 0 per row.
  std::vector<uint8_t> raw;
  raw.reserve((img.width * 4 + 1) * img.height);
  for (uint32_t y = 0; y < img.height; ++y) {
    raw.push_back(0);
    const uint8_t* row = img.rgba.data() + size_t(y) * img.width * 4;
    raw.insert(raw.end(), row, row + img.width * 4);
  }
  // zlib stream: stored deflate blocks (max 65535 each).
  std::vector<uint8_t> z;
  z.push_back(0x78); z.push_back(0x01);
  size_t off = 0;
  while (off < raw.size()) {
    uint16_t len = static_cast<uint16_t>(std::min<size_t>(65535, raw.size() - off));
    bool last = (off + len == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back(len & 0xFF); z.push_back((len >> 8) & 0xFF);
    uint16_t nlen = ~len;
    z.push_back(nlen & 0xFF); z.push_back((nlen >> 8) & 0xFF);
    z.insert(z.end(), raw.begin() + off, raw.begin() + off + len);
    off += len;
  }
  putBE32(z, adler32(raw.data(), raw.size()));

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  std::vector<uint8_t> ihdr;
  putBE32(ihdr, img.width); putBE32(ihdr, img.height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});

  std::ofstream out(path, std::ios::binary);
  if (!out) return false;
  out.write(reinterpret_cast<const char*>(png.data()), png.size());
  return true;
}

Image diffHeatmap(const Image& a, const Image& b,
                  uint64_t* diffPixelCount, uint32_t* maxChannelDiff) {
  Image out;
  out.width = std::min(a.width, b.width);
  out.height = std::min(a.height, b.height);
  out.rgba.resize(size_t(out.width) * out.height * 4, 255);
  uint64_t count = 0;
  uint32_t maxd = 0;
  for (size_t i = 0; i < size_t(out.width) * out.height; ++i) {
    uint32_t d = 0;
    for (int c = 0; c < 3; ++c) {
      int delta = int(a.rgba[i * 4 + c]) - int(b.rgba[i * 4 + c]);
      if (delta < 0) delta = -delta;
      d = std::max(d, uint32_t(delta));
    }
    if (d) ++count;
    maxd = std::max(maxd, d);
    uint8_t v = static_cast<uint8_t>(std::min(255u, d * 4));
    out.rgba[i * 4 + 0] = v;
    out.rgba[i * 4 + 1] = d ? 24 : 0;
    out.rgba[i * 4 + 2] = d ? 24 : 0;
  }
  if (diffPixelCount) *diffPixelCount = count;
  if (maxChannelDiff) *maxChannelDiff = maxd;
  return out;
}
