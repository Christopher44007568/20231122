#include "gbi/renderer.hpp"

#include "gbi/util.hpp"

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <vector>

namespace gbi {

namespace {

constexpr std::uint32_t kMaxPushSize = 128;
constexpr std::uint64_t kReadbackAlignment = 256;

[[noreturn]] void failVk(VkResult result, const char* operation) {
  throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                           std::to_string(static_cast<int>(result)));
}

void checkVk(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    failVk(result, operation);
  }
}

std::uint64_t alignUp(std::uint64_t value, std::uint64_t alignment) {
  return (value + alignment - 1) / alignment * alignment;
}

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
  void* mapped = nullptr;
};

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  VkDeviceSize allocation_size = 0;
  VkDeviceSize bytes_per_pixel = 0;
  std::string name;
};

struct PushConstants {
  float view_projection[16];
  float albedo[4];
  float params[4];
};

struct DrawRange {
  std::uint32_t first_vertex = 0;
  std::uint32_t vertex_count = 0;
  std::size_t object_index = 0;
};

Bytes loadSpirv(const std::string& stem) {
  std::vector<fs::path> directories;
  if (const char* configured = std::getenv("GBI_SHADER_DIR")) {
    directories.emplace_back(configured);
  }
  directories.emplace_back(GBI_SHADER_DIR);
  directories.push_back(fs::current_path() / "shaders");
  std::error_code error;
  const fs::path executable = fs::read_symlink("/proc/self/exe", error);
  if (!error) {
    const fs::path directory = executable.parent_path();
    directories.push_back(directory / "shaders");
    directories.push_back(directory / ".." / "share" / "gbuffer-inspector" /
                          "shaders");
  }
  directories.emplace_back("/usr/local/share/gbuffer-inspector/shaders");
  directories.emplace_back("/usr/share/gbuffer-inspector/shaders");
  for (const fs::path& directory : directories) {
    const fs::path path = directory / (stem + ".spv");
    if (fs::is_regular_file(path, error)) {
      return readFile(path);
    }
  }
  throw std::runtime_error("cannot locate shader " + stem + ".spv");
}

VkShaderModule createShaderModule(VkDevice device, const Bytes& bytes) {
  if (bytes.empty() || bytes.size() % sizeof(std::uint32_t) != 0) {
    throw std::runtime_error("invalid SPIR-V file");
  }
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = bytes.size();
  info.pCode = reinterpret_cast<const std::uint32_t*>(bytes.data());
  VkShaderModule module = VK_NULL_HANDLE;
  checkVk(vkCreateShaderModule(device, &info, nullptr, &module),
          "vkCreateShaderModule");
  return module;
}

VkVertexInputBindingDescription vertexBinding() {
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return binding;
}

std::array<VkVertexInputAttributeDescription, 2> vertexAttributes() {
  std::array<VkVertexInputAttributeDescription, 2> attributes{};
  attributes[0].location = 0;
  attributes[0].binding = 0;
  attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset = offsetof(Vertex, position);
  attributes[1].location = 1;
  attributes[1].binding = 0;
  attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[1].offset = offsetof(Vertex, normal);
  return attributes;
}

VkPipelineShaderStageCreateInfo shaderStage(VkShaderModule module,
                                            VkShaderStageFlagBits stage) {
  VkPipelineShaderStageCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  info.stage = stage;
  info.module = module;
  info.pName = "main";
  return info;
}

}  // namespace

struct VulkanRenderer::Impl {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  GLFWwindow* window = nullptr;
  DeviceIdentity identity;

  VkInstance instance = VK_NULL_HANDLE;
  VkSurfaceKHR surface = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  std::uint32_t graphics_family = 0;
  std::uint32_t present_family = 0;
  bool dedicated_present_family = false;
  VkQueue graphics_queue = VK_NULL_HANDLE;
  VkQueue present_queue = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkFence render_fence = VK_NULL_HANDLE;

  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkPipelineLayout gbuffer_pipeline_layout = VK_NULL_HANDLE;
  VkPipelineLayout final_pipeline_layout = VK_NULL_HANDLE;
  VkPipeline gbuffer_pipeline = VK_NULL_HANDLE;
  VkPipeline final_pipeline = VK_NULL_HANDLE;
  VkRenderPass gbuffer_pass = VK_NULL_HANDLE;
  VkRenderPass final_pass = VK_NULL_HANDLE;
  VkFramebuffer gbuffer_framebuffer = VK_NULL_HANDLE;
  VkFramebuffer final_framebuffer = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;

  Image albedo;
  Image normal;
  Image position;
  Image object_id;
  Image depth;
  Image final_image;

  Buffer vertex_buffer;
  Buffer readback_buffer;
  std::vector<DrawRange> draw_ranges;
  std::vector<Vertex> frame_vertices;

  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  VkFormat swapchain_format = VK_FORMAT_UNDEFINED;
  VkExtent2D swapchain_extent{};
  std::vector<VkImage> swapchain_images;
  std::vector<VkImageView> swapchain_views;
  VkSemaphore image_available = VK_NULL_HANDLE;
  VkSemaphore render_finished = VK_NULL_HANDLE;
  VkFence present_fence = VK_NULL_HANDLE;

  bool rendered_once = false;

  Impl(std::uint32_t requested_width, std::uint32_t requested_height,
       GLFWwindow* requested_window);
  ~Impl();

  void createInstance();
  void createSurface();
  void selectPhysicalDevice();
  void createDevice();
  void createCommandPool();
  void createImages();
  void createRenderPasses();
  void createDescriptors();
  void createPipelines();
  void createFramebuffers();
  void createFences();
  void createSwapchain();

  void destroySwapchain();
  void destroyImage(Image& image);
  void destroyBuffer(Buffer& buffer);
  void destroy();

  Image createImageObject(const std::string& name, VkFormat format,
                          VkImageUsageFlags usage, VkImageAspectFlags aspect,
                          std::uint32_t image_width,
                          std::uint32_t image_height,
                          std::uint32_t bytes_per_pixel);
  Buffer createBufferObject(VkDeviceSize size, VkBufferUsageFlags usage,
                            VkMemoryPropertyFlags properties,
                            bool persistent_map);
  std::uint32_t findMemoryType(std::uint32_t type_bits,
                               VkMemoryPropertyFlags properties) const;

  std::vector<DrawRange> uploadScene(const Scene& scene,
                                     const std::vector<std::string>& visible);
  void renderAttachments(const Scene& scene, const std::vector<DrawRange>& draws,
                         const Mat4& view_projection);
  RenderedView readback(const Scene& scene, const std::string& view_id,
                        const std::string& kind,
                        const std::string& object_identifier,
                        std::uint32_t object_index,
                        std::uint64_t frame_index);
  void waitIdle() const;
  void present();
};

