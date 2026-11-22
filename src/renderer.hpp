#pragma once

#include "compare.hpp"
#include "params.hpp"
#include "ui.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

struct GLFWwindow;

class Renderer {
 public:
  Renderer() = default;
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  bool initialize(int width, int height, bool headless, GLFWwindow* window,
                  const std::filesystem::path& shaderDirectory,
                  std::string& error);
  void shutdown();

  bool renderBaseline(const DepthParams& params, std::string& error);
  bool renderScenario(const DepthParams& params, std::uint32_t focusId,
                      const BiasCandidate& focusBias, FrameData& output,
                      double& elapsedMs, std::string& error);
  bool present(int mode, std::uint32_t focusId, float split,
               const UiFrame& ui, std::string& error);

  const FrameData& baselineFrame() const { return baselineFrame_; }
  const FrameData& currentFrame() const { return currentFrame_; }
  const FrameData& diffFrame() const { return diffFrame_; }
  std::uint32_t pickObject(int x, int y) const;
  bool headless() const { return headless_; }

 private:
  struct ImageResource {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
  };

  struct SceneTarget {
    ImageResource color;
    ImageResource aux;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
  };

  bool createInstance(bool headless, std::string& error);
  bool createSurface(GLFWwindow* window, std::string& error);
  bool pickPhysicalDevice(std::string& error);
  bool createDevice(std::string& error);
  bool createSwapchain(std::string& error);
  void destroySwapchain();
  bool createCommandResources(std::string& error);
  bool createOffscreenResources(std::string& error);
  void destroyOffscreenResources();
  bool createSceneRenderPass(std::string& error);
  bool createSwapRenderPass(std::string& error);
  bool createScenePipeline(std::string& error);
  bool createCompositeResources(std::string& error);
  bool createUiPipeline(std::string& error);
  bool createQuadBuffer(std::string& error);
  bool createStagingBuffer(std::string& error);
  bool createUiBuffer(std::string& error);

  bool createImage(std::uint32_t width, std::uint32_t height, VkFormat format,
                   VkImageUsageFlags usage, ImageResource& image,
                   std::string& error,
                   VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT);
  bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags properties, VkBuffer& buffer,
                    VkDeviceMemory& memory, std::string& error);
  std::uint32_t findMemoryType(std::uint32_t mask,
                               VkMemoryPropertyFlags properties) const;
  VkShaderModule loadShader(const std::string& name, std::string& error) const;
  VkCommandBuffer beginSingleTimeCommands() const;
  bool endSingleTimeCommands(VkCommandBuffer commandBuffer, std::string& error);

  bool renderTarget(const SceneTarget& target, const DepthParams& params,
                    bool baselinePass, std::uint32_t focusId,
                    const BiasCandidate& focusBias, std::string& error);
  bool readbackTarget(const SceneTarget& target, FrameData& frame,
                      std::string& error);
  bool uploadUi(const UiFrame& ui, std::string& error);

  VkInstance instance_ = VK_NULL_HANDLE;
  VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue queue_ = VK_NULL_HANDLE;
  std::uint32_t queueFamily_ = 0;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkCommandPool commandPool_ = VK_NULL_HANDLE;
  VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;

  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchainExtent_{};
  std::vector<VkImage> swapchainImages_;
  std::vector<VkImageView> swapchainViews_;
  std::vector<VkFramebuffer> swapchainFramebuffers_;
  VkRenderPass swapRenderPass_ = VK_NULL_HANDLE;
  VkSemaphore imageAvailable_ = VK_NULL_HANDLE;

  VkRenderPass sceneRenderPass_ = VK_NULL_HANDLE;
  VkPipelineLayout scenePipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline sceneBaselinePipeline_ = VK_NULL_HANDLE;
  VkPipeline sceneScenarioPipeline_ = VK_NULL_HANDLE;
  VkDescriptorSetLayout compositeSetLayout_ = VK_NULL_HANDLE;
  VkDescriptorSet compositeSet_ = VK_NULL_HANDLE;
  VkPipelineLayout compositePipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline compositePipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout uiPipelineLayout_ = VK_NULL_HANDLE;
  VkPipeline uiPipeline_ = VK_NULL_HANDLE;
  VkSampler colorSampler_ = VK_NULL_HANDLE;
  VkSampler auxSampler_ = VK_NULL_HANDLE;

  SceneTarget baselineTarget_;
  SceneTarget currentTarget_;
  ImageResource depthResource_;
  VkBuffer quadBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory quadMemory_ = VK_NULL_HANDLE;
  VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
  VkDeviceSize stagingSize_ = 0;
  VkBuffer uiBuffer_ = VK_NULL_HANDLE;
  VkDeviceMemory uiMemory_ = VK_NULL_HANDLE;
  VkDeviceSize uiBufferSize_ = 0;

  int width_ = 0;
  int height_ = 0;
  bool headless_ = false;
  bool initialized_ = false;
  GLFWwindow* window_ = nullptr;
  std::filesystem::path shaderDirectory_;
  FrameData baselineFrame_;
  FrameData currentFrame_;
  FrameData diffFrame_;
};

