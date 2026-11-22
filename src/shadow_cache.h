#pragma once

#include "mathutil.h"
#include "scene.h"
#include "vk_context.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

// Invalidation reason bits tracked per (light, cascade) cache entry.
enum InvalidationReason : uint32_t {
  ReasonNone = 0,
  ReasonFirstRender = 1u << 0,
  ReasonLightTransform = 1u << 1,
  ReasonCascadeConfig = 1u << 2,
  ReasonObjectTransform = 1u << 3,
  ReasonMaterialChange = 1u << 4,
  ReasonManual = 1u << 5,
};
const char* reasonName(uint32_t reasonBit);
static const uint32_t kAllReasonBits[] = {
    ReasonFirstRender,   ReasonLightTransform, ReasonCascadeConfig,
    ReasonObjectTransform, ReasonMaterialChange, ReasonManual};

struct CacheEntry {
  GpuImage depth;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  Mat4 viewProj = Mat4::identity();

  // Version snapshot taken at the last render.
  bool rendered = false;
  uint32_t lightTransformVersion = 0;
  uint32_t lightCascadeVersion = 0;
  std::unordered_map<uint32_t, uint32_t> casterVersions; // objectId -> ver
  std::unordered_map<int, uint32_t> materialVersions;    // matId -> ver

  // Statistics.
  uint64_t refreshCount = 0;
  double totalCpuMs = 0.0;
  double totalGpuMs = 0.0;
  double lastCpuMs = 0.0;
  double lastGpuMs = 0.0;
};

struct ReasonDetail {
  uint32_t bits = ReasonNone;
  std::vector<int> changedMaterials;    // material ids
  std::vector<uint32_t> changedObjects; // object ids
};

// Owns one depth-cache entry per (light, cascade) and refreshes them
// individually, so a single invalidated level never forces a full re-render.
class ShadowCacheSystem {
public:
  bool init(VkContext& ctx, const Scene& scene);
  void shutdown();

  size_t lightCount() const { return lightCount_; }
  size_t cascadeCount(size_t light) const { return entries_[light].size(); }
  CacheEntry& entry(size_t light, size_t c) { return entries_[light][c]; }
  const CacheEntry& entry(size_t light, size_t c) const {
    return entries_[light][c];
  }

  Mat4 cascadeViewProj(const Light& light, size_t cascade) const;
  bool objectInCascade(const Light& light, size_t cascade,
                       const SceneObject& obj) const;

  // Diffs the current scene state against the entry's snapshot.
  ReasonDetail computeReasons(const Scene& scene, size_t light,
                              size_t cascade) const;

  // Re-renders exactly one cache entry and snapshots the scene versions.
  // `extraReason` is OR-ed into the recorded reason (e.g. ReasonManual).
  bool refresh(Scene& scene, size_t light, size_t cascade,
               uint32_t extraReason = ReasonNone);

  // Copies the depth map of one entry to host memory (row-major floats).
  bool readback(size_t light, size_t cascade, std::vector<float>& out);

  uint32_t resolution(size_t light) const { return resolutions_[light]; }

private:
  VkContext* ctx_ = nullptr;
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeLayout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkQueryPool queryPool_ = VK_NULL_HANDLE;
  GpuBuffer vertexBuf_;
  GpuBuffer indexBuf_;
  size_t lightCount_ = 0;
  std::vector<std::vector<CacheEntry>> entries_;
  std::vector<uint32_t> resolutions_;

  bool createPipeline();
};
