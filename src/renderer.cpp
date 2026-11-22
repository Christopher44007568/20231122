#include "renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <vector>

#define GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#ifndef BBOX_SHADER_DIR
#define BBOX_SHADER_DIR "shaders"
#endif

#include "sha256.hpp"

namespace bbox {
namespace {

struct Vertex {
  float position[3];
  float color[3];
};

void checkVk(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                             std::to_string(static_cast<int>(result)));
  }
}

std::vector<char> readBinary(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    throw std::runtime_error("cannot open shader " + path);
  }
  const std::streamsize size = input.tellg();
  input.seekg(0, std::ios::beg);
  std::vector<char> data(static_cast<std::size_t>(size));
  if (!input.read(data.data(), size)) {
    throw std::runtime_error("cannot read shader " + path);
  }
  return data;
}

std::vector<Vertex> buildVertices(const std::map<std::string, Model>& models,
                                  const std::set<std::string>& exceptions,
                                  double aspect) {
  Vec3 scene_min{std::numeric_limits<double>::max(),
                 std::numeric_limits<double>::max(),
                 std::numeric_limits<double>::max()};
  Vec3 scene_max{std::numeric_limits<double>::lowest(),
                 std::numeric_limits<double>::lowest(),
                 std::numeric_limits<double>::lowest()};
  for (const auto& entry : models) {
    const Bounds& bounds = entry.second.bounds;
    scene_min.x = std::min(scene_min.x, bounds.min.x);
    scene_min.y = std::min(scene_min.y, bounds.min.y);
    scene_min.z = std::min(scene_min.z, bounds.min.z);
    scene_max.x = std::max(scene_max.x, bounds.max.x);
    scene_max.y = std::max(scene_max.y, bounds.max.y);
    scene_max.z = std::max(scene_max.z, bounds.max.z);
  }
  if (models.empty()) {
    return {};
  }

  const Vec3 center{(scene_min.x + scene_max.x) * 0.5,
                    (scene_min.y + scene_max.y) * 0.5,
                    (scene_min.z + scene_max.z) * 0.5};
  const double span = std::max(
      {scene_max.x - scene_min.x, scene_max.y - scene_min.y,
       scene_max.z - scene_min.z, 0.001});
  const double yaw = 0.62;
  const double pitch = 0.48;
  const std::array<std::array<int, 2>, 12> edges = {
      {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
       {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}};

  std::vector<Vertex> vertices;
  for (const auto& entry : models) {
    const Model& model = entry.second;
    const std::array<Vec3, 8> corners = {
        {{model.bounds.min.x, model.bounds.min.y, model.bounds.min.z},
         {model.bounds.max.x, model.bounds.min.y, model.bounds.min.z},
         {model.bounds.max.x, model.bounds.max.y, model.bounds.min.z},
         {model.bounds.min.x, model.bounds.max.y, model.bounds.min.z},
         {model.bounds.min.x, model.bounds.min.y, model.bounds.max.z},
         {model.bounds.max.x, model.bounds.min.y, model.bounds.max.z},
         {model.bounds.max.x, model.bounds.max.y, model.bounds.max.z},
         {model.bounds.min.x, model.bounds.max.y, model.bounds.max.z}}};

    std::array<std::array<float, 3>, 8> projected{};
    for (std::size_t index = 0; index < corners.size(); ++index) {
      const double dx = corners[index].x - center.x;
      const double dy = corners[index].y - center.y;
      const double dz = corners[index].z - center.z;
      const double rotated_x = dx * std::cos(yaw) - dy * std::sin(yaw);
      const double rotated_y = dx * std::sin(yaw) + dy * std::cos(yaw);
      const double screen_y =
          dz * std::cos(pitch) - rotated_y * std::sin(pitch);
      projected[index] = {
          static_cast<float>(rotated_x / span * 1.65 * aspect),
          static_cast<float>(screen_y / span * 1.65), 0.0F};
    }

    std::array<float, 3> color{0.30F, 0.68F, 0.95F};
    if (exceptions.count(model.id) != 0) {
      color = {0.95F, 0.30F, 0.28F};
    } else if (model.last_event != "seed-v1") {
      color = {0.30F, 0.92F, 0.52F};
    }
    for (const auto& edge : edges) {
      for (const int corner : edge) {
        vertices.push_back({{projected[corner][0], projected[corner][1], 0.0F},
                            {color[0], color[1], color[2]}});
      }
    }
  }
  return vertices;
}

class VulkanApp {
 public:
  VulkanApp(const std::vector<Vertex>& vertices, bool visible);
  ~VulkanApp();

  void drawFrames(int frame_count, bool write_last_frame,
                  const std::string& screenshot_path);
  const std::string& adapterName() const { return adapter_name_; }
  const std::string& formatName() const { return format_name_; }
  int width() const { return static_cast<int>(extent_.width); }
  int height() const { return static_cast<int>(extent_.height); }

 private:
  void createWindow(bool visible);
  void createInstance();
  void createSurface();
  void pickPhysicalDevice();
  void createDevice();
  void createSwapchain();
  void createImageViews();
  void createRenderPass();
  void createPipeline();
  void createFramebuffers();
  void createVertexBuffer(const std::vector<Vertex>& vertices);
  void createReadbackBuffer();
  void createCommandObjects();
  VkShaderModule createShaderModule(const std::string& path);
  std::uint32_t findMemoryType(std::uint32_t type_bits,
                               VkMemoryPropertyFlags properties) const;
  void recordFrame(bool copy_to_readback);
  void saveBmp(const std::string& path);
  void destroySwapchainObjects();

  GLFWwindow* window_ = nullptr;
  VkInstance instance_ = VK_NULL_HANDLE;
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  std::uint32_t queue_family_ = 0;
  VkQueue queue_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_UNDEFINED;
  VkExtent2D extent_{};
  std::vector<VkImage> images_;
  std::vector<VkImageView> image_views_;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer_ = VK_NULL_HANDLE;
  VkSemaphore image_available_ = VK_NULL_HANDLE;
  VkSemaphore render_finished_ = VK_NULL_HANDLE;
  VkBuffer vertex_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory_ = VK_NULL_HANDLE;
  VkBuffer readback_buffer_ = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory_ = VK_NULL_HANDLE;
  void* readback_mapped_ = nullptr;
  std::uint32_t vertex_count_ = 0;
  std::uint32_t image_index_ = 0;
  std::string adapter_name_;
  std::string format_name_;
  bool bgra_ = true;
};

}  // namespace

