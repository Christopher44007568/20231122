#include "png.h"
#include <cstdio>

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, size_t len) {
  static uint32_t table[256];
  static bool init = false;
  if (!init) {
    for (uint32_t i = 0; i < 256; ++i) {
      uint32_t c = i;
      for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
      table[i] = c;
    }
    init = true;
  }
  crc ^= 0xffffffffu;
  for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
  return crc ^ 0xffffffffu;
}

static void putU32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(uint8_t(v >> 24));
  out.push_back(uint8_t(v >> 16));
  out.push_back(uint8_t(v >> 8));
  out.push_back(uint8_t(v));
}

static void putChunk(std::vector<uint8_t>& out, const char type[4],
                     const std::vector<uint8_t>& payload) {
  putU32(out, uint32_t(payload.size()));
  size_t typePos = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  uint32_t crc = crc32Update(0, out.data() + typePos, 4 + payload.size());
  putU32(out, crc);
}

bool writePngRgba(const std::string& path, uint32_t width, uint32_t height,
                  const std::vector<uint8_t>& rgba) {
  if (rgba.size() != size_t(width) * height * 4) return false;

  std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', 0x0d, 0x0a, 0x1a, 0x0a};

  std::vector<uint8_t> ihdr;
  putU32(ihdr, width);
  putU32(ihdr, height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0}); // 8-bit RGBA
  putChunk(png, "IHDR", ihdr);

  // Raw scanlines, filter byte 0 per row.
  std::vector<uint8_t> raw;
  raw.reserve(size_t(height) * (1 + size_t(width) * 4));
  for (uint32_t y = 0; y < height; ++y) {
    raw.push_back(0);
    const uint8_t* row = rgba.data() + size_t(y) * width * 4;
    raw.insert(raw.end(), row, row + size_t(width) * 4);
  }

  // zlib stream: CMF/FLG + stored deflate blocks + adler32.
  std::vector<uint8_t> z = {0x78, 0x01};
  size_t pos = 0;
  while (pos < raw.size() || (raw.empty() && pos == 0)) {
    size_t blockLen = raw.size() - pos;
    if (blockLen > 65535) blockLen = 65535;
    bool last = (pos + blockLen == raw.size());
    z.push_back(last ? 1 : 0);
    z.push_back(uint8_t(blockLen & 0xff));
    z.push_back(uint8_t(blockLen >> 8));
    uint16_t nlen = uint16_t(~blockLen);
    z.push_back(uint8_t(nlen & 0xff));
    z.push_back(uint8_t(nlen >> 8));
    z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + blockLen);
    pos += blockLen;
    if (raw.empty()) break;
  }
  uint32_t s1 = 1, s2 = 0;
  for (uint8_t b : raw) {
    s1 = (s1 + b) % 65521;
    s2 = (s2 + s1) % 65521;
  }
  putU32(z, (s2 << 16) | s1);
  putChunk(png, "IDAT", z);
  putChunk(png, "IEND", {});

  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(png.data(), 1, png.size(), f) == png.size();
  std::fclose(f);
  return ok;
}