std::uint32_t VulkanRenderer::Impl::findMemoryType(
    std::uint32_t type_bits, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory_properties{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memory_properties);
  for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount;
       ++index) {
    if ((type_bits & (1U << index)) != 0U &&
        (memory_properties.memoryTypes[index].propertyFlags & properties) ==
            properties) {
      return index;
    }
  }
  throw std::runtime_error("no compatible Vulkan memory type found");
}

Buffer VulkanRenderer::Impl::createBufferObject(
    VkDeviceSize size, VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties, bool persistent_map) {
  Buffer result;
  result.size = size;
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  checkVk(vkCreateBuffer(device, &buffer_info, nullptr, &result.buffer),
          "vkCreateBuffer");

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits, properties);
  checkVk(vkAllocateMemory(device, &allocation, nullptr, &result.memory),
          "vkAllocateMemory");
  checkVk(vkBindBufferMemory(device, result.buffer, result.memory, 0),
          "vkBindBufferMemory");
  if (persistent_map) {
    checkVk(vkMapMemory(device, result.memory, 0, size, 0, &result.mapped),
            "vkMapMemory");
  }
  return result;
}

void VulkanRenderer::Impl::destroyBuffer(Buffer& buffer) {
  if (buffer.mapped != nullptr) {
    vkUnmapMemory(device, buffer.memory);
    buffer.mapped = nullptr;
  }
  if (buffer.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device, buffer.buffer, nullptr);
    buffer.buffer = VK_NULL_HANDLE;
  }
  if (buffer.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
  }
  buffer.size = 0;
}

void VulkanRenderer::Impl::destroyImage(Image& image) {
  if (image.view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, image.view, nullptr);
    image.view = VK_NULL_HANDLE;
  }
  if (image.image != VK_NULL_HANDLE) {
    vkDestroyImage(device, image.image, nullptr);
    image.image = VK_NULL_HANDLE;
  }
  if (image.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, image.memory, nullptr);
    image.memory = VK_NULL_HANDLE;
  }
}

VulkanRenderer::Impl::Impl(std::uint32_t requested_width,
                           std::uint32_t requested_height,
                           GLFWwindow* requested_window)
    : width(requested_width), height(requested_height), window(requested_window) {
  if (width == 0 || height == 0) {
    throw std::runtime_error("render dimensions must be non-zero");
  }
  try {
    createInstance();
    createSurface();
    selectPhysicalDevice();
    createDevice();
    createCommandPool();
    createImages();
    createRenderPasses();
    createDescriptors();
    createPipelines();
    createFramebuffers();
    createFences();
    if (window != nullptr) {
      createSwapchain();
    }
  } catch (...) {
    destroy();
    throw;
  }
}

VulkanRenderer::Impl::~Impl() {
  destroy();
}

void VulkanRenderer::Impl::waitIdle() const {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
}

void VulkanRenderer::Impl::destroy() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
  destroySwapchain();
  destroyBuffer(vertex_buffer);
  destroyBuffer(readback_buffer);
  if (device != VK_NULL_HANDLE) {
    if (render_fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, render_fence, nullptr);
      render_fence = VK_NULL_HANDLE;
    }
    if (present_fence != VK_NULL_HANDLE) {
      vkDestroyFence(device, present_fence, nullptr);
      present_fence = VK_NULL_HANDLE;
    }
    if (image_available != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, image_available, nullptr);
      image_available = VK_NULL_HANDLE;
    }
    if (render_finished != VK_NULL_HANDLE) {
      vkDestroySemaphore(device, render_finished, nullptr);
      render_finished = VK_NULL_HANDLE;
    }
    if (sampler != VK_NULL_HANDLE) {
      vkDestroySampler(device, sampler, nullptr);
      sampler = VK_NULL_HANDLE;
    }
    if (gbuffer_framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device, gbuffer_framebuffer, nullptr);
      gbuffer_framebuffer = VK_NULL_HANDLE;
    }
    if (final_framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device, final_framebuffer, nullptr);
      final_framebuffer = VK_NULL_HANDLE;
    }
    destroyImage(albedo);
    destroyImage(normal);
    destroyImage(position);
    destroyImage(object_id);
    destroyImage(depth);
    destroyImage(final_image);
    if (gbuffer_pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, gbuffer_pipeline, nullptr);
      gbuffer_pipeline = VK_NULL_HANDLE;
    }
    if (final_pipeline != VK_NULL_HANDLE) {
      vkDestroyPipeline(device, final_pipeline, nullptr);
      final_pipeline = VK_NULL_HANDLE;
    }
    if (gbuffer_pipeline_layout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, gbuffer_pipeline_layout, nullptr);
      gbuffer_pipeline_layout = VK_NULL_HANDLE;
    }
    if (final_pipeline_layout != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device, final_pipeline_layout, nullptr);
      final_pipeline_layout = VK_NULL_HANDLE;
    }
    if (gbuffer_pass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device, gbuffer_pass, nullptr);
      gbuffer_pass = VK_NULL_HANDLE;
    }
    if (final_pass != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device, final_pass, nullptr);
      final_pass = VK_NULL_HANDLE;
    }
    if (descriptor_pool != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
      descriptor_pool = VK_NULL_HANDLE;
    }
    if (descriptor_layout != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
      descriptor_layout = VK_NULL_HANDLE;
    }
    if (command_pool != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device, command_pool, nullptr);
      command_pool = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  if (surface != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance, surface, nullptr);
    surface = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
}

void VulkanRenderer::Impl::createInstance() {
  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "gbuffer-inspector";
  application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  application.pEngineName = "gbi-offscreen";
  application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  application.apiVersion = VK_API_VERSION_1_1;

  std::vector<const char*> extensions;
  if (window != nullptr) {
    std::uint32_t count = 0;
    const char** required = glfwGetRequiredInstanceExtensions(&count);
    if (required == nullptr || count == 0) {
      throw std::runtime_error(
          "GLFW did not provide Vulkan surface extensions");
    }
    extensions.assign(required, required + count);
  }

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &application;
  create_info.enabledExtensionCount =
      static_cast<std::uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames =
      extensions.empty() ? nullptr : extensions.data();
  checkVk(vkCreateInstance(&create_info, nullptr, &instance),
          "vkCreateInstance");
}

void VulkanRenderer::Impl::createSurface() {
  if (window == nullptr) {
    return;
  }
  checkVk(glfwCreateWindowSurface(instance, window, nullptr, &surface),
          "glfwCreateWindowSurface");
}