namespace {

VulkanApp::VulkanApp(const std::vector<Vertex>& vertices, bool visible) {
  createWindow(visible);
  createInstance();
  createSurface();
  pickPhysicalDevice();
  createDevice();
  createSwapchain();
  createImageViews();
  createRenderPass();
  createPipeline();
  createFramebuffers();
  createVertexBuffer(vertices);
  createReadbackBuffer();
  createCommandObjects();
}

VulkanApp::~VulkanApp() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
  }
  if (device_ != VK_NULL_HANDLE) {
    if (readback_memory_ != VK_NULL_HANDLE && readback_mapped_ != nullptr) {
      vkUnmapMemory(device_, readback_memory_);
    }
    if (readback_buffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, readback_buffer_, nullptr);
    }
    if (readback_memory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, readback_memory_, nullptr);
    }
    if (vertex_buffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    }
    if (vertex_memory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, vertex_memory_, nullptr);
    }
    if (image_available_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, image_available_, nullptr);
    }
    if (render_finished_ != VK_NULL_HANDLE) {
      vkDestroySemaphore(device_, render_finished_, nullptr);
    }
    if (command_pool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    }
    destroySwapchainObjects();
    if (pipeline_ != VK_NULL_HANDLE) {
      vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (pipeline_layout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    }
    if (render_pass_ != VK_NULL_HANDLE) {
      vkDestroyRenderPass(device_, render_pass_, nullptr);
    }
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
  }
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
  }
  glfwTerminate();
}

