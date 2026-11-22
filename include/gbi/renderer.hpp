#pragma once

#include "gbi/evidence.hpp"
#include "gbi/scene.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct GLFWwindow;
struct VkInstance_T;
struct VkDevice_T;
struct VkPhysicalDevice_T;
struct VkQueue_T;
struct VkCommandPool_T;
struct VkCommandBuffer_T;
struct VkDescriptorPool_T;
struct VkDescriptorSetLayout_T;
struct VkDescriptorSet_T;
struct VkPipelineLayout_T;
struct VkPipeline_T;
struct VkRenderPass_T;
struct VkFramebuffer_T;
struct VkImage_T;
struct VkImageView_T;
struct VkDeviceMemory_T;
struct VkSampler_T;
struct VkBuffer_T;
struct VkFence_T;
struct VkSemaphore_T;
struct VkSurfaceKHR_T;
struct VkSwapchainKHR_T;
struct VkShaderModule_T;

namespace gbi {

struct DeviceIdentity {
  std::string name;
  std::string driver;
  std::uint32_t api_version = 0;
};

class VulkanRenderer {
 public:
  VulkanRenderer(std::uint32_t width, std::uint32_t height,
                 GLFWwindow* window = nullptr);
  ~VulkanRenderer();

  VulkanRenderer(const VulkanRenderer&) = delete;
  VulkanRenderer& operator=(const VulkanRenderer&) = delete;

  const DeviceIdentity& device() const;
  std::uint32_t width() const;
  std::uint32_t height() const;
  bool hasWindow() const;

  RenderedView render(const Scene& scene,
                      const std::vector<std::string>& visible_objects,
                      const std::string& view_id, const std::string& kind,
                      const std::string& object_id,
                      std::uint32_t object_index,
                      std::uint64_t frame_index);

  void present();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace gbi