void VulkanRenderer::Impl::selectPhysicalDevice() {
  std::uint32_t device_count = 0;
  checkVk(vkEnumeratePhysicalDevices(instance, &device_count, nullptr),
          "vkEnumeratePhysicalDevices");
  if (device_count == 0) {
    throw std::runtime_error("no Vulkan physical device is available");
  }
  std::vector<VkPhysicalDevice> candidates(device_count);
  checkVk(vkEnumeratePhysicalDevices(instance, &device_count,
                                     candidates.data()),
          "vkEnumeratePhysicalDevices");

  int best_score = -1;
  for (VkPhysicalDevice candidate : candidates) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_1) {
      continue;
    }
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                             families.data());
    int graphics = -1;
    int present = -1;
    for (std::uint32_t index = 0; index < family_count; ++index) {
      if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U &&
          graphics < 0) {
        graphics = static_cast<int>(index);
      }
      if (window != nullptr) {
        VkBool32 supported = VK_FALSE;
        checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                    candidate, index, surface, &supported),
                "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (supported == VK_TRUE) {
          present = static_cast<int>(index);
          break;
        }
      }
    }
    if (graphics < 0 || (window != nullptr && present < 0)) {
      continue;
    }
    int score = 100;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score = 1000;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score = 700;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
      score = 300;
    }
    if (score > best_score) {
      best_score = score;
      physical = candidate;
      graphics_family = static_cast<std::uint32_t>(graphics);
      present_family = window == nullptr
                           ? static_cast<std::uint32_t>(graphics)
                           : static_cast<std::uint32_t>(present);
      identity.name = properties.deviceName;
      identity.api_version = properties.apiVersion;
      identity.driver =
          "vendor=" + std::to_string(properties.vendorID) +
          " device=" + std::to_string(properties.deviceID) +
          " driver_version=" +
          std::to_string(VK_VERSION_MAJOR(properties.driverVersion)) + "." +
          std::to_string(VK_VERSION_MINOR(properties.driverVersion)) + "." +
          std::to_string(VK_VERSION_PATCH(properties.driverVersion));
    }
  }
  if (physical == VK_NULL_HANDLE) {
    throw std::runtime_error(
        "no Vulkan device has both graphics and presentation support");
  }
  dedicated_present_family = present_family != graphics_family;
}

void VulkanRenderer::Impl::createDevice() {
  const float priority = 1.0F;
  std::vector<VkDeviceQueueCreateInfo> queue_infos;
  VkDeviceQueueCreateInfo graphics_info{};
  graphics_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  graphics_info.queueFamilyIndex = graphics_family;
  graphics_info.queueCount = 1;
  graphics_info.pQueuePriorities = &priority;
  queue_infos.push_back(graphics_info);
  if (dedicated_present_family) {
    VkDeviceQueueCreateInfo present_info = graphics_info;
    present_info.queueFamilyIndex = present_family;
    queue_infos.push_back(present_info);
  }

  std::vector<const char*> extensions;
  if (window != nullptr) {
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
  }
  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.queueCreateInfoCount =
      static_cast<std::uint32_t>(queue_infos.size());
  create_info.pQueueCreateInfos = queue_infos.data();
  create_info.enabledExtensionCount =
      static_cast<std::uint32_t>(extensions.size());
  create_info.ppEnabledExtensionNames =
      extensions.empty() ? nullptr : extensions.data();
  checkVk(vkCreateDevice(physical, &create_info, nullptr, &device),
          "vkCreateDevice");
  vkGetDeviceQueue(device, graphics_family, 0, &graphics_queue);
  if (dedicated_present_family) {
    vkGetDeviceQueue(device, present_family, 0, &present_queue);
  } else {
    present_queue = graphics_queue;
  }
}

void VulkanRenderer::Impl::createCommandPool() {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = graphics_family;
  checkVk(vkCreateCommandPool(device, &pool_info, nullptr, &command_pool),
          "vkCreateCommandPool");

  VkCommandBufferAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocation.commandPool = command_pool;
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = 1;
  checkVk(vkAllocateCommandBuffers(device, &allocation, &command_buffer),
          "vkAllocateCommandBuffers");
}

auto VulkanRenderer::Impl::createImageObject(
    const std::string& name, VkFormat format, VkImageUsageFlags usage,
    VkImageAspectFlags aspect, std::uint32_t image_width,
    std::uint32_t image_height, std::uint32_t bytes_per_pixel) -> Image {
  Image result;
  result.name = name;
  result.format = format;
  result.bytes_per_pixel = bytes_per_pixel;
  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = format;
  image_info.extent = {image_width, image_height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  checkVk(vkCreateImage(device, &image_info, nullptr, &result.image),
          "vkCreateImage");
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, result.image, &requirements);
  result.allocation_size = requirements.size;
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  checkVk(vkAllocateMemory(device, &allocation, nullptr, &result.memory),
          "vkAllocateMemory(image)");
  checkVk(vkBindImageMemory(device, result.image, result.memory, 0),
          "vkBindImageMemory");

  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = result.image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = format;
  view_info.subresourceRange.aspectMask = aspect;
  view_info.subresourceRange.baseMipLevel = 0;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.baseArrayLayer = 0;
  view_info.subresourceRange.layerCount = 1;
  checkVk(vkCreateImageView(device, &view_info, nullptr, &result.view),
          "vkCreateImageView");
  return result;
}

void VulkanRenderer::Impl::createImages() {
  constexpr VkImageUsageFlags sampled_color_usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  constexpr VkImageUsageFlags plain_color_usage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

  albedo = createImageObject("albedo", VK_FORMAT_R8G8B8A8_UNORM,
                             sampled_color_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                             width, height, 4);
  normal = createImageObject("normal", VK_FORMAT_R8G8B8A8_UNORM,
                             sampled_color_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                             width, height, 4);
  position = createImageObject("position", VK_FORMAT_R16G16B16A16_SFLOAT,
                               sampled_color_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                               width, height, 8);
  object_id = createImageObject("object_id", VK_FORMAT_R32_UINT,
                                plain_color_usage, VK_IMAGE_ASPECT_COLOR_BIT,
                                width, height, 4);
  depth = createImageObject("depth", VK_FORMAT_D32_SFLOAT,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT, width, height, 4);
  final_image = createImageObject("final", VK_FORMAT_R8G8B8A8_UNORM,
                                  plain_color_usage,
                                  VK_IMAGE_ASPECT_COLOR_BIT, width, height, 4);

  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_NEAREST;
  sampler_info.minFilter = VK_FILTER_NEAREST;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxLod = 0.0F;
  checkVk(vkCreateSampler(device, &sampler_info, nullptr, &sampler),
          "vkCreateSampler");
}

