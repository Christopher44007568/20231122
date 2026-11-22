#pragma once
#include <string>
#include <vector>
#include "trace.h"
#include "vk.h"

// One recorded/replayable draw call of the reflection-probe frame.
struct DrawStep {
  std::string name;
  uint32_t pipelineId = 0;  // 0 = flat color, 1 = floor w/ reflection sample
  uint32_t meshId = 0;      // 0 = floor quad, 1 = object A, 2 = object B
  uint32_t fbTarget = 0;    // 0 = reflection probe, 1 = main framebuffer
  float tint[3] = {1, 1, 1};
  bool mirror = false;      // reflection pass renders mirrored
  uint32_t vertexCount = 0;
};

class SceneRenderer {
 public:
  void init(Gpu& g);
  void destroy();

  // The canonical frame: reflection probe pass + main pass.
  static std::vector<DrawStep> fullFrame();

  // Executes `steps` in order. If `rec` is non-null, appends draw and
  // resource-state records (record mode). Fills fbHashes with the
  // framebuffer hash after each executed draw. Returns the final main
  // framebuffer (read back after all draws).
  Image run(const std::vector<DrawStep>& steps, trace::Trace* rec,
            std::vector<uint64_t>* fbHashes);

  // Hash of everything that defines a draw's GPU state.
  static uint64_t stateHash(const DrawStep& s);

  Image renderReference();  // full frame, no tracing

 private:
  Gpu* g_ = nullptr;
  GpuImage refl_, main_;
  VkRenderPass rpClear_ = VK_NULL_HANDLE, rpLoad_ = VK_NULL_HANDLE;
  VkFramebuffer fbRefl_ = VK_NULL_HANDLE, fbMain_ = VK_NULL_HANDLE;
  VkPipelineLayout layFlat_ = VK_NULL_HANDLE, layFloor_ = VK_NULL_HANDLE;
  VkPipeline pipeFlat_ = VK_NULL_HANDLE, pipeFloor_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout dsl_ = VK_NULL_HANDLE;
  VkDescriptorPool dp_ = VK_NULL_HANDLE;
  VkDescriptorSet ds_ = VK_NULL_HANDLE;
  VkSampler sampler_ = VK_NULL_HANDLE;
  GpuBuffer meshes_[3];
  bool cleared_[2] = {false, false};

  void drawOne(const DrawStep& s);
  void ensureTargetState(const DrawStep& s, trace::Trace* rec, uint32_t& idx);
};