void VulkanApp::createWindow(bool visible) {
  if (glfwInit() != GLFW_TRUE) {
    throw std::runtime_error("glfwInit failed (no display?)");
  }
  if (glfwVulkanSupported() != GLFW_TRUE) {
    throw std::runtime_error("GLFW reports no Vulkan loader");
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
  window_ = glfwCreateWindow(960, 600, "Bounding Box Replay", nullptr, nullptr);
  if (window_ == nullptr) {
    throw std::runtime_error("glfwCreateWindow failed");
  }
}

void VulkanApp::createInstance() {
  std::uint32_t extension_count = 0;
  const char** extensions =
      glfwGetRequiredInstanceExtensions(&extension_count);
  if (extensions == nullptr || extension_count == 0) {
    throw std::runtime_error("GLFW returned no Vulkan surface extensions");
  }

  VkApplicationInfo application{};
  application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application.pApplicationName = "bbox-replay";
  application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  application.pEngineName = "bbox-replay";
  application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  application.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  create_info.pApplicationInfo = &application;
  create_info.enabledExtensionCount = extension_count;
  create_info.ppEnabledExtensionNames = extensions;
  checkVk(vkCreateInstance(&create_info, nullptr, &instance_),
          "vkCreateInstance");
}

void VulkanApp::createSurface() {
  checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
          "glfwCreateWindowSurface");
}

void VulkanApp::pickPhysicalDevice() {
  std::uint32_t count = 0;
  checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
          "vkEnumeratePhysicalDevices");
  if (count == 0) {
    throw std::runtime_error("no Vulkan physical devices");
  }
  std::vector<VkPhysicalDevice> devices(count);
  checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
          "vkEnumeratePhysicalDevices");

  for (VkPhysicalDevice candidate : devices) {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                              families.data());
    std::uint32_t swapchain_extension_count = 0;
    checkVk(vkEnumerateDeviceExtensionProperties(
                candidate, nullptr, &swapchain_extension_count, nullptr),
            "vkEnumerateDeviceExtensionProperties");
    std::vector<VkExtensionProperties> extensions(swapchain_extension_count);
    checkVk(vkEnumerateDeviceExtensionProperties(
                candidate, nullptr, &swapchain_extension_count, extensions.data()),
            "vkEnumerateDeviceExtensionProperties");
    const bool has_swapchain =
        std::any_of(extensions.begin(), extensions.end(),
                    [](const VkExtensionProperties& extension) {
                      return std::strcmp(extension.extensionName,
                                         VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                    });
    if (!has_swapchain) {
      continue;
    }
    for (std::uint32_t index = 0; index < family_count; ++index) {
      VkBool32 present_supported = VK_FALSE;
      checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                  candidate, index, surface_, &present_supported),
              "vkGetPhysicalDeviceSurfaceSupportKHR");
      if ((families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 &&
          present_supported == VK_TRUE) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        physical_device_ = candidate;
        queue_family_ = index;
        adapter_name_ = properties.deviceName;
        return;
      }
    }
  }
  throw std::runtime_error("no Vulkan device supports graphics and present");
}

void VulkanApp::createDevice() {
  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = queue_family_;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;

  const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
  VkDeviceCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  create_info.queueCreateInfoCount = 1;
  create_info.pQueueCreateInfos = &queue_info;
  create_info.enabledExtensionCount = 1;
  create_info.ppEnabledExtensionNames = &extension;
  checkVk(vkCreateDevice(physical_device_, &create_info, nullptr, &device_),
          "vkCreateDevice");
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
}