void VulkanRenderer::Impl::createRenderPasses() {
  std::array<VkAttachmentDescription, 5> attachments{};
  const VkImageLayout sampled_final = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  const VkImageLayout transfer_final = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  for (std::size_t index = 0; index < 3; ++index) {
    attachments[index].format = index == 2 ? VK_FORMAT_R16G16B16A16_SFLOAT
                                           : VK_FORMAT_R8G8B8A8_UNORM;
    attachments[index].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[index].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[index].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[index].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[index].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[index].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[index].finalLayout = sampled_final;
  }
  attachments[3].format = VK_FORMAT_R32_UINT;
  attachments[3].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[3].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[3].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[3].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[3].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[3].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[3].finalLayout = transfer_final;
  attachments[4].format = VK_FORMAT_D32_SFLOAT;
  attachments[4].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[4].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[4].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[4].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[4].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[4].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[4].finalLayout = transfer_final;

  std::array<VkAttachmentReference, 4> color_references{};
  for (std::size_t index = 0; index < color_references.size(); ++index) {
    color_references[index].attachment = static_cast<std::uint32_t>(index);
    color_references[index].layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  VkAttachmentReference depth_reference{};
  depth_reference.attachment = 4;
  depth_reference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount =
      static_cast<std::uint32_t>(color_references.size());
  subpass.pColorAttachments = color_references.data();
  subpass.pDepthStencilAttachment = &depth_reference;

  VkSubpassDependency gbuffer_dependency{};
  gbuffer_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  gbuffer_dependency.dstSubpass = 0;
  gbuffer_dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  gbuffer_dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  gbuffer_dependency.srcAccessMask = 0;
  gbuffer_dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
      VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo pass_info{};
  pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  pass_info.attachmentCount = static_cast<std::uint32_t>(attachments.size());
  pass_info.pAttachments = attachments.data();
  pass_info.subpassCount = 1;
  pass_info.pSubpasses = &subpass;
  pass_info.dependencyCount = 1;
  pass_info.pDependencies = &gbuffer_dependency;
  checkVk(vkCreateRenderPass(device, &pass_info, nullptr, &gbuffer_pass),
          "vkCreateRenderPass(gbuffer)");

  VkAttachmentDescription final_attachment{};
  final_attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  final_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  final_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  final_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  final_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  final_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  final_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  final_attachment.finalLayout = transfer_final;

  VkAttachmentReference final_reference{};
  final_reference.attachment = 0;
  final_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription final_subpass{};
  final_subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  final_subpass.colorAttachmentCount = 1;
  final_subpass.pColorAttachments = &final_reference;

  VkSubpassDependency final_dependency{};
  final_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  final_dependency.dstSubpass = 0;
  final_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  final_dependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  final_dependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  final_dependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  VkRenderPassCreateInfo final_pass_info{};
  final_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  final_pass_info.attachmentCount = 1;
  final_pass_info.pAttachments = &final_attachment;
  final_pass_info.subpassCount = 1;
  final_pass_info.pSubpasses = &final_subpass;
  final_pass_info.dependencyCount = 1;
  final_pass_info.pDependencies = &final_dependency;
  checkVk(vkCreateRenderPass(device, &final_pass_info, nullptr, &final_pass),
          "vkCreateRenderPass(final)");
}

void VulkanRenderer::Impl::createDescriptors() {
  std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
  for (std::size_t index = 0; index < bindings.size(); ++index) {
    bindings[index].binding = static_cast<std::uint32_t>(index);
    bindings[index].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[index].descriptorCount = 1;
    bindings[index].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
  layout_info.pBindings = bindings.data();
  checkVk(vkCreateDescriptorSetLayout(device, &layout_info, nullptr,
                                     &descriptor_layout),
          "vkCreateDescriptorSetLayout");

  VkDescriptorPoolSize pool_size{};
  pool_size.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  pool_size.descriptorCount = 3;
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 1;
  pool_info.pPoolSizes = &pool_size;
  checkVk(vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptor_pool),
          "vkCreateDescriptorPool");

  VkDescriptorSetAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocation.descriptorPool = descriptor_pool;
  allocation.descriptorSetCount = 1;
  allocation.pSetLayouts = &descriptor_layout;
  checkVk(vkAllocateDescriptorSets(device, &allocation, &descriptor_set),
          "vkAllocateDescriptorSets");

  const std::array<VkImageView, 3> views = {albedo.view, normal.view,
                                            position.view};
  std::array<VkDescriptorImageInfo, 3> image_infos{};
  std::array<VkWriteDescriptorSet, 3> writes{};
  for (std::size_t index = 0; index < views.size(); ++index) {
    image_infos[index].sampler = sampler;
    image_infos[index].imageView = views[index];
    image_infos[index].imageLayout =
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    writes[index].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[index].dstSet = descriptor_set;
    writes[index].dstBinding = static_cast<std::uint32_t>(index);
    writes[index].descriptorCount = 1;
    writes[index].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[index].pImageInfo = &image_infos[index];
  }
  vkUpdateDescriptorSets(device, static_cast<std::uint32_t>(writes.size()),
                         writes.data(), 0, nullptr);
}

void VulkanRenderer::Impl::createPipelines() {
  const Bytes gbuffer_vertex = loadSpirv("gbuffer.vert");
  const Bytes gbuffer_fragment = loadSpirv("gbuffer.frag");
  const Bytes fullscreen_vertex = loadSpirv("fullscreen.vert");
  const Bytes deferred_fragment = loadSpirv("deferred.frag");
  VkShaderModule gbuffer_vertex_module =
      createShaderModule(device, gbuffer_vertex);
  VkShaderModule gbuffer_fragment_module =
      createShaderModule(device, gbuffer_fragment);
  VkShaderModule fullscreen_vertex_module =
      createShaderModule(device, fullscreen_vertex);
  VkShaderModule deferred_fragment_module =
      createShaderModule(device, deferred_fragment);

  VkPushConstantRange push_range{};
  push_range.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push_range.offset = 0;
  push_range.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo gbuffer_layout_info{};
  gbuffer_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  gbuffer_layout_info.pushConstantRangeCount = 1;
  gbuffer_layout_info.pPushConstantRanges = &push_range;
  checkVk(vkCreatePipelineLayout(device, &gbuffer_layout_info, nullptr,
                                &gbuffer_pipeline_layout),
          "vkCreatePipelineLayout(gbuffer)");

  VkPipelineLayoutCreateInfo final_layout_info{};
  final_layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  final_layout_info.setLayoutCount = 1;
  final_layout_info.pSetLayouts = &descriptor_layout;
  checkVk(vkCreatePipelineLayout(device, &final_layout_info, nullptr,
                                &final_pipeline_layout),
          "vkCreatePipelineLayout(final)");

  std::array<VkPipelineShaderStageCreateInfo, 2> gbuffer_stages = {
      shaderStage(gbuffer_vertex_module, VK_SHADER_STAGE_VERTEX_BIT),
      shaderStage(gbuffer_fragment_module, VK_SHADER_STAGE_FRAGMENT_BIT)};
  VkVertexInputBindingDescription binding = vertexBinding();
  const auto attributes = vertexAttributes();
  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount =
      static_cast<std::uint32_t>(attributes.size());
  vertex_input.pVertexAttributeDescriptions = attributes.data();

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0F, 0.0F, static_cast<float>(width),
                      static_cast<float>(height), 0.0F, 1.0F};
  VkRect2D scissor{{0, 0}, {width, height}};
  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0F;
  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineDepthStencilStateCreateInfo depth_state{};
  depth_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_state.depthTestEnable = VK_TRUE;
  depth_state.depthWriteEnable = VK_TRUE;
  depth_state.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  depth_state.minDepthBounds = 0.0F;
  depth_state.maxDepthBounds = 1.0F;

  std::array<VkPipelineColorBlendAttachmentState, 4> gbuffer_blends{};
  for (auto& blend : gbuffer_blends) {
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.blendEnable = VK_FALSE;
  }
  VkPipelineColorBlendStateCreateInfo gbuffer_blend_state{};
  gbuffer_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  gbuffer_blend_state.attachmentCount =
      static_cast<std::uint32_t>(gbuffer_blends.size());
  gbuffer_blend_state.pAttachments = gbuffer_blends.data();

  VkGraphicsPipelineCreateInfo gbuffer_info{};
  gbuffer_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  gbuffer_info.stageCount = static_cast<std::uint32_t>(gbuffer_stages.size());
  gbuffer_info.pStages = gbuffer_stages.data();
  gbuffer_info.pVertexInputState = &vertex_input;
  gbuffer_info.pInputAssemblyState = &input_assembly;
  gbuffer_info.pViewportState = &viewport_state;
  gbuffer_info.pRasterizationState = &raster;
  gbuffer_info.pMultisampleState = &multisample;
  gbuffer_info.pDepthStencilState = &depth_state;
  gbuffer_info.pColorBlendState = &gbuffer_blend_state;
  gbuffer_info.layout = gbuffer_pipeline_layout;
  gbuffer_info.renderPass = gbuffer_pass;
  gbuffer_info.subpass = 0;
  checkVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gbuffer_info,
                                   nullptr, &gbuffer_pipeline),
          "vkCreateGraphicsPipelines(gbuffer)");

  std::array<VkPipelineShaderStageCreateInfo, 2> final_stages = {
      shaderStage(fullscreen_vertex_module, VK_SHADER_STAGE_VERTEX_BIT),
      shaderStage(deferred_fragment_module, VK_SHADER_STAGE_FRAGMENT_BIT)};
  VkPipelineVertexInputStateCreateInfo empty_vertex_input{};
  empty_vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkPipelineColorBlendAttachmentState final_blend{};
  final_blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                               VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo final_blend_state{};
  final_blend_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  final_blend_state.attachmentCount = 1;
  final_blend_state.pAttachments = &final_blend;
  VkGraphicsPipelineCreateInfo final_info{};
  final_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  final_info.stageCount = static_cast<std::uint32_t>(final_stages.size());
  final_info.pStages = final_stages.data();
  final_info.pVertexInputState = &empty_vertex_input;
  final_info.pInputAssemblyState = &input_assembly;
  final_info.pViewportState = &viewport_state;
  final_info.pRasterizationState = &raster;
  final_info.pMultisampleState = &multisample;
  final_info.pDepthStencilState = nullptr;
  final_info.pColorBlendState = &final_blend_state;
  final_info.layout = final_pipeline_layout;
  final_info.renderPass = final_pass;
  final_info.subpass = 0;
  checkVk(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &final_info,
                                   nullptr, &final_pipeline),
          "vkCreateGraphicsPipelines(final)");

  vkDestroyShaderModule(device, gbuffer_vertex_module, nullptr);
  vkDestroyShaderModule(device, gbuffer_fragment_module, nullptr);
  vkDestroyShaderModule(device, fullscreen_vertex_module, nullptr);
  vkDestroyShaderModule(device, deferred_fragment_module, nullptr);
}

