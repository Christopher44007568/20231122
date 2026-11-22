#include "renderer.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>

namespace {

bool checkVk(VkResult result, const char* operation, std::string& error) {
  if (result == VK_SUCCESS) {
    return true;
  }
  error = std::string(operation) + " failed with VkResult " +
          std::to_string(static_cast<int>(result));
  return false;
}

}  // namespace

Renderer::~Renderer() { shutdown(); }

bool Renderer::initialize(int width, int height, bool headless,
                          GLFWwindow* window,
                          const std::filesystem::path& shaderDirectory,
                          std::string& error) {
  width_ = width;
  height_ = height;
  headless_ = headless;
  window_ = window;
  shaderDirectory_ = shaderDirectory;
  if (!createInstance(headless, error) ||
      !createSurface(window, error) ||
      !pickPhysicalDevice(error) ||
      !createDevice(error) ||
      !createCommandResources(error) ||
      !createSceneRenderPass(error) ||
      !createOffscreenResources(error) ||
      !createScenePipeline(error) ||
      !createQuadBuffer(error) ||
      !createStagingBuffer(error)) {
    return false;
  }
  if (!headless_) {
    if (!createSwapchain(error) ||
        !createSwapRenderPass(error) ||
        !createCompositeResources(error) ||
        !createUiPipeline(error) ||
        !createUiBuffer(error)) {
      return false;
    }
  }
  initialized_ = true;
  return true;
}

bool Renderer::createInstance(bool headless, std::string& error) {
  VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  appInfo.pApplicationName = "screen-depth-recheck";
  appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.pEngineName = "depth-recheck";
  appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
  appInfo.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> extensions;
  if (!headless) {
    std::uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (required == nullptr) {
      error = "GLFW did not expose Vulkan instance extensions";
      return false;
    }
    extensions.assign(required, required + count);
  }

  VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  createInfo.pApplicationInfo = &appInfo;
  createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
  return checkVk(vkCreateInstance(&createInfo, nullptr, &instance_),
                 "vkCreateInstance", error);
}

bool Renderer::createSurface(GLFWwindow* window, std::string& error) {
  if (headless_) {
    return true;
  }
  if (window == nullptr) {
    error = "GLFW window is required for windowed mode";
    return false;
  }
  return checkVk(glfwCreateWindowSurface(instance_, window, nullptr, &surface_),
                 "glfwCreateWindowSurface", error);
}

bool Renderer::pickPhysicalDevice(std::string& error) {
  std::uint32_t deviceCount = 0;
  if (!checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr),
               "vkEnumeratePhysicalDevices", error) || deviceCount == 0) {
    if (error.empty()) {
      error = "No Vulkan physical device found";
    }
    return false;
  }
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data());

  int bestScore = -1;
  for (VkPhysicalDevice candidate : devices) {
    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
    for (std::uint32_t family = 0; family < familyCount; ++family) {
      if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
        continue;
      }
      VkBool32 presentSupport = VK_TRUE;
      if (!headless_) {
        vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface_,
                                            &presentSupport);
      }
      if (!presentSupport) {
        continue;
      }
      VkPhysicalDeviceProperties properties{};
      vkGetPhysicalDeviceProperties(candidate, &properties);
      int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU
                    ? 1000 : 0;
      score += static_cast<int>(properties.limits.maxImageDimension2D / 4096);
      if (score > bestScore) {
        bestScore = score;
        physicalDevice_ = candidate;
        queueFamily_ = family;
      }
    }
  }
  if (physicalDevice_ == VK_NULL_HANDLE) {
    error = "No Vulkan graphics/present queue found";
    return false;
  }
  return true;
}

bool Renderer::createDevice(std::string& error) {
  const float priority = 1.0f;
  VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queueInfo.queueFamilyIndex = queueFamily_;
  queueInfo.queueCount = 1;
  queueInfo.pQueuePriorities = &priority;

  std::vector<const char*> extensions;
  if (!headless_) {
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
  VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  createInfo.queueCreateInfoCount = 1;
  createInfo.pQueueCreateInfos = &queueInfo;
  createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.empty() ? nullptr : extensions.data();
  if (!checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
               "vkCreateDevice", error)) {
    return false;
  }
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);
  return true;
}

