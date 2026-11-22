#include "recorder.h"

#include "capture_format.h"
#include "renderer.h"
#include "vk_context.h"

#include <cstdio>
#include <stdexcept>

namespace {

constexpr uint32_t kWidth = 256;
constexpr uint32_t kHeight = 256;

ptdc::Vertex v(float x, float y, float r, float g, float b) {
  return ptdc::Vertex{{x, y}, {r, g, b}};
}

// Demo scene: four draw calls with distinct topologies and screen regions.
void buildScene(ptdc::Capture& cap) {
  cap.vertices = {
      // draw 0: TRIANGLE_LIST quad, left half, red
      v(-0.9f, -0.9f, 1, 0, 0), v(-0.1f, -0.9f, 1, 0.3f, 0), v(-0.1f, 0.9f, 1, 0, 0.2f),
      v(-0.9f, -0.9f, 1, 0, 0), v(-0.1f, 0.9f, 1, 0, 0.2f), v(-0.9f, 0.9f, 1, 0.4f, 0),
      // draw 1: TRIANGLE_STRIP quad, top right, green
      v(0.1f, 0.1f, 0, 1, 0), v(0.9f, 0.1f, 0.2f, 1, 0), v(0.1f, 0.9f, 0, 1, 0.3f),
      v(0.9f, 0.9f, 0, 0.8f, 0),
      // draw 2: TRIANGLE_FAN hexagon, bottom right, blue
      v(0.5f, -0.5f, 0, 0, 1), v(0.1f, -0.5f, 0, 0.3f, 1), v(0.3f, -0.15f, 0, 0, 1),
      v(0.7f, -0.15f, 0.3f, 0, 1), v(0.9f, -0.5f, 0, 0, 0.7f), v(0.7f, -0.85f, 0, 0.2f, 1),
      v(0.3f, -0.85f, 0, 0, 1), v(0.1f, -0.5f, 0, 0.3f, 1),
      // draw 3: TRIANGLE_LIST triangle, center, yellow
      v(-0.15f, 0.15f, 1, 1, 0), v(0.15f, 0.15f, 1, 0.8f, 0), v(0.0f, -0.15f, 0.9f, 1, 0),
  };
  cap.draws = {
      {3 /*TRIANGLE_LIST*/, 0, 6, {1, 2}},
      {4 /*TRIANGLE_STRIP*/, 6, 4, {1, 2}},
      {5 /*TRIANGLE_FAN*/, 10, 8, {1, 2}},
      {3 /*TRIANGLE_LIST*/, 18, 3, {1, 2}},
  };
}

}  // namespace

int recordCapture(const RecordOptions& opts) {
  VkContext ctx;
  Renderer renderer(ctx, kWidth, kHeight);

  ptdc::Capture cap;
  cap.version = opts.legacyFormat ? ptdc::kVersionLegacy : ptdc::kVersionCurrent;
  cap.width = kWidth;
  cap.height = kHeight;
  buildScene(cap);

  // Exactly one extension record, unknown to this replayer build: it models a
  // capture produced on a machine with a vendor extension we do not implement.
  cap.extensions.push_back({"VK_EXT_unknown_topology_probe", 1, false});

  cap.resources = {
      {1, ptdc::ResourceType::Buffer, cap.vertices.size() * sizeof(ptdc::Vertex), "scene_vb",
       {{0, ptdc::ResourceState::VertexBuffer}}},
      {2, ptdc::ResourceType::Image, uint64_t{kWidth} * kHeight * 4, "frame_color",
       {{0, ptdc::ResourceState::ColorAttachment}}},
  };

  renderer.uploadVertices(cap.vertices);
  Image ref = renderer.renderFrame(cap.draws);
  cap.referenceImage = ref.rgba;
  cap.referenceHash = ptdc::fnv1a64(ref.rgba.data(), ref.rgba.size());

  if (opts.corruptDraw >= 0) {
    if (static_cast<size_t>(opts.corruptDraw) >= cap.draws.size())
      throw std::runtime_error("corrupt draw index out of range");
    cap.draws[opts.corruptDraw].topology = opts.corruptTopology;
    std::fprintf(stderr,
                 "recorder: simulating recovery corruption: draw %d topology rewritten to %s\n",
                 opts.corruptDraw, ptdc::topologyName(opts.corruptTopology));
  }

  ptdc::saveCapture(cap, opts.outPath);
  std::printf("recorded %s capture: %s (%zu draws, %zu vertices, device: %s)\n",
              cap.version == ptdc::kVersionLegacy ? "legacy v1" : "v2", opts.outPath.c_str(),
              cap.draws.size(), cap.vertices.size(), ctx.deviceName().c_str());
  return 0;
}