void VulkanRenderer::Impl::createFramebuffers() {
  const std::array<VkImageView, 5> views = {
      albedo.view, normal.view, position.view, object_id.view, depth.view};
  VkFramebufferCreateInfo gbuffer_info{};
  gbuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  gbuffer_info.renderPass = gbuffer_pass;
  gbuffer_info.attachmentCount = static_cast<std::uint32_t>(views.size());
  gbuffer_info.pAttachments = views.data();
  gbuffer_info.width = width;
  gbuffer_info.height = height;
  gbuffer_info.layers = 1;
  checkVk(vkCreateFramebuffer(device, &gbuffer_info, nullptr,
                             &gbuffer_framebuffer),
          "vkCreateFramebuffer(gbuffer)");

  VkFramebufferCreateInfo final_info{};
  final_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  final_info.renderPass = final_pass;
  final_info.attachmentCount = 1;
  final_info.pAttachments = &final_image.view;
  final_info.width = width;
  final_info.height = height;
  final_info.layers = 1;
  checkVk(vkCreateFramebuffer(device, &final_info, nullptr,
                             &final_framebuffer),
          "vkCreateFramebuffer(final)");
}

void VulkanRenderer::Impl::createFences() {
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  checkVk(vkCreateFence(device, &fence_info, nullptr, &render_fence),
          "vkCreateFence(render)");
  checkVk(vkCreateFence(device, &fence_info, nullptr, &present_fence),
          "vkCreateFence(present)");

  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  checkVk(vkCreateSemaphore(device, &semaphore_info, nullptr,
                           &image_available),
          "vkCreateSemaphore(image_available)");
  checkVk(vkCreateSemaphore(device, &semaphore_info, nullptr,
                           &render_finished),
          "vkCreateSemaphore(render_finished)");
}

