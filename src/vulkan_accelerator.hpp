#pragma once

#include "compressor.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kfc {

struct VulkanProbe {
  bool available = false;
  std::string error;
  std::string deviceName;
  std::uint32_t apiVersion = 0;
  std::uint32_t driverVersion = 0;
  std::uint64_t localHeapBytes = 0;
  std::uint64_t hostVisibleHeapBytes = 0;
  std::string backend = "vulkan-compute";
};

class VulkanAccelerator final : public IntervalEvaluator {
public:
  static std::unique_ptr<VulkanAccelerator> create(std::string* error);
  ~VulkanAccelerator() override;

  IntervalError evaluate(const std::vector<PoseFrame>& frames,
                         std::size_t startIndex,
                         std::size_t endIndex) override;
  const std::string& name() const override;
  const std::string& detail() const override;

  const VulkanProbe& probe() const { return probe_; }
  static VulkanProbe probeOnly();

private:
  VulkanAccelerator() = default;
  bool initialize(std::string* error);
  void cleanup();
  bool uploadFrames(const std::vector<PoseFrame>& frames);
  bool uploadBounds(const PoseFrame& start, const PoseFrame& end);
  std::size_t gpuFrameStride() const;

  VulkanProbe probe_;
  std::string detail_;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queueFamily_ = 0;
  VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
  VkShaderModule shaderModule_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
  VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
  VkBuffer frameBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory frameMemory_ = VK_NULL_HANDLE;
  VkBuffer boundBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory boundMemory_ = VK_NULL_HANDLE;
  VkBuffer errorBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory errorMemory_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
  void* frameMapped_ = nullptr;
  void* boundMapped_ = nullptr;
  void* errorMapped_ = nullptr;
  std::size_t frameCapacity_ = 0;
  const void* uploadedFrames_ = nullptr;
  std::size_t uploadedFrameCount_ = 0;
};

}  // namespace kfc