void VulkanApp::createSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities{};
  checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
              physical_device_, surface_, &capabilities),
          "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

  std::uint32_t format_count = 0;
  checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
              physical_device_, surface_, &format_count, nullptr),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
              physical_device_, surface_, &format_count, formats.data()),
          "vkGetPhysicalDeviceSurfaceFormatsKHR");
  if (formats.empty()) {
    throw std::runtime_error("Vulkan surface has no formats");
  }

  const std::array<VkFormat, 4> preferred_formats = {
      VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_B8G8R8A8_SRGB,
      VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8G8B8A8_SRGB};
  VkSurfaceFormatKHR selected = formats.front();
  for (const VkFormat preferred : preferred_formats) {
    const auto it =
        std::find_if(formats.begin(), formats.end(),
                     [preferred](const VkSurfaceFormatKHR& candidate) {
                       return candidate.format == preferred &&
                              candidate.colorSpace ==
                                  VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
                     });
    if (it != formats.end()) {
      selected = *it;
      break;
    }
  }
  format_ = selected.format;
  bgra_ = format_ == VK_FORMAT_B8G8R8A8_UNORM ||
          format_ == VK_FORMAT_B8G8R8A8_SRGB;
  format_name_ = bgra_ ? "B8G8R8A8" : "R8G8B8A8";

  VkExtent2D extent = capabilities.currentExtent;
  if (extent.width == std::numeric_limits<std::uint32_t>::max()) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    extent.width = static_cast<std::uint32_t>(std::max(width, 1));
    extent.height = static_cast<std::uint32_t>(std::max(height, 1));
    extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                              capabilities.maxImageExtent.width);
    extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                               capabilities.maxImageExtent.height);
  }
  extent_ = extent;

  std::uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0 &&
      image_count > capabilities.maxImageCount) {
    image_count = capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  create_info.surface = surface_;
  create_info.minImageCount = image_count;
  create_info.imageFormat = selected.format;
  create_info.imageColorSpace = selected.colorSpace;
  create_info.imageExtent = extent;
  create_info.imageArrayLayers = 1;
  create_info.imageUsage =
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  create_info.preTransform = capabilities.currentTransform;
  create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  create_info.clipped = VK_TRUE;
  checkVk(vkCreateSwapchainKHR(device_, &create_info, nullptr, &swapchain_),
          "vkCreateSwapchainKHR");

  checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count, nullptr),
          "vkGetSwapchainImagesKHR");
  images_.resize(image_count);
  checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &image_count,
                                  images_.data()),
          "vkGetSwapchainImagesKHR");
}

void VulkanApp::createImageViews() {
  image_views_.resize(images_.size(), VK_NULL_HANDLE);
  for (std::size_t index = 0; index < images_.size(); ++index) {
    VkImageViewCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.image = images_[index];
    create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    create_info.format = format_;
    create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    create_info.subresourceRange.baseMipLevel = 0;
    create_info.subresourceRange.levelCount = 1;
    create_info.subresourceRange.baseArrayLayer = 0;
    create_info.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device_, &create_info, nullptr,
                              &image_views_[index]),
            "vkCreateImageView");
  }
}

void VulkanApp::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = format_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  VkAttachmentReference color_reference{};
  color_reference.attachment = 0;
  color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.srcAccessMask = 0;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  create_info.attachmentCount = 1;
  create_info.pAttachments = &color;
  create_info.subpassCount = 1;
  create_info.pSubpasses = &subpass;
  create_info.dependencyCount = 1;
  create_info.pDependencies = &dependency;
  checkVk(vkCreateRenderPass(device_, &create_info, nullptr, &render_pass_),
          "vkCreateRenderPass");
}

VkShaderModule VulkanApp::createShaderModule(const std::string& path) {
  const std::vector<char> code = readBinary(path);
  VkShaderModuleCreateInfo create_info{};
  create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  create_info.codeSize = code.size();
  create_info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
  VkShaderModule module = VK_NULL_HANDLE;
  checkVk(vkCreateShaderModule(device_, &create_info, nullptr, &module),
          "vkCreateShaderModule");
  return module;
}

