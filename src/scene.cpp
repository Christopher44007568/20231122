#include "scene.h"

namespace rpsc {

static uint32_t packRgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24);
}

static Record makePipeline(uint32_t id, uint32_t blend, uint32_t cull,
                           uint32_t mask, uint32_t vpW, uint32_t vpH) {
  Record r;
  r.type = OpType::CreatePipeline;
  r.w[0] = id;
  r.w[1] = blend;
  r.w[2] = cull;
  r.w[3] = mask;
  r.w[4] = vpW;
  r.w[5] = vpH;
  return r;
}

static Record simple(OpType t, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0,
                     uint32_t d = 0) {
  Record r;
  r.type = t;
  r.w[0] = a;
  r.w[1] = b;
  r.w[2] = c;
  r.w[3] = d;
  return r;
}

std::vector<Record> buildScene() {
  std::vector<Record> s;
  // Pipeline cache entries: distinct blends of state so the cache has
  // something meaningful to key on.
  s.push_back(makePipeline(1, /*blend=*/0, /*cull=*/0, /*mask=*/0xF, 512, 512));
  s.push_back(makePipeline(2, /*blend=*/1, /*cull=*/0, /*mask=*/0xF, 512, 512));
  s.push_back(makePipeline(3, /*blend=*/0, /*cull=*/0, /*mask=*/0x7, 256, 256));
  s.push_back(simple(OpType::BeginFrame, 0));
  // Resource state: color image 0, UNDEFINED(0) -> COLOR_ATTACHMENT_OPTIMAL(2).
  s.push_back(simple(OpType::Barrier, 0, 0, 2));
  s.push_back(simple(OpType::BindPipeline, 1));
  s.push_back(simple(OpType::Draw, 3, 0, 1, packRgba(230, 40, 40, 255)));
  s.push_back(simple(OpType::BindPipeline, 1));  // cache hit expected
  s.push_back(simple(OpType::Draw, 3, 3, 1, packRgba(40, 220, 60, 255)));
  s.push_back(simple(OpType::BindPipeline, 2));
  s.push_back(simple(OpType::Draw, 3, 6, 1, packRgba(60, 60, 240, 128)));
  s.push_back(simple(OpType::BindPipeline, 3));
  s.push_back(simple(OpType::Draw, 3, 9, 1, packRgba(240, 220, 40, 255)));
  s.push_back(simple(OpType::BindPipeline, 2));  // cache hit expected
  s.push_back(simple(OpType::Draw, 3, 12, 1, packRgba(220, 40, 220, 160)));
  s.push_back(simple(OpType::BindPipeline, 1));  // cache hit expected
  s.push_back(simple(OpType::Draw, 3, 15, 1, packRgba(40, 220, 220, 255)));
  s.push_back(simple(OpType::EndFrame, 0));
  return s;
}

size_t sceneTruncationRecordIndex() {
  // Records: 0-2 CreatePipeline, 3 BeginFrame, 4 Barrier, 5 Bind, 6 Draw,
  // 7 Bind, 8 Draw, 9 Bind, 10 Draw, 11 Bind(P3), 12 Draw ...
  // Truncating record 11 (BindPipeline P3) removes the state change for
  // draw #4 and everything after it.
  return 11;
}

}  // namespace rpsc