void VulkanRenderer::Impl::createSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities{};
  checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical, surface,
                                                    &capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
  std::uint32_t format_count = 0;
  checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
                                              &format_count, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(physical, surface,
                                              &format_count, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
  if (formats.empty()) {
    throw std::runtime_error("surface has no Vulkan formats");
  }
  VkSurfaceFormatKHR chosen_format = formats.front();
  for (const VkSurfaceFormatKHR& candidate : formats) {
    if ((candidate.format == VK_FORMAT_B8G8R8A8_UNORM ||
         candidate.format == VK_FORMAT_R8G8B8A8_UNORM) &&
        candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen_format = candidate;
      break;
    }
  }

  std::uint32_t mode_count = 0;
  checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                                   &mode_count, nullptr),
          "vkGetPhysicalDeviceSurfacePresentModesKHR");
  std::vector<VkPresentModeKHR> modes(mode_count);
  checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(physical, surface,
                                                   &mode_count, modes.data()),
          "vkGetPhysicalDeviceSurfacePresentModesKHR");
  VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
  for (VkPresentModeKHR candidate : modes) {
    if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) {
      present_mode = candidate;
    }
  }

  swapchain_format = chosen_format.format;
  if (capabilities.currentExtent.width !=
      std::numeric_limits<std::uint32_t>::max()) {
    swapchain_extent = capabilities.currentExtent;
  } else {
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    swapchain_extent.width =
        std::clamp(static_cast<std::uint32_t>(framebuffer_width),
                   capabilities.minImageExtent.width,
                   capabilities.maxImageExtent.width);
    swapchain_extent.height =
        std::clamp(static_cast<std::uint32_t>(framebuffer_height),
                   capabilities.minImageExtent.height,
                   capabilities.maxImageExtent.height);
  }
  std::uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }
  VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  const std::array<VkCompositeAlphaFlagBitsKHR, 4> composites = {
      VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
      VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
      VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR};
  for (VkCompositeAlphaFlagBitsKHR candidate : composites) {
    if ((capabilities.supportedCompositeAlpha & candidate) != 0U) {
      composite = candidate;
      break;
    }
  }
  std::array<std::uint32_t, 2> families = {graphics_family, present_family};
  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface;
  create_info.minImageCount = image_count;
  create_info.imageFormat = swapchain_format;
  create_info.imageColorSpace = chosen_format.colorSpace;
  create_info.imageExtent = swapchain_extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  create_info.imageSharingMode =
      dedicated_present_family ? VK_SHARING_MODE_CONCURRENT
                               : VK_SHARING_MODE_EXCLUSIVE;
  create_info.queueFamilyIndexCount =
      dedicated_present_family ? static_cast<std::uint32_t>(families.size())
                               : 0;
  create_info.pQueueFamilyIndices =
      dedicated_present_family ? families.data() : nullptr;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = composite;
  create_info.presentMode = present_mode;
  create_info.clipped = VK_TRUE;
  create_info.oldSwapchain = VK_NULL_HANDLE;
  checkVk(vkCreateSwapchainKHR(device, &create_info, nullptr, &swapchain),
          "vkCreateSwapchainKHR");

  std::uint32_t swapchain_count = 0;
  checkVk(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_count,
                                 nullptr),
          "vkGetSwapchainImagesKHR");
  swapchain_images.resize(swapchain_count);
  checkVk(vkGetSwapchainImagesKHR(device, swapchain, &swapchain_count,
                                 swapchain_images.data()),
          "vkGetSwapchainImagesKHR");
  swapchain_views.resize(swapchain_count, VK_NULL_HANDLE);
  for (std::size_t index = 0; index < swapchain_images.size(); ++index) {
    VkImageViewCreateInfo view_info{};
    view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_info.image = swapchain_images[index];
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = swapchain_format;
    view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    view_info.subresourceRange.levelCount = 1;
    view_info.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device, &view_info, nullptr,
                             &swapchain_views[index]),
            "vkCreateImageView(swapchain)");
  }
}

void VulkanRenderer::Impl::destroySwapchain() {
  if (device == VK_NULL_HANDLE) {
    return;
  }
  for (VkImageView view : swapchain_views) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device, view, nullptr);
    }
  }
  swapchain_views.clear();
  swapchain_images.clear();
  if (swapchain != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain = VK_NULL_HANDLE;
  }
}

