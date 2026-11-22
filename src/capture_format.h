#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Primitive Topology Diagnostic Capture ("PTDC") file model.
//
// v1 (legacy): header + draw records + vertex blob + reference image hash.
// v2 (current): adds extension records, resource state timeline and the
// full reference image. v1 files are upgraded to the v2 in-memory model on
// load; fields that v1 cannot provide stay empty and are reported as
// limitations during diagnosis.

namespace ptdc {

constexpr uint32_t kMagic = 0x43445450;  // "PTDC", little-endian
constexpr uint32_t kVersionLegacy = 1;
constexpr uint32_t kVersionCurrent = 2;

struct Vertex {
  float pos[2];
  float color[3];
};

struct ExtensionRecord {
  std::string name;
  uint32_t specVersion = 0;
  bool required = false;  // capture requires the extension to replay faithfully
};

enum class ResourceType : uint32_t { Buffer = 0, Image = 1 };

// Resource states tracked across the frame (draw-index -> state at that point).
enum class ResourceState : uint32_t {
  Undefined = 0,
  TransferDst = 1,
  VertexBuffer = 2,
  ColorAttachment = 3,
  ShaderRead = 4,
};

struct ResourceStateEntry {
  uint32_t drawIndex = 0;  // state valid before this draw executes
  ResourceState state = ResourceState::Undefined;
};

struct ResourceRecord {
  uint32_t id = 0;
  ResourceType type = ResourceType::Buffer;
  uint64_t size = 0;
  std::string debugName;
  std::vector<ResourceStateEntry> timeline;
};

struct DrawCall {
  uint32_t topology = 0;  // VkPrimitiveTopology
  uint32_t firstVertex = 0;
  uint32_t vertexCount = 0;
  std::vector<uint32_t> resourceRefs;  // resource ids touched by this draw
};

struct Capture {
  uint32_t version = kVersionCurrent;
  uint32_t width = 0;
  uint32_t height = 0;
  std::vector<ExtensionRecord> extensions;
  std::vector<ResourceRecord> resources;
  std::vector<DrawCall> draws;
  std::vector<Vertex> vertices;
  std::vector<uint8_t> referenceImage;  // RGBA8, empty for upgraded v1 captures
  uint64_t referenceHash = 0;           // FNV-1a of reference image (both versions)
  std::string sourcePath;
};

// Throws std::runtime_error on malformed input.
Capture loadCapture(const std::string& path);
void saveCapture(const Capture& cap, const std::string& path);

uint64_t fnv1a64(const void* data, size_t size);
const char* topologyName(uint32_t topology);
const char* resourceStateName(ResourceState state);

}  // namespace ptdc