bool Renderer::createCommandResources(std::string& error) {
  VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamily_;
  if (!checkVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
               "vkCreateCommandPool", error)) {
    return false;
  }
  VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  return checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr,
                                   &imageAvailable_),
                 "vkCreateSemaphore", error);
}

std::uint32_t Renderer::findMemoryType(
  std::uint32_t mask, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
  for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
    if ((mask & (1u << index)) != 0 &&
        (memory.memoryTypes[index].propertyFlags & properties) == properties) {
      return index;
    }
  }
  return UINT32_MAX;
}

bool Renderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties, VkBuffer& buffer,
                            VkDeviceMemory& memory, std::string& error) {
  VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer),
               "vkCreateBuffer", error)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, buffer, &requirements);
  const std::uint32_t memoryType =
    findMemoryType(requirements.memoryTypeBits, properties);
  if (memoryType == UINT32_MAX) {
    error = "No suitable Vulkan buffer memory type";
    return false;
  }
  VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = memoryType;
  if (!checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &memory),
               "vkAllocateMemory(buffer)", error)) {
    return false;
  }
  return checkVk(vkBindBufferMemory(device_, buffer, memory, 0),
                 "vkBindBufferMemory", error);
}

bool Renderer::createImage(std::uint32_t width, std::uint32_t height,
                           VkFormat format, VkImageUsageFlags usage,
                           ImageResource& image, std::string& error,
                           VkImageAspectFlags aspect) {
  image.format = format;
  VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.format = format;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  imageInfo.usage = usage;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!checkVk(vkCreateImage(device_, &imageInfo, nullptr, &image.image),
               "vkCreateImage", error)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image.image, &requirements);
  const std::uint32_t memoryType =
    findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memoryType == UINT32_MAX) {
    error = "No suitable Vulkan image memory type";
    return false;
  }
  VkMemoryAllocateInfo allocateInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = memoryType;
  if (!checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &image.memory),
               "vkAllocateMemory(image)", error)) {
    return false;
  }
  if (!checkVk(vkBindImageMemory(device_, image.image, image.memory, 0),
               "vkBindImageMemory", error)) {
    return false;
  }
  VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = image.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspect;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;
  return checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &image.view),
                 "vkCreateImageView", error);
}

VkCommandBuffer Renderer::beginSingleTimeCommands() const {
  VkCommandBufferAllocateInfo allocateInfo{
    VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  allocateInfo.commandPool = commandPool_;
  allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocateInfo.commandBufferCount = 1;
  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

bool Renderer::endSingleTimeCommands(VkCommandBuffer commandBuffer,
                                     std::string& error) {
  if (commandBuffer == VK_NULL_HANDLE) {
    error = "Cannot allocate one-time command buffer";
    return false;
  }
  if (!checkVk(vkEndCommandBuffer(commandBuffer),
               "vkEndCommandBuffer", error)) {
    return false;
  }
  VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  if (!checkVk(vkQueueSubmit(queue_, 1, &submitInfo, VK_NULL_HANDLE),
               "vkQueueSubmit", error)) {
    return false;
  }
  if (!checkVk(vkQueueWaitIdle(queue_), "vkQueueWaitIdle", error)) {
    return false;
  }
  vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
  return true;
}

VkShaderModule Renderer::loadShader(const std::string& name,
                                    std::string& error) const {
  const std::filesystem::path path = shaderDirectory_ / name;
  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file) {
    error = "Missing shader: " + path.string();
    return VK_NULL_HANDLE;
  }
  const std::streamsize size = file.tellg();
  if (size <= 0 || size % 4 != 0) {
    error = "Invalid SPIR-V shader: " + path.string();
    return VK_NULL_HANDLE;
  }
  std::vector<char> code(static_cast<std::size_t>(size));
  file.seekg(0);
  file.read(code.data(), size);
  VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  createInfo.codeSize = code.size();
  createInfo.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  if (!checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
               "vkCreateShaderModule", error)) {
    return VK_NULL_HANDLE;
  }
  return module;
}

void Renderer::destroySwapchain() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  for (VkFramebuffer framebuffer : swapchainFramebuffers_) {
    vkDestroyFramebuffer(device_, framebuffer, nullptr);
  }
  swapchainFramebuffers_.clear();
  for (VkImageView view : swapchainViews_) {
    vkDestroyImageView(device_, view, nullptr);
  }
  swapchainViews_.clear();
  swapchainImages_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