std::vector<DrawRange> VulkanRenderer::Impl::uploadScene(
    const Scene& scene, const std::vector<std::string>& visible) {
  std::set<std::string> visible_set(visible.begin(), visible.end());
  frame_vertices.clear();
  std::vector<DrawRange> ranges;
  ranges.reserve(scene.objects.size());

  for (std::size_t object_index = 0; object_index < scene.objects.size();
       ++object_index) {
    const SceneObject& object = scene.objects[object_index];
    if (!visible_set.empty() &&
        visible_set.find(object.id) == visible_set.end()) {
      continue;
    }
    std::vector<Vertex> base =
        object.shape == "prism" ? makePrism() : makeCube();
    const Vec3 rotation_radians{
        object.rotation_deg.x * 3.14159265358979323846F / 180.0F,
        object.rotation_deg.y * 3.14159265358979323846F / 180.0F,
        object.rotation_deg.z * 3.14159265358979323846F / 180.0F};
    const Mat4 model = Mat4::translation(object.position) *
                       Mat4::rotationY(rotation_radians.y) *
                       Mat4::rotationX(rotation_radians.x) *
                       Mat4::rotationZ(rotation_radians.z) *
                       Mat4::scale(object.scale);
    const Mat4 normal_model = Mat4::rotationY(rotation_radians.y) *
                              Mat4::rotationX(rotation_radians.x) *
                              Mat4::rotationZ(rotation_radians.z);
    const float safe_x = std::abs(object.scale.x) < 1e-6F ? 1e-6F
                                                          : object.scale.x;
    const float safe_y = std::abs(object.scale.y) < 1e-6F ? 1e-6F
                                                          : object.scale.y;
    const float safe_z = std::abs(object.scale.z) < 1e-6F ? 1e-6F
                                                          : object.scale.z;
    DrawRange range;
    range.first_vertex = static_cast<std::uint32_t>(frame_vertices.size());
    range.vertex_count = static_cast<std::uint32_t>(base.size());
    range.object_index = object_index;
    for (const Vertex& vertex : base) {
      const float position_input[4] = {vertex.position[0], vertex.position[1],
                                       vertex.position[2], 1.0F};
      float transformed[4] = {0.0F, 0.0F, 0.0F, 0.0F};
      for (std::size_t column = 0; column < 4; ++column) {
        for (std::size_t row = 0; row < 4; ++row) {
          transformed[column] +=
              model.data[row * 4 + column] * position_input[row];
        }
      }
      const float normal_input[3] = {vertex.normal[0] / safe_x,
                                     vertex.normal[1] / safe_y,
                                     vertex.normal[2] / safe_z};
      float normal_output[3] = {0.0F, 0.0F, 0.0F};
      for (std::size_t column = 0; column < 3; ++column) {
        for (std::size_t row = 0; row < 3; ++row) {
          normal_output[column] +=
              normal_model.data[row * 4 + column] * normal_input[row];
        }
      }
      const float length =
          std::sqrt(normal_output[0] * normal_output[0] +
                    normal_output[1] * normal_output[1] +
                    normal_output[2] * normal_output[2]);
      if (length > 1e-8F) {
        normal_output[0] /= length;
        normal_output[1] /= length;
        normal_output[2] /= length;
      }
      frame_vertices.push_back({{transformed[0], transformed[1],
                                 transformed[2]},
                                {normal_output[0], normal_output[1],
                                 normal_output[2]}});
    }
    ranges.push_back(range);
  }

  const VkDeviceSize required =
      std::max<VkDeviceSize>(static_cast<VkDeviceSize>(frame_vertices.size()) *
                                 sizeof(Vertex),
                             sizeof(Vertex));
  if (vertex_buffer.buffer == VK_NULL_HANDLE || vertex_buffer.size < required) {
    vkDeviceWaitIdle(device);
    destroyBuffer(vertex_buffer);
    vertex_buffer = createBufferObject(
        alignUp(required, 4096), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
  }
  if (!frame_vertices.empty()) {
    std::memcpy(vertex_buffer.mapped, frame_vertices.data(),
                frame_vertices.size() * sizeof(Vertex));
  }
  return ranges;
}

void VulkanRenderer::Impl::renderAttachments(
    const Scene& scene, const std::vector<DrawRange>& draws,
    const Mat4& view_projection) {
  std::array<VkClearValue, 5> clears{};
  clears[0].color = {{0.0F, 0.0F, 0.0F, 0.0F}};
  clears[1].color = {{0.5F, 0.5F, 1.0F, 0.0F}};
  clears[2].color = {{0.0F, 0.0F, 0.0F, 0.0F}};
  clears[3].color = {{0.0F, 0.0F, 0.0F, 0.0F}};
  clears[4].depthStencil = {1.0F, 0};

  VkRenderPassBeginInfo gbuffer_begin{};
  gbuffer_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  gbuffer_begin.renderPass = gbuffer_pass;
  gbuffer_begin.framebuffer = gbuffer_framebuffer;
  gbuffer_begin.renderArea = {{0, 0}, {width, height}};
  gbuffer_begin.clearValueCount = static_cast<std::uint32_t>(clears.size());
  gbuffer_begin.pClearValues = clears.data();
  vkCmdBeginRenderPass(command_buffer, &gbuffer_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    gbuffer_pipeline);
  const VkDeviceSize vertex_offset = 0;
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer.buffer,
                         &vertex_offset);
  for (const DrawRange& draw : draws) {
    const SceneObject& object = scene.objects[draw.object_index];
    PushConstants push{};
    std::memcpy(push.view_projection, view_projection.dataPtr(),
                sizeof(push.view_projection));
    push.albedo[0] = object.albedo.x;
    push.albedo[1] = object.albedo.y;
    push.albedo[2] = object.albedo.z;
    push.albedo[3] = 1.0F;
    push.params[0] = object.roughness;
    push.params[1] = object.metallic;
    push.params[2] = static_cast<float>(draw.object_index + 1);
    push.params[3] = object.corrected ? 1.0F : 0.0F;
    vkCmdPushConstants(command_buffer, gbuffer_pipeline_layout,
                       VK_SHADER_STAGE_VERTEX_BIT |
                           VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(push), &push);
    vkCmdDraw(command_buffer, draw.vertex_count, 1, draw.first_vertex, 0);
  }
  vkCmdEndRenderPass(command_buffer);

  VkRenderPassBeginInfo final_begin{};
  final_begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  final_begin.renderPass = final_pass;
  final_begin.framebuffer = final_framebuffer;
  final_begin.renderArea = {{0, 0}, {width, height}};
  VkClearValue final_clear{};
  final_clear.color = {{0.02F, 0.025F, 0.035F, 1.0F}};
  final_begin.clearValueCount = 1;
  final_begin.pClearValues = &final_clear;
  vkCmdBeginRenderPass(command_buffer, &final_begin, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    final_pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          final_pipeline_layout, 0, 1, &descriptor_set, 0,
                          nullptr);
  vkCmdDraw(command_buffer, 3, 1, 0, 0);
  vkCmdEndRenderPass(command_buffer);
}