void VulkanApp::createPipeline() {
  const std::string shader_dir = BBOX_SHADER_DIR;
  VkShaderModule vertex_shader =
      createShaderModule(shader_dir + "/bbox.vert.spv");
  VkShaderModule fragment_shader =
      createShaderModule(shader_dir + "/bbox.frag.spv");

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_shader;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment_shader;
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0].location = 0;
  attributes[0].binding = 0;
  attributes[0].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[0].offset = offsetof(Vertex, position);
  attributes[1].location = 1;
  attributes[1].binding = 0;
  attributes[1].format = VK_FORMAT_R32G32B32_SFLOAT;
  attributes[1].offset = offsetof(Vertex, color);

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;

  VkPipelineInputAssemblyStateCreateInfo input_assembly{};
  input_assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  input_assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent_.width);
  viewport.height = static_cast<float>(extent_.height);
  viewport.maxDepth = 1.0F;
  VkRect2D scissor{};
  scissor.extent = extent_;
  VkPipelineViewportStateCreateInfo viewport_state{};
  viewport_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType =
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0F;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;

  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  checkVk(vkCreatePipelineLayout(device_, &layout_info, nullptr,
                                 &pipeline_layout_),
          "vkCreatePipelineLayout");

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &input_assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pColorBlendState = &blend;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;

  const VkResult result =
      vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline_info,
                                nullptr, &pipeline_);
  vkDestroyShaderModule(device_, fragment_shader, nullptr);
  vkDestroyShaderModule(device_, vertex_shader, nullptr);
  checkVk(result, "vkCreateGraphicsPipelines");
}

void VulkanApp::createFramebuffers() {
  framebuffers_.resize(image_views_.size(), VK_NULL_HANDLE);
  for (std::size_t index = 0; index < image_views_.size(); ++index) {
    VkFramebufferCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    create_info.renderPass = render_pass_;
    create_info.attachmentCount = 1;
    create_info.pAttachments = &image_views_[index];
    create_info.width = extent_.width;
    create_info.height = extent_.height;
    create_info.layers = 1;
    checkVk(vkCreateFramebuffer(device_, &create_info, nullptr,
                                &framebuffers_[index]),
            "vkCreateFramebuffer");
  }
}

std::uint32_t VulkanApp::findMemoryType(
    std::uint32_t type_bits, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory_properties{};
  vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties);
  for (std::uint32_t index = 0; index < memory_properties.memoryTypeCount;
       ++index) {
    if ((type_bits & (1U << index)) != 0 &&
        (memory_properties.memoryTypes[index].propertyFlags & properties) ==
            properties) {
      return index;
    }
  }
  throw std::runtime_error("no suitable Vulkan memory type");
}

void VulkanApp::createVertexBuffer(const std::vector<Vertex>& vertices) {
  vertex_count_ = static_cast<std::uint32_t>(vertices.size());
  if (vertices.empty()) {
    return;
  }
  const VkDeviceSize size = sizeof(Vertex) * vertices.size();
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  checkVk(vkCreateBuffer(device_, &buffer_info, nullptr, &vertex_buffer_),
          "vkCreateBuffer(vertex)");

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, vertex_buffer_, &requirements);
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  checkVk(vkAllocateMemory(device_, &allocation, nullptr, &vertex_memory_),
          "vkAllocateMemory(vertex)");
  checkVk(vkBindBufferMemory(device_, vertex_buffer_, vertex_memory_, 0),
          "vkBindBufferMemory(vertex)");
  void* mapped = nullptr;
  checkVk(vkMapMemory(device_, vertex_memory_, 0, size, 0, &mapped),
          "vkMapMemory(vertex)");
  std::memcpy(mapped, vertices.data(), static_cast<std::size_t>(size));
  vkUnmapMemory(device_, vertex_memory_);
}

void VulkanApp::createReadbackBuffer() {
  const VkDeviceSize size =
      static_cast<VkDeviceSize>(extent_.width) * extent_.height * 4U;
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  checkVk(vkCreateBuffer(device_, &buffer_info, nullptr, &readback_buffer_),
          "vkCreateBuffer(readback)");
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, readback_buffer_, &requirements);
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex =
      findMemoryType(requirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  checkVk(vkAllocateMemory(device_, &allocation, nullptr, &readback_memory_),
          "vkAllocateMemory(readback)");
  checkVk(vkBindBufferMemory(device_, readback_buffer_, readback_memory_, 0),
          "vkBindBufferMemory(readback)");
  checkVk(vkMapMemory(device_, readback_memory_, 0, size, 0, &readback_mapped_),
          "vkMapMemory(readback)");
}