void Renderer::destroyOffscreenResources() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  const auto destroyTarget = [&](SceneTarget& target) {
    if (target.framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, target.framebuffer, nullptr);
      target.framebuffer = VK_NULL_HANDLE;
    }
    for (ImageResource* image : {&target.color, &target.aux}) {
      if (image->view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, image->view, nullptr);
      }
      if (image->image != VK_NULL_HANDLE) {
        vkDestroyImage(device_, image->image, nullptr);
      }
      if (image->memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, image->memory, nullptr);
      }
      *image = {};
    }
  };
  destroyTarget(baselineTarget_);
  destroyTarget(currentTarget_);
  if (depthResource_.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device_, depthResource_.view, nullptr);
  }
  if (depthResource_.image != VK_NULL_HANDLE) {
    vkDestroyImage(device_, depthResource_.image, nullptr);
  }
  if (depthResource_.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, depthResource_.memory, nullptr);
  }
  depthResource_ = {};
}

void Renderer::shutdown() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  destroySwapchain();
  destroyOffscreenResources();
  if (device_ != VK_NULL_HANDLE) {
    if (sceneBaselinePipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, sceneBaselinePipeline_, nullptr);
    }
    if (sceneScenarioPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, sceneScenarioPipeline_, nullptr);
    }
    if (compositePipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, compositePipeline_, nullptr);
    }
    if (uiPipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, uiPipeline_, nullptr);
    }
    if (scenePipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, scenePipelineLayout_, nullptr);
    }
    if (compositePipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, compositePipelineLayout_, nullptr);
    }
    if (uiPipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, uiPipelineLayout_, nullptr);
    }
    if (compositeSetLayout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, compositeSetLayout_, nullptr);
    }
    if (colorSampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, colorSampler_, nullptr);
    }
    if (auxSampler_ != VK_NULL_HANDLE) {
      vkDestroySampler(device_, auxSampler_, nullptr);
    }
    if (descriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (quadBuffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, quadBuffer_, nullptr);
    }
    if (quadMemory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, quadMemory_, nullptr);
    }
    if (stagingBuffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, stagingBuffer_, nullptr);
    }
    if (stagingMemory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, stagingMemory_, nullptr);
    }
    if (uiBuffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, uiBuffer_, nullptr);
    }
    if (uiMemory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, uiMemory_, nullptr);
    }
    if (imageAvailable_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, imageAvailable_, nullptr);
    }
    if (sceneRenderPass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, sceneRenderPass_, nullptr);
    }
    if (swapRenderPass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, swapRenderPass_, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
  instance_ = VK_NULL_HANDLE;
  physicalDevice_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  queue_ = VK_NULL_HANDLE;
  surface_ = VK_NULL_HANDLE;
  commandPool_ = VK_NULL_HANDLE;
  descriptorPool_ = VK_NULL_HANDLE;
  swapchain_ = VK_NULL_HANDLE;
  sceneRenderPass_ = VK_NULL_HANDLE;
  swapRenderPass_ = VK_NULL_HANDLE;
  scenePipelineLayout_ = VK_NULL_HANDLE;
  sceneBaselinePipeline_ = VK_NULL_HANDLE;
  sceneScenarioPipeline_ = VK_NULL_HANDLE;
  compositeSetLayout_ = VK_NULL_HANDLE;
  compositePipelineLayout_ = VK_NULL_HANDLE;
  compositePipeline_ = VK_NULL_HANDLE;
  uiPipelineLayout_ = VK_NULL_HANDLE;
  uiPipeline_ = VK_NULL_HANDLE;
  colorSampler_ = VK_NULL_HANDLE;
  auxSampler_ = VK_NULL_HANDLE;
  quadBuffer_ = VK_NULL_HANDLE;
  quadMemory_ = VK_NULL_HANDLE;
  stagingBuffer_ = VK_NULL_HANDLE;
  stagingMemory_ = VK_NULL_HANDLE;
  uiBuffer_ = VK_NULL_HANDLE;
  uiMemory_ = VK_NULL_HANDLE;
  imageAvailable_ = VK_NULL_HANDLE;
  initialized_ = false;
}



