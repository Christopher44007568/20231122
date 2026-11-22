#pragma once
#include "math3d.h"
#include "vk_context.h"
#include <array>
#include <string>
#include <vector>

// Deterministic scene/camera/light parameters. This is the full "input"
// of one render job and the unit captured by snapshots.
struct SceneParams {
  uint32_t seed = 42;
  uint32_t width = 800;
  uint32_t height = 600;
  uint32_t shadowMapSize = 1024;
  float camYawDeg = 35.0f;
  float camPitchDeg = 25.0f;
  float camDist = 55.0f;
  float camFovDeg = 50.0f;
  float camNear = 1.0f;
  float camFar = 120.0f;
  Vec3 lightDir{0.4f, -0.8f, 0.35f};
  float splitLambda = 0.35f;
};

struct RenderJob {
  SceneParams params;
  std::array<int, 3> cascadeOrder{0, 1, 2};
  bool sceneBeforeShadows = false;
};

struct FrameUBO {
  Mat4 view;
  Mat4 proj;
  Mat4 lightVP[3];
  Vec4 splits;
  Vec4 lightDir;
  Vec4 camPos;
};

class CsmRenderer {
public:
  bool init(const VkContext& ctx, const SceneParams& params, std::string& err);
  bool record(const RenderJob& job, std::string& err);
  bool submitAndReadback(std::vector<uint8_t>& outRgba, std::string& err);
  bool render(const RenderJob& job, std::vector<uint8_t>& outRgba,
              std::string& err) {
    return record(job, err) && submitAndReadback(outRgba, err);
  }
  void destroy();
  uint32_t vertexCount() const { return vertexCount_; }

private:
  bool createGeometry(uint32_t seed, std::string& err);
  bool createRenderPasses(std::string& err);
  bool createPipelines(std::string& err);
  bool createFrameResources(const SceneParams& params, std::string& err);
  void computeLightMatrices(const SceneParams& p, Mat4 outVP[3],
                            float outSplits[3]) const;

  const VkContext* ctx_ = nullptr;
  uint32_t width_ = 0, height_ = 0, shadowSize_ = 0;
  GpuBuffer vbuf_;
  uint32_t vertexCount_ = 0;
  GpuImage shadowMaps_[3];
  GpuImage colorTarget_;
  GpuImage depthTarget_;
  GpuBuffer ubo_;
  VkRenderPass shadowPass_ = VK_NULL_HANDLE;
  VkRenderPass scenePass_ = VK_NULL_HANDLE;
  VkFramebuffer shadowFb_[3] = {VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE};
  VkFramebuffer sceneFb_ = VK_NULL_HANDLE;
  VkPipelineLayout shadowPipeLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout scenePipeLayout_ = VK_NULL_HANDLE;
  VkPipeline shadowPipe_ = VK_NULL_HANDLE;
  VkPipeline scenePipe_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout descLayout_ = VK_NULL_HANDLE;
  VkDescriptorPool descPool_ = VK_NULL_HANDLE;
  VkDescriptorSet descSet_ = VK_NULL_HANDLE;
  VkSampler shadowSampler_ = VK_NULL_HANDLE;
  VkCommandBuffer cmd_ = VK_NULL_HANDLE;
  bool initialized_ = false;
};