void VulkanApp::createCommandObjects() {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = queue_family_;
  checkVk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
          "vkCreateCommandPool");
  VkCommandBufferAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocation.commandPool = command_pool_;
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = 1;
  checkVk(vkAllocateCommandBuffers(device_, &allocation, &command_buffer_),
          "vkAllocateCommandBuffers");

  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  checkVk(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                            &image_available_),
          "vkCreateSemaphore(image)");
  checkVk(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                            &render_finished_),
          "vkCreateSemaphore(render)");
}

void VulkanApp::recordFrame(bool copy_to_readback) {
  checkVk(vkResetCommandBuffer(command_buffer_, 0),
          "vkResetCommandBuffer");
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  checkVk(vkBeginCommandBuffer(command_buffer_, &begin_info),
          "vkBeginCommandBuffer");

  VkClearValue clear{};
  clear.color = {{0.015F, 0.020F, 0.034F, 1.0F}};
  VkRenderPassBeginInfo render_info{};
  render_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_info.renderPass = render_pass_;
  render_info.framebuffer = framebuffers_.at(image_index_);
  render_info.renderArea.extent = extent_;
  render_info.clearValueCount = 1;
  render_info.pClearValues = &clear;
  vkCmdBeginRenderPass(command_buffer_, &render_info,
                       VK_SUBPASS_CONTENTS_INLINE);
  if (vertex_count_ > 0) {
    vkCmdBindPipeline(command_buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(command_buffer_, 0, 1, &vertex_buffer_, &offset);
    vkCmdDraw(command_buffer_, vertex_count_, 1, 0, 0);
  }
  vkCmdEndRenderPass(command_buffer_);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = images_.at(image_index_);
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.layerCount = 1;
  if (copy_to_readback) {
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(command_buffer_,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = 1;
    region.imageExtent = {extent_.width, extent_.height, 1};
    vkCmdCopyImageToBuffer(command_buffer_, images_.at(image_index_),
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback_buffer_, 1, &region);
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = 0;
  } else {
    barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    barrier.dstAccessMask = 0;
  }
  barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  const VkPipelineStageFlags source_stage =
      copy_to_readback ? VK_PIPELINE_STAGE_TRANSFER_BIT
                       : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  vkCmdPipelineBarrier(command_buffer_, source_stage,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barrier);
  checkVk(vkEndCommandBuffer(command_buffer_), "vkEndCommandBuffer");
}

void VulkanApp::saveBmp(const std::string& path) {
  if (readback_mapped_ == nullptr) {
    throw std::runtime_error("readback buffer is not mapped");
  }
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("cannot open screenshot " + path);
  }
  const std::uint32_t row_bytes =
      ((extent_.width * 3U + 3U) / 4U) * 4U;
  const std::uint32_t pixel_bytes = row_bytes * extent_.height;
  const std::uint32_t file_bytes = 54U + pixel_bytes;
  auto write_u16 = [&output](std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 0xff),
                           static_cast<char>((value >> 8) & 0xff)};
    output.write(bytes, 2);
  };
  auto write_u32 = [&output](std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 0xff),
                           static_cast<char>((value >> 8) & 0xff),
                           static_cast<char>((value >> 16) & 0xff),
                           static_cast<char>((value >> 24) & 0xff)};
    output.write(bytes, 4);
  };
  output.put('B');
  output.put('M');
  write_u32(file_bytes);
  write_u16(0);
  write_u16(0);
  write_u32(54);
  write_u32(40);
  write_u32(extent_.width);
  write_u32(extent_.height);
  write_u16(1);
  write_u16(24);
  write_u32(0);
  write_u32(pixel_bytes);
  write_u32(2835);
  write_u32(2835);
  write_u32(0);
  write_u32(0);

  const auto* pixels = static_cast<const std::uint8_t*>(readback_mapped_);
  std::vector<std::uint8_t> row(row_bytes, 0);
  for (std::uint32_t output_y = 0; output_y < extent_.height; ++output_y) {
    const std::uint32_t y = extent_.height - 1U - output_y;
    for (std::uint32_t x = 0; x < extent_.width; ++x) {
      const std::size_t source =
          (static_cast<std::size_t>(y) * extent_.width + x) * 4U;
      const std::size_t target = static_cast<std::size_t>(x) * 3U;
      row[target] = pixels[source + (bgra_ ? 0U : 2U)];
      row[target + 1] = pixels[source + 1U];
      row[target + 2] = pixels[source + (bgra_ ? 2U : 0U)];
    }
    output.write(reinterpret_cast<const char*>(row.data()),
                 static_cast<std::streamsize>(row.size()));
  }
  if (!output) {
    throw std::runtime_error("failed while writing screenshot " + path);
  }
}

