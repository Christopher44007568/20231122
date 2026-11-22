#include "codecs.h"
#include <algorithm>
#include <cstring>

namespace tcpv {
namespace {

struct Rgba {
  int r, g, b, a;
};

Rgba getPx(const Image& img, uint32_t x, uint32_t y) {
  x = std::min(x, img.width - 1);
  y = std::min(y, img.height - 1);
  const uint8_t* p = img.px(x, y);
  return {p[0], p[1], p[2], p[3]};
}

uint16_t pack565(int r, int g, int b) {
  return uint16_t(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

void unpack565(uint16_t c, int& r, int& g, int& b) {
  r = ((c >> 11) & 31) * 255 / 31;
  g = ((c >> 5) & 63) * 255 / 63;
  b = (c & 31) * 255 / 31;
}

// ---- BC1 -----------------------------------------------------------------

void encodeBc1Block(const Image& img, uint32_t bx, uint32_t by, uint8_t out[8]) {
  int minr = 255, ming = 255, minb = 255, maxr = 0, maxg = 0, maxb = 0;
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 4; x++) {
      Rgba p = getPx(img, bx * 4 + x, by * 4 + y);
      minr = std::min(minr, p.r); maxr = std::max(maxr, p.r);
      ming = std::min(ming, p.g); maxg = std::max(maxg, p.g);
      minb = std::min(minb, p.b); maxb = std::max(maxb, p.b);
    }
  uint16_t c0 = pack565(maxr, maxg, maxb);
  uint16_t c1 = pack565(minr, ming, minb);
  if (c0 < c1) std::swap(c0, c1);
  if (c0 == c1) c1 = c0 > 0 ? c0 - 1 : 0; // keep 4-color mode
  int r[4], g[4], b[4];
  unpack565(c0, r[0], g[0], b[0]);
  unpack565(c1, r[1], g[1], b[1]);
  r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
  r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
  uint32_t idx = 0;
  for (uint32_t i = 0; i < 16; i++) {
    Rgba p = getPx(img, bx * 4 + (i % 4), by * 4 + (i / 4));
    int best = 0, bestd = 1 << 30;
    for (int k = 0; k < 4; k++) {
      int dr = p.r - r[k], dg = p.g - g[k], db = p.b - b[k];
      int d = dr * dr + dg * dg + db * db;
      if (d < bestd) { bestd = d; best = k; }
    }
    idx |= uint32_t(best) << (2 * i);
  }
  out[0] = uint8_t(c0); out[1] = uint8_t(c0 >> 8);
  out[2] = uint8_t(c1); out[3] = uint8_t(c1 >> 8);
  out[4] = uint8_t(idx); out[5] = uint8_t(idx >> 8);
  out[6] = uint8_t(idx >> 16); out[7] = uint8_t(idx >> 24);
}

void decodeBc1Block(const uint8_t in[8], Rgba px[16]) {
  uint16_t c0 = uint16_t(in[0] | (in[1] << 8));
  uint16_t c1 = uint16_t(in[2] | (in[3] << 8));
  int r[4], g[4], b[4], a[4] = {255, 255, 255, 255};
  unpack565(c0, r[0], g[0], b[0]);
  unpack565(c1, r[1], g[1], b[1]);
  if (c0 > c1) {
    r[2] = (2 * r[0] + r[1]) / 3; g[2] = (2 * g[0] + g[1]) / 3; b[2] = (2 * b[0] + b[1]) / 3;
    r[3] = (r[0] + 2 * r[1]) / 3; g[3] = (g[0] + 2 * g[1]) / 3; b[3] = (b[0] + 2 * b[1]) / 3;
  } else {
    r[2] = (r[0] + r[1]) / 2; g[2] = (g[0] + g[1]) / 2; b[2] = (b[0] + b[1]) / 2;
    r[3] = g[3] = b[3] = 0; a[3] = 0;
  }
  uint32_t idx = uint32_t(in[4]) | (uint32_t(in[5]) << 8) |
                 (uint32_t(in[6]) << 16) | (uint32_t(in[7]) << 24);
  for (int i = 0; i < 16; i++) {
    int k = (idx >> (2 * i)) & 3;
    px[i] = {r[k], g[k], b[k], a[k]};
  }
}

// ---- BC3 -----------------------------------------------------------------
// BC3 = BC1 color block + explicit alpha block (8 interpolated alpha values).

void encodeBc3AlphaBlock(const Image& img, uint32_t bx, uint32_t by,
                         uint8_t out[8]) {
  int amin = 255, amax = 0;
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 4; x++) {
      int a = getPx(img, bx * 4 + x, by * 4 + y).a;
      amin = std::min(amin, a);
      amax = std::max(amax, a);
    }
  uint8_t a0 = uint8_t(amax), a1 = uint8_t(amin);
  if (a0 == a1 && a0 > 0) a1 = a0 - 1; // keep 8-alpha mode
  int pal[8];
  pal[0] = a0;
  pal[1] = a1;
  for (int i = 1; i <= 6; i++) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
  uint64_t bits = 0;
  for (uint32_t i = 0; i < 16; i++) {
    int a = getPx(img, bx * 4 + (i % 4), by * 4 + (i / 4)).a;
    int best = 0, bestd = 1 << 30;
    for (int k = 0; k < 8; k++) {
      int d = (a - pal[k]) * (a - pal[k]);
      if (d < bestd) { bestd = d; best = k; }
    }
    bits |= uint64_t(best) << (3 * i);
  }
  out[0] = a0;
  out[1] = a1;
  for (int i = 0; i < 6; i++) out[2 + i] = uint8_t(bits >> (8 * i));
}

void decodeBc3AlphaBlock(const uint8_t in[8], int alpha[16]) {
  int a0 = in[0], a1 = in[1];
  int pal[8];
  pal[0] = a0;
  pal[1] = a1;
  if (a0 > a1) {
    for (int i = 1; i <= 6; i++) pal[1 + i] = ((7 - i) * a0 + i * a1) / 7;
  } else {
    for (int i = 1; i <= 4; i++) pal[1 + i] = ((5 - i) * a0 + i * a1) / 5;
    pal[6] = 0;
    pal[7] = 255;
  }
  uint64_t bits = 0;
  for (int i = 0; i < 6; i++) bits |= uint64_t(in[2 + i]) << (8 * i);
  for (int i = 0; i < 16; i++) alpha[i] = pal[(bits >> (3 * i)) & 7];
}

// ---- BC7 (mode 6 only) ----------------------------------------------------
// Mode 6: RGBA endpoints 7.7.7.7 + 1 p-bit each, 3-bit indices, 1 subset.

struct BitWriter {
  uint8_t* out;
  int bitpos = 0;
  void put(uint32_t v, int nbits) {
    for (int i = 0; i < nbits; i++) {
      int byte = bitpos >> 3, bit = bitpos & 7;
      if ((v >> i) & 1) out[byte] |= uint8_t(1u << bit);
      bitpos++;
    }
  }
};

struct BitReader {
  const uint8_t* in;
  int bitpos = 0;
  uint32_t get(int nbits) {
    uint32_t v = 0;
    for (int i = 0; i < nbits; i++) {
      int byte = bitpos >> 3, bit = bitpos & 7;
      if ((in[byte] >> bit) & 1) v |= 1u << i;
      bitpos++;
    }
    return v;
  }
};

// Quantize 8-bit to 7-bit with a fixed p-bit, minimizing reconstruction error.
int quantize7p(int v, int p) {
  int bestQ = 0, bestErr = 1 << 30;
  for (int q = 0; q <= 127; q++) {
    int err = std::abs(((q << 1) | p) - v);
    if (err < bestErr) { bestErr = err; bestQ = q; }
  }
  return bestQ;
}

// Pick the p-bit for one endpoint (min total error across its 4 channels).
int pickPbit(const int v[4]) {
  int err[2] = {0, 0};
  for (int p = 0; p <= 1; p++)
    for (int k = 0; k < 4; k++) {
      int q = quantize7p(v[k], p);
      err[p] += std::abs(((q << 1) | p) - v[k]);
    }
  return err[1] < err[0] ? 1 : 0;
}

void encodeBc7Block(const Image& img, uint32_t bx, uint32_t by, uint8_t out[16]) {
  std::memset(out, 0, 16);
  int lo[4] = {255, 255, 255, 255}, hi[4] = {0, 0, 0, 0};
  for (uint32_t y = 0; y < 4; y++)
    for (uint32_t x = 0; x < 4; x++) {
      Rgba p = getPx(img, bx * 4 + x, by * 4 + y);
      int c[4] = {p.r, p.g, p.b, p.a};
      for (int k = 0; k < 4; k++) {
        lo[k] = std::min(lo[k], c[k]);
        hi[k] = std::max(hi[k], c[k]);
      }
    }
  // Mode 6 has one p-bit per endpoint, shared across that endpoint's channels.
  int p0 = pickPbit(lo);
  int p1 = pickPbit(hi);
  int q0[4], q1[4];
  for (int k = 0; k < 4; k++) {
    q0[k] = quantize7p(lo[k], p0);
    q1[k] = quantize7p(hi[k], p1);
  }
  int e0[4], e1[4];
  for (int k = 0; k < 4; k++) {
    e0[k] = (q0[k] << 1) | p0;
    e1[k] = (q1[k] << 1) | p1;
  }
  static const int W[8] = {0, 9, 18, 27, 37, 46, 55, 64};
  BitWriter w{out, 0};
  w.put(1u << 6, 7); // mode 6
  for (int k = 0; k < 4; k++) { w.put(uint32_t(q0[k]), 7); w.put(uint32_t(q1[k]), 7); }
  w.put(uint32_t(p0), 1);
  w.put(uint32_t(p1), 1);
  for (uint32_t i = 0; i < 16; i++) {
    Rgba px = getPx(img, bx * 4 + (i % 4), by * 4 + (i / 4));
    int c[4] = {px.r, px.g, px.b, px.a};
    int best = 0, bestd = 1 << 30;
    for (int k = 0; k < 8; k++) {
      int d = 0;
      for (int ch = 0; ch < 4; ch++) {
        int v = ((64 - W[k]) * e0[ch] + W[k] * e1[ch] + 32) >> 6;
        int dd = c[ch] - v;
        d += dd * dd;
      }
      if (d < bestd) { bestd = d; best = k; }
    }
    w.put(uint32_t(best), i == 0 ? 2 : 3); // anchor index drops high bit
  }
}

void decodeBc7Block(const uint8_t in[16], Rgba px[16]) {
  BitReader r{in, 0};
  uint32_t mode = r.get(7);
  if (mode != (1u << 6)) {
    // Unsupported mode in this minimal decoder: emit magenta as evidence of
    // a format problem rather than silently misrendering.
    for (int i = 0; i < 16; i++) px[i] = {255, 0, 255, 255};
    return;
  }
  int q0[4], q1[4];
  for (int k = 0; k < 4; k++) { q0[k] = int(r.get(7)); q1[k] = int(r.get(7)); }
  int p0 = int(r.get(1)), p1 = int(r.get(1));
  int e0[4], e1[4];
  for (int k = 0; k < 4; k++) {
    e0[k] = (q0[k] << 1) | p0;
    e1[k] = (q1[k] << 1) | p1;
  }
  static const int W[8] = {0, 9, 18, 27, 37, 46, 55, 64};
  for (int i = 0; i < 16; i++) {
    int idx = int(r.get(i == 0 ? 2 : 3));
    int c[4];
    for (int ch = 0; ch < 4; ch++)
      c[ch] = ((64 - W[idx]) * e0[ch] + W[idx] * e1[ch] + 32) >> 6;
    px[i] = {c[0], c[1], c[2], c[3]};
  }
}

} // namespace

namespace {

const CodecInfo kCodecs[] = {
    {CodecId::BC1, "BC1", "legacy", 8},
    {CodecId::BC3, "BC3", "legacy", 16},
    {CodecId::BC7, "BC7", "new", 16},
};

uint32_t blocksX(uint32_t w) { return (w + 3) / 4; }
uint32_t blocksY(uint32_t h) { return (h + 3) / 4; }

} // namespace

std::vector<CodecInfo> allCodecs() {
  return {std::begin(kCodecs), std::end(kCodecs)};
}

const CodecInfo& codecInfo(CodecId id) {
  for (const auto& c : kCodecs)
    if (c.id == id) return c;
  return kCodecs[0];
}

CodecId codecFromName(const std::string& name, bool& ok) {
  ok = true;
  if (name == "BC1" || name == "bc1") return CodecId::BC1;
  if (name == "BC3" || name == "bc3") return CodecId::BC3;
  if (name == "BC7" || name == "bc7") return CodecId::BC7;
  ok = false;
  return CodecId::BC1;
}

std::vector<uint8_t> encodeBlocks(CodecId id, const Image& img) {
  const CodecInfo& ci = codecInfo(id);
  uint32_t bxc = blocksX(img.width), byc = blocksY(img.height);
  std::vector<uint8_t> out(size_t(bxc) * byc * ci.blockBytes, 0);
  for (uint32_t by = 0; by < byc; by++)
    for (uint32_t bx = 0; bx < bxc; bx++) {
      uint8_t* dst = &out[(size_t(by) * bxc + bx) * ci.blockBytes];
      switch (id) {
        case CodecId::BC1:
          encodeBc1Block(img, bx, by, dst);
          break;
        case CodecId::BC3:
          encodeBc3AlphaBlock(img, bx, by, dst);
          encodeBc1Block(img, bx, by, dst + 8);
          break;
        case CodecId::BC7:
          encodeBc7Block(img, bx, by, dst);
          break;
      }
    }
  return out;
}

Image decodeBlocks(CodecId id, const uint8_t* data, uint32_t w, uint32_t h) {
  const CodecInfo& ci = codecInfo(id);
  Image img(w, h);
  uint32_t bxc = blocksX(w), byc = blocksY(h);
  for (uint32_t by = 0; by < byc; by++)
    for (uint32_t bx = 0; bx < bxc; bx++) {
      const uint8_t* src = data + (size_t(by) * bxc + bx) * ci.blockBytes;
      Rgba px[16];
      switch (id) {
        case CodecId::BC1:
          decodeBc1Block(src, px);
          break;
        case CodecId::BC3: {
          decodeBc1Block(src + 8, px);
          int alpha[16];
          decodeBc3AlphaBlock(src, alpha);
          for (int i = 0; i < 16; i++) px[i].a = alpha[i];
          break;
        }
        case CodecId::BC7:
          decodeBc7Block(src, px);
          break;
      }
      for (uint32_t y = 0; y < 4; y++)
        for (uint32_t x = 0; x < 4; x++) {
          uint32_t gx = bx * 4 + x, gy = by * 4 + y;
          if (gx >= w || gy >= h) continue;
          uint8_t* d = img.px(gx, gy);
          const Rgba& s = px[y * 4 + x];
          d[0] = uint8_t(s.r);
          d[1] = uint8_t(s.g);
          d[2] = uint8_t(s.b);
          d[3] = uint8_t(s.a);
        }
    }
  return img;
}

} // namespace tcpv