RenderedView VulkanRenderer::Impl::readback(
    const Scene& scene, const std::string& view_id, const std::string& kind,
    const std::string& object_identifier, std::uint32_t object_index,
    std::uint64_t frame_index) {
  struct CopyItem {
    Image* image = nullptr;
    VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    VkAccessFlags write_access = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    std::uint64_t offset = 0;
    std::uint64_t size = 0;
    bool sampled = false;
  };
  std::vector<CopyItem> items;
  std::uint64_t required = 0;
  const auto add_item = [&](Image& image, VkImageAspectFlags aspect,
                            VkAccessFlags access, bool sampled) {
    CopyItem item;
    item.image = &image;
    item.aspect = aspect;
    item.write_access = access;
    item.offset = alignUp(required, kReadbackAlignment);
    item.size = static_cast<std::uint64_t>(width) * height *
                image.bytes_per_pixel;
    item.sampled = sampled;
    required = item.offset + item.size;
    items.push_back(item);
  };
  add_item(albedo, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT, true);
  add_item(normal, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT, true);
  add_item(position, VK_IMAGE_ASPECT_COLOR_BIT, VK_ACCESS_SHADER_READ_BIT, true);
  add_item(object_id, VK_IMAGE_ASPECT_COLOR_BIT,
           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, false);
  add_item(depth, VK_IMAGE_ASPECT_DEPTH_BIT,
           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, false);
  add_item(final_image, VK_IMAGE_ASPECT_COLOR_BIT,
           VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, false);

  if (readback_buffer.buffer == VK_NULL_HANDLE ||
      readback_buffer.size < required) {
    vkDeviceWaitIdle(device);
    destroyBuffer(readback_buffer);
    readback_buffer = createBufferObject(
        alignUp(required, 4096), VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        true);
  }

  checkVk(vkWaitForFences(device, 1, &render_fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences");
  checkVk(vkResetFences(device, 1, &render_fence), "vkResetFences");
  checkVk(vkResetCommandBuffer(command_buffer, 0),
          "vkResetCommandBuffer");
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checkVk(vkBeginCommandBuffer(command_buffer, &begin_info),
          "vkBeginCommandBuffer");

  const Mat4 view =
      Mat4::lookAt(scene.camera.eye, scene.camera.target, scene.camera.up);
  const Mat4 projection = Mat4::orthographic(
      -scene.camera.scale_x, scene.camera.scale_x, -scene.camera.scale_y,
      scene.camera.scale_y, scene.camera.near_plane, scene.camera.far_plane);
  const Mat4 view_projection = projection * view;
  renderAttachments(scene, draw_ranges, view_projection);

  std::array<VkBufferImageCopy, 6> copies{};
  for (std::size_t index = 0; index < items.size(); ++index) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = items[index].sampled
                            ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                            : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = items[index].image->image;
    barrier.subresourceRange.aspectMask = items[index].aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = items[index].write_access;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    copies[index].bufferOffset = items[index].offset;
    copies[index].imageSubresource.aspectMask = items[index].aspect;
    copies[index].imageSubresource.mipLevel = 0;
    copies[index].imageSubresource.baseArrayLayer = 0;
    copies[index].imageSubresource.layerCount = 1;
    copies[index].imageExtent = {width, height, 1};
    vkCmdCopyImageToBuffer(command_buffer, items[index].image->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer.buffer, 1, &copies[index]);
  }

  VkMemoryBarrier final_barrier{};
  final_barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  final_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  final_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &final_barrier, 0,
                       nullptr, 0, nullptr);
  checkVk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer");
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer;
  checkVk(vkQueueSubmit(graphics_queue, 1, &submit, render_fence),
          "vkQueueSubmit(render)");
  checkVk(vkWaitForFences(device, 1, &render_fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences(render)");

  RenderedView view_result;
  view_result.view_id = view_id;
  view_result.kind = kind;
  view_result.object_id = object_identifier;
  view_result.object_index = object_index;
  view_result.rendered_at = nowStamp();
  view_result.frame_index = frame_index;
  view_result.width = width;
  view_result.height = height;
  const auto* mapped = static_cast<const std::uint8_t*>(readback_buffer.mapped);
  for (const CopyItem& item : items) {
    Bytes bytes(mapped + item.offset, mapped + item.offset + item.size);
    view_result.attachments.push_back(makeAttachment(
        item.image->name,
        item.image->format == VK_FORMAT_R16G16B16A16_SFLOAT
            ? "R16G16B16A16_SFLOAT"
            : item.image->format == VK_FORMAT_R32_UINT
                  ? "R32_UINT"
                  : item.image->format == VK_FORMAT_D32_SFLOAT
                        ? "D32_SFLOAT"
                        : "R8G8B8A8_UNORM",
        width, height, static_cast<std::uint32_t>(item.image->bytes_per_pixel),
        static_cast<std::uint32_t>(width * item.image->bytes_per_pixel),
        std::move(bytes)));
  }
  view_result.mask.ids.resize(static_cast<std::size_t>(width) * height);
  const RenderedAttachment* object_attachment = nullptr;
  for (const RenderedAttachment& attachment : view_result.attachments) {
    if (attachment.name == "object_id") {
      object_attachment = &attachment;
      break;
    }
  }
  if (object_attachment != nullptr &&
      object_attachment->bytes.size() >=
          view_result.mask.ids.size() * sizeof(std::uint32_t)) {
    std::memcpy(view_result.mask.ids.data(), object_attachment->bytes.data(),
                view_result.mask.ids.size() * sizeof(std::uint32_t));
  }
  rendered_once = true;
  return view_result;
}

void VulkanRenderer::Impl::present() {
  if (window == nullptr || swapchain == VK_NULL_HANDLE || !rendered_once) {
    return;
  }
  checkVk(vkWaitForFences(device, 1, &present_fence, VK_TRUE, UINT64_MAX),
          "vkWaitForFences(present)");
  std::uint32_t image_index = 0;
  const VkResult acquire = vkAcquireNextImageKHR(
      device, swapchain, UINT64_MAX, image_available, VK_NULL_HANDLE,
      &image_index);
  if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
    failVk(acquire, "vkAcquireNextImageKHR");
  }
  checkVk(vkResetFences(device, 1, &present_fence), "vkResetFences(present)");
  checkVk(vkResetCommandBuffer(command_buffer, 0),
          "vkResetCommandBuffer(present)");
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checkVk(vkBeginCommandBuffer(command_buffer, &begin_info),
          "vkBeginCommandBuffer(present)");

  VkImageMemoryBarrier to_transfer{};
  to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.image = swapchain_images[image_index];
  to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_transfer.subresourceRange.levelCount = 1;
  to_transfer.subresourceRange.layerCount = 1;
  to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                       1, &to_transfer);

  VkImageBlit blit{};
  blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.srcSubresource.layerCount = 1;
  blit.srcOffsets[1] = {static_cast<std::int32_t>(width),
                        static_cast<std::int32_t>(height), 1};
  blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  blit.dstSubresource.layerCount = 1;
  blit.dstOffsets[1] = {
      static_cast<std::int32_t>(swapchain_extent.width),
      static_cast<std::int32_t>(swapchain_extent.height), 1};
  vkCmdBlitImage(command_buffer, final_image.image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 swapchain_images[image_index],
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                 VK_FILTER_NEAREST);

  VkImageMemoryBarrier to_present = to_transfer;
  to_present.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_present.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  to_present.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_present.dstAccessMask = 0;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &to_present);
  checkVk(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer(present)");

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &command_buffer;
  const VkPipelineStageFlags wait_stage =
      VK_PIPELINE_STAGE_TRANSFER_BIT;
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &image_available;
  submit.pWaitDstStageMask = &wait_stage;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &render_finished;
  checkVk(vkQueueSubmit(graphics_queue, 1, &submit, present_fence),
          "vkQueueSubmit(present)");

  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = &render_finished;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = &swapchain;
  present_info.pImageIndices = &image_index;
  const VkResult presented =
      vkQueuePresentKHR(present_queue, &present_info);
  if (presented != VK_SUCCESS && presented != VK_SUBOPTIMAL_KHR) {
    failVk(presented, "vkQueuePresentKHR");
  }
}

VulkanRenderer::VulkanRenderer(std::uint32_t width, std::uint32_t height,
                               GLFWwindow* window)
    : impl_(std::make_unique<Impl>(width, height, window)) {}

VulkanRenderer::~VulkanRenderer() = default;

const DeviceIdentity& VulkanRenderer::device() const {
  return impl_->identity;
}

std::uint32_t VulkanRenderer::width() const {
  return impl_->width;
}

std::uint32_t VulkanRenderer::height() const {
  return impl_->height;
}

bool VulkanRenderer::hasWindow() const {
  return impl_->window != nullptr;
}

RenderedView VulkanRenderer::render(
    const Scene& scene, const std::vector<std::string>& visible_objects,
    const std::string& view_id, const std::string& kind,
    const std::string& object_id, std::uint32_t object_index,
    std::uint64_t frame_index) {
  impl_->draw_ranges = impl_->uploadScene(scene, visible_objects);
  return impl_->readback(scene, view_id, kind, object_id, object_index,
                         frame_index);
}

void VulkanRenderer::present() {
  impl_->present();
}

}  // namespace gbi
