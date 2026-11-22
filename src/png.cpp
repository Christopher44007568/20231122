#include "png.h"

#include <cstring>

#include "util.h"

namespace tar {
namespace {

void Put32(std::vector<uint8_t>& out, uint32_t v) {
  out.push_back(static_cast<uint8_t>(v >> 24));
  out.push_back(static_cast<uint8_t>(v >> 16));
  out.push_back(static_cast<uint8_t>(v >> 8));
  out.push_back(static_cast<uint8_t>(v));
}

uint32_t Get32(const uint8_t* p) {
  return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
         (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void WriteChunk(std::vector<uint8_t>& out, const char type[4],
                const std::vector<uint8_t>& payload) {
  Put32(out, static_cast<uint32_t>(payload.size()));
  size_t crc_off = out.size();
  out.insert(out.end(), type, type + 4);
  out.insert(out.end(), payload.begin(), payload.end());
  Put32(out, Crc32(out.data() + crc_off, 4 + payload.size()));
}

std::vector<uint8_t> ZlibStore(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out;
  out.push_back(0x78);  // CMF: 32K window, deflate
  out.push_back(0x01);  // FLG: check bits, no preset dict
  size_t off = 0;
  do {
    size_t chunk = size - off;
    if (chunk > 65535) chunk = 65535;
    bool final = (off + chunk == size);
    out.push_back(final ? 0x01 : 0x00);
    uint16_t len = static_cast<uint16_t>(chunk);
    uint16_t nlen = static_cast<uint16_t>(~len);
    out.push_back(static_cast<uint8_t>(len));
    out.push_back(static_cast<uint8_t>(len >> 8));
    out.push_back(static_cast<uint8_t>(nlen));
    out.push_back(static_cast<uint8_t>(nlen >> 8));
    out.insert(out.end(), data + off, data + off + chunk);
    off += chunk;
  } while (off < size);
  uint32_t adler = Adler32(data, size);
  Put32(out, adler);
  return out;
}

}  // namespace

void WritePng(const std::string& path, const Image& img) {
  if (img.width == 0 || img.height == 0 ||
      img.rgba.size() != static_cast<size_t>(img.width) * img.height * 4) {
    throw PipelineError("WritePng: bad image dimensions for " + path);
  }
  std::vector<uint8_t> png;
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  png.insert(png.end(), sig, sig + 8);

  std::vector<uint8_t> ihdr;
  Put32(ihdr, img.width);
  Put32(ihdr, img.height);
  ihdr.insert(ihdr.end(), {8, 6, 0, 0, 0});  // 8-bit RGBA, no interlace
  WriteChunk(png, "IHDR", ihdr);

  std::vector<uint8_t> raw;
  raw.reserve(static_cast<size_t>(img.height) * (1 + img.width * 4));
  const size_t stride = static_cast<size_t>(img.width) * 4;
  for (uint32_t y = 0; y < img.height; ++y) {
    raw.push_back(0);  // filter: none
    raw.insert(raw.end(), img.rgba.begin() + y * stride,
               img.rgba.begin() + (y + 1) * stride);
  }
  WriteChunk(png, "IDAT", ZlibStore(raw.data(), raw.size()));
  WriteChunk(png, "IEND", {});
  WriteFile(path, png.data(), png.size());
}

Image ReadPng(const std::string& path) {
  std::vector<uint8_t> png = ReadFile(path);
  const uint8_t sig[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
  if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0) {
    throw PipelineError("not a PNG: " + path);
  }
  Image img;
  int color_type = -1;
  std::vector<uint8_t> idat;
  size_t off = 8;
  while (off + 12 <= png.size()) {
    uint32_t len = Get32(&png[off]);
    if (off + 12 + len > png.size()) throw PipelineError("truncated PNG: " + path);
    const uint8_t* type = &png[off + 4];
    const uint8_t* payload = &png[off + 8];
    if (std::memcmp(type, "IHDR", 4) == 0) {
      img.width = Get32(payload);
      img.height = Get32(payload);
      if (payload[8] != 8) throw PipelineError("unsupported bit depth: " + path);
      color_type = payload[9];
      if (color_type != 6 && color_type != 2) {
        throw PipelineError("unsupported color type: " + path);
      }
      if (payload[12] != 0) throw PipelineError("interlaced PNG unsupported: " + path);
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      idat.insert(idat.end(), payload, payload + len);
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      break;
    }
    off += 12 + len;
  }
  if (img.width == 0 || img.height == 0 || idat.size() < 6) {
    throw PipelineError("missing IHDR/IDAT: " + path);
  }
  if (idat[0] != 0x78 || ((idat[0] << 8) | idat[1]) % 31 != 0) {
    throw PipelineError("bad zlib header: " + path);
  }
  // Inflate stored blocks only.
  std::vector<uint8_t> raw;
  size_t p = 2;
  for (;;) {
    if (p + 5 > idat.size() - 4) throw PipelineError("bad deflate stream: " + path);
    bool final = (idat[p] & 1) != 0;
    if ((idat[p] >> 1) != 0) {
      throw PipelineError("compressed deflate blocks unsupported: " + path);
    }
    uint16_t len = static_cast<uint16_t>(idat[p + 1] | (idat[p + 2] << 8));
    uint16_t nlen = static_cast<uint16_t>(idat[p + 3] | (idat[p + 4] << 8));
    if (static_cast<uint16_t>(~len) != nlen || p + 5 + len > idat.size()) {
      throw PipelineError("corrupt stored block: " + path);
    }
    raw.insert(raw.end(), idat.begin() + p + 5, idat.begin() + p + 5 + len);
    p += 5 + len;
    if (final) break;
  }
  size_t bpp = color_type == 6 ? 4 : 3;
  size_t stride = static_cast<size_t>(img.width) * bpp;
  if (raw.size() != static_cast<size_t>(img.height) * (stride + 1)) {
    throw PipelineError("unexpected raw size: " + path);
  }
  img.rgba.assign(static_cast<size_t>(img.width) * img.height * 4, 255);
  for (uint32_t y = 0; y < img.height; ++y) {
    const uint8_t* row = raw.data() + y * (stride + 1);
    if (row[0] != 0) throw PipelineError("PNG filters unsupported: " + path);
    for (uint32_t x = 0; x < img.width; ++x) {
      uint8_t* dst = img.rgba.data() + (static_cast<size_t>(y) * img.width + x) * 4;
      const uint8_t* src = row + 1 + static_cast<size_t>(x) * bpp;
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      dst[3] = bpp == 4 ? src[3] : 255;
    }
  }
  return img;
}

}  // namespace tar
