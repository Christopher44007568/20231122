#pragma once

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <string>
#include <vector>

#include "backend.hpp"

namespace nmts {

class VulkanBackend : public IBackend {
 public:
  static VulkanBackend* create(const BackendOptions& opt,
                               DegradeReason& reason,
                               std::string& detail,
                               BackendCounters& counters);
  ~VulkanBackend() override;
  std::uint32_t kind() const override { return kBackendVulkan; }
  bool verify_range(const Material& m,
                    std::uint32_t first,
                    std::uint32_t count,
                    std::vector<ResultRecord>& out,
                    std::string& err) override;

 private:
  VulkanBackend() = default;
  bool init(const BackendOptions& opt, DegradeReason& reason,
            std::string& detail, BackendCounters& counters);
  void destroy();
  bool create_storage_buffer(VkDeviceSize size, VkBuffer& buf,
                             VkDeviceMemory& mem, std::string& err);
  uint32_t find_memory_type(uint32_t type_bits,
                            VkMemoryPropertyFlags props);

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queue_family_ = 0;
  VkCommandPool cmd_pool_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout desc_layout_ = VK_NULL_HANDLE;
  VkPipelineLayout pipe_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkDescriptorPool desc_pool_ = VK_NULL_HANDLE;
  bool glfw_initialized_ = false;
  BackendOptions opt_;
  BackendCounters* counters_ = nullptr;
};

}  // namespace nmts
