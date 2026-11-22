#pragma once

#include "audit_types.h"

#include <vulkan/vulkan.h>
#include <atomic>
#include <string>
#include <vector>

struct GLFWwindow;

namespace audit {

class VulkanRunner {
 public:
  VulkanRunner() = default;
  ~VulkanRunner();

  VulkanRunner(const VulkanRunner&) = delete;
  VulkanRunner& operator=(const VulkanRunner&) = delete;

  bool initialize(std::string& error);
  AuditBundle runAllScenarios();
  void shutdown();

 private:
  bool createGlfwWindow();
  bool createInstance();
  bool pickPhysicalDevice();
  bool createLogicalDevice();
  bool createRenderingArtifacts();
  ScenarioRecord runCompleted();
  ScenarioRecord runRecordingCancelled();
  ScenarioRecord runExecutionCancelled();
  ScenarioRecord runBoundaryUndecidable();
  QueryResultEvidence readQuery(uint32_t slot);
  void appendStep(ScenarioRecord& rec, const std::string& stage,
                  const std::string& detail, const std::string& ref);

  bool glfwReady_ = false;
  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  uint32_t queueFamily_ = 0;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkQueryPool queryPool_ = VK_NULL_HANDLE;
  VkImage colorImage_ = VK_NULL_HANDLE;
  VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
  VkImageView colorView_ = VK_NULL_HANDLE;
  VkRenderPass renderPass_ = VK_NULL_HANDLE;
  VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
  EnvironmentEvidence environment_;
  static constexpr uint32_t kQueryCount = 4;
};

}  // namespace audit
