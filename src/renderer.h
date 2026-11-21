#pragma once
// Vulkan + GLFW replay engine for RPSC traces.
//
// GLFW provides the windowing context (hidden window; rendering is done
// offscreen so the review flow is reproducible and CI-safe). Vulkan does
// the actual drawing. The pipeline state cache is keyed by a hash of the
// full pipeline state, mirroring how a render pipeline state cache works
// in a real engine: CreatePipeline inserts, BindPipeline looks up.
#include "image.h"
#include "trace.h"

#include <string>
#include <unordered_map>
#include <vector>

typedef struct VkInstance_T* VkInstance;
typedef struct VkPhysicalDevice_T* VkPhysicalDevice;
typedef struct VkDevice_T* VkDevice;
typedef struct VkQueue_T* VkQueue;
typedef struct VkRenderPass_T* VkRenderPass;
typedef struct VkPipeline_T* VkPipeline;
typedef struct VkPipelineLayout_T* VkPipelineLayout;
typedef struct VkBuffer_T* VkBuffer;
typedef struct VkDeviceMemory_T* VkDeviceMemory;
typedef struct VkImage_T* VkImage;
typedef struct VkImageView_T* VkImageView;
typedef struct VkFramebuffer_T* VkFramebuffer;
typedef struct VkCommandPool_T* VkCommandPool;
typedef struct VkCommandBuffer_T* VkCommandBuffer;
typedef struct VkShaderModule_T* VkShaderModule;
struct GLFWwindow;

namespace rpsc {

constexpr uint32_t kFbWidth = 512;
constexpr uint32_t kFbHeight = 512;

struct OpSnapshot {
  size_t seq = 0;            // index of the record in the executed stream
  OpType type = OpType::BeginFrame;
  uint32_t pipelineId = 0;   // bound pipeline after this op (0 = none)
  uint32_t blendEnable = 0;
  uint32_t cullMode = 0;
  uint32_t colorWriteMask = 0;
  uint32_t viewportW = 0;
  uint32_t viewportH = 0;
  uint32_t colorImageLayout = 0;  // logical layout of the color image
  uint64_t fbHash = 0;            // framebuffer hash (valid when hasFbHash)
  uint64_t stateHash = 0;         // hash of pipeline + layout state
  bool hasFbHash = false;
  bool layoutError = false;       // draw executed in a non-attachment layout
  std::string note;
};

struct ReplayResult {
  std::vector<OpSnapshot> ops;
  uint32_t cacheHits = 0;
  uint32_t cacheMisses = 0;
  uint32_t drawsExecuted = 0;
  std::vector<std::string> checkpointMismatches;  // v2 self-verification
  std::vector<std::string> warnings;
  bool glfwAvailable = false;
};

class Renderer {
 public:
  struct Impl;
  ~Renderer();
  bool init(std::string* err);
  // Executes the trace. When frameDir is non-empty, the framebuffer after
  // every draw is dumped as frameDir/draw_NN.ppm.
  bool execute(const Trace& trace, const std::string& frameDir,
               ReplayResult* out, std::string* err);
  void shutdown();

 private:
  Impl* impl_ = nullptr;
};

}  // namespace rpsc