void VulkanApp::drawFrames(int frame_count, bool write_last_frame,
                           const std::string& screenshot_path) {
  for (int frame = 0; frame < frame_count; ++frame) {
    glfwPollEvents();
    std::uint32_t image_index = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, std::numeric_limits<std::uint64_t>::max(),
        image_available_, VK_NULL_HANDLE, &image_index);
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("vkAcquireNextImageKHR failed");
    }
    image_index_ = image_index;
    const bool copy = write_last_frame && frame + 1 == frame_count &&
                      !screenshot_path.empty();
    recordFrame(copy);

    VkSemaphore wait_semaphore = image_available_;
    VkSemaphore signal_semaphore = render_finished_;
    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &wait_semaphore;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_buffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &signal_semaphore;
    checkVk(vkQueueSubmit(queue_, 1, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit");
    checkVk(vkQueueWaitIdle(queue_), "vkQueueWaitIdle");
    if (copy) {
      saveBmp(screenshot_path);
    }

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &signal_semaphore;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &image_index;
    const VkResult present_result = vkQueuePresentKHR(queue_, &present);
    if (present_result != VK_SUCCESS && present_result != VK_SUBOPTIMAL_KHR) {
      throw std::runtime_error("vkQueuePresentKHR failed");
    }
    checkVk(vkQueueWaitIdle(queue_), "vkQueueWaitIdle(present)");
  }
}

void VulkanApp::destroySwapchainObjects() {
  for (VkFramebuffer framebuffer : framebuffers_) {
    if (framebuffer != VK_NULL_HANDLE) {
      vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
  }
  framebuffers_.clear();
  for (VkImageView view : image_views_) {
    if (view != VK_NULL_HANDLE) {
      vkDestroyImageView(device_, view, nullptr);
    }
  }
  image_views_.clear();
  if (swapchain_ != VK_NULL_HANDLE) {
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
}

}  // namespace

RenderResult renderModels(const std::map<std::string, Model>& models,
                          const std::set<std::string>& exception_objects,
                          const RenderOptions& options) {
  RenderResult result;
  result.attempted = true;
  try {
    const std::vector<Vertex> vertices = buildVertices(
        models, exception_objects, 600.0 / 960.0);
    VulkanApp app(vertices, options.visible);
    result.adapter = app.adapterName();
    result.width = app.width();
    result.height = app.height();
    const int frames = std::clamp(options.frames, 1, 1000);
    app.drawFrames(frames, !options.screenshot_path.empty(),
                   options.screenshot_path);
    result.ok = true;
    result.reason = "rendered " + app.formatName() + " swapchain frames";
    if (!options.screenshot_path.empty()) {
      result.screenshot_sha256 = sha256File(options.screenshot_path);
    }
  } catch (const std::exception& exception) {
    result.ok = false;
    result.reason = exception.what();
  }
  return result;
}

picojson::value renderResultJson(const RenderResult& result) {
  picojson::object object;
  object["attempted"] = picojson::value(result.attempted);
  object["ok"] = picojson::value(result.ok);
  object["skipped"] = picojson::value(result.skipped);
  object["backend"] = picojson::value(result.backend);
  object["adapter"] = picojson::value(result.adapter);
  object["reason"] = picojson::value(result.reason);
  object["width"] = picojson::value(static_cast<double>(result.width));
  object["height"] = picojson::value(static_cast<double>(result.height));
  object["screenshot_sha256"] =
      picojson::value(result.screenshot_sha256);
  return picojson::value(object);
}

}  // namespace bbox
