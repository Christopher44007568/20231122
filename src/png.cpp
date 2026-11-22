#include "image.h"
#include "util.h"
#include <cstring>

namespace tcpv {
namespace {

uint32_t crcTable[256];
bool crcReady = false;

void initCrc() {
  for (uint32_t n = 0; n < 256; n++) {
    uint32_t c = n;
    for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
    crcTable[n] = c;
  }
  crcReady = true;
}

uint32_t crc32(const uint8_t* p, size_t n, uint32_t c = 0xffffffffu) {
  if (!crcReady) initCrc();
  while (n--) c = crcTable[(c ^ *p++) & 0xff] ^ (c >> 8);
  return c;
}

uint32_t adler32(const uint8_t* p, size_t n) {
  uint32_t a = 1, b = 0;
  while (n) {
    size_t chunk = n > 5552 ? 5552 : n;
    n -= chunk;
    while (chunk--) {
      a += *p++;
      b += a;
    }
    a %= 65521;
    b %= 65521;
  }
  return (b << 16) | a;
}

void put32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v >> 24));
  out.push_back(uint8_t(v >> 16));
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}

void chunk(std::vector<uint8_t>& out, const char type[4],
           const std::vector<uint8_t>& data) {
  put32(out, uint32_t(data.size()));
  size_t start = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), data.begin(), data.end());
  uint32_t c = crc32(out.data() + start, out.size() - start) ^ 0xffffffffu;
  put32(out, c);
}

} // namespace

bool writePng(const std::string& path, const Image& img) {
  if (img.width == 0 || img.height == 0) return false;

  // Raw scanlines: filter byte 0 per row.
  std::vector<uint8_t> raw;
  raw.reserve(size_t(img.height) * (1 + size_t(img.width) * 4));
  for (uint32_t y = 0; y < img.height; y++) {
    raw.push_back(0);
    const uint8_t* row = &img.rgba[size_t(y) * img.width * 4];
    raw.insert(raw.end(), row, row + size_t(img.width) * 4);
  }

  // zlib stream with stored (uncompressed) deflate blocks.
  std::vector<uint8_t> z;
  z.push_back(0x78);
  z.push_back(0x01);
  size_t pos = 0;
  while (pos < raw.size() || (raw.empty() && pos == 0)) {
    size_t n = raw.size() - pos;
    if (n > 65535) n = 65535;
    bool last = (pos + n == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(n));
    z.push_back(uint8_t(n >> 8));
    uint16_t nn = uint16_t(~n);
    z.push_back(uint8_t(nn));
    z.push_back(uint8_t(nn >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n);
    pos += n;
    if (raw.empty()) break;
  }
  uint32_t ad = adler32(raw.data(), raw.size());
  put32(z, ad);

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};
  std::vector<uint8_t> ihdr;
  put32(ihdr, img.width);
  put32(ihdr, img.height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
  chunk(png, "IHDR", ihdr);
  chunk(png, "IDAT", z);
  chunk(png, "IEND", {});

  return writeFileBytes(path, png);
}

} // namespace tcpv
