#include "vulkan_app.hpp"

#include <array>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>

namespace camtrak::gui {

namespace {

constexpr int kMaxFramesInFlight = 2;

#define VK_CHECK(call)                                                    \
  do {                                                                    \
    VkResult result__ = (call);                                           \
    if (result__ != VK_SUCCESS) {                                         \
      throw std::runtime_error(std::string("vulkan error ") +             \
                               std::to_string(result__) + " at " +       \
                               __FILE__ + ":" +                           \
                               std::to_string(__LINE__));                \
    }                                                                     \
  } while (0)

std::vector<char> readSpv(const std::string& path) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) throw std::runtime_error("cannot open shader: " + path);
  const std::streamsize size = stream.tellg();
  std::vector<char> buffer(static_cast<std::size_t>(size));
  stream.seekg(0);
  stream.read(buffer.data(), size);
  return buffer;
}

std::uint32_t findMemoryType(VkPhysicalDevice physical_device,
                             std::uint32_t type_filter,
                             VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory_properties;
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
  for (std::uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
    if ((type_filter & (1u << i)) &&
        (memory_properties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  throw std::runtime_error("no suitable memory type");
}

}  // namespace

void VulkanApp::ensureVertexCapacity(std::size_t byte_count) {
  if (byte_count <= vertex_buffer_capacity_) return;
  if (vertex_buffer_ != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, vertex_buffer_, nullptr);
    vkFreeMemory(device_, vertex_memory_, nullptr);
  }
  vertex_buffer_capacity_ = std::max(byte_count * 2, std::size_t(4096));
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = vertex_buffer_capacity_;
  buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VK_CHECK(vkCreateBuffer(device_, &buffer_info, nullptr, &vertex_buffer_));
  VkMemoryRequirements requirements;
  vkGetBufferMemoryRequirements(device_, vertex_buffer_, &requirements);
  VkMemoryAllocateInfo alloc_info{};
  alloc_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc_info.allocationSize = requirements.size;
  alloc_info.memoryTypeIndex = findMemoryType(
      physical_device_, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  VK_CHECK(vkAllocateMemory(device_, &alloc_info, nullptr, &vertex_memory_));
  VK_CHECK(vkBindBufferMemory(device_, vertex_buffer_, vertex_memory_, 0));
}

VulkanApp::VulkanApp() = default;

VulkanApp::~VulkanApp() { shutdown(); }

VulkanApp::QueueFamilies VulkanApp::findQueueFamilies(
    VkPhysicalDevice device) const {
  QueueFamilies families;
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> props(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());
  for (std::uint32_t i = 0; i < count; ++i) {
    if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      families.graphics = i;
    }
    VkBool32 present_support = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_,
                                         &present_support);
    if (present_support) families.present = i;
    if (families.complete()) break;
  }
  return families;
}

bool VulkanApp::initialize(GLFWwindow* window) {
  window_ = window;
  VkApplicationInfo app_info{};
  app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app_info.pApplicationName = "camtrak";
  app_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.pEngineName = "camtrak";
  app_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app_info.apiVersion = VK_API_VERSION_1_2;

  std::uint32_t glfw_extension_count = 0;
  const char** glfw_extensions =
      glfwGetRequiredInstanceExtensions(&glfw_extension_count);
  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app_info;
  instance_info.enabledExtensionCount = glfw_extension_count;
  instance_info.ppEnabledExtensionNames = glfw_extensions;
  if (vkCreateInstance(&instance_info, nullptr, &instance_) != VK_SUCCESS) {
    std::cerr << "failed to create Vulkan instance\n";
    return false;
  }

  if (glfwVulkanSupported() == GLFW_FALSE ||
      glfwCreateWindowSurface(instance_, window_, nullptr, &surface_) !=
          VK_SUCCESS) {
    std::cerr << "failed to create window surface\n";
    return false;
  }

  std::uint32_t device_count = 0;
  vkEnumeratePhysicalDevices(instance_, &device_count, nullptr);
  if (device_count == 0) {
    std::cerr << "no Vulkan-capable physical device found\n";
    return false;
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  vkEnumeratePhysicalDevices(instance_, &device_count, devices.data());
  for (VkPhysicalDevice device : devices) {
    QueueFamilies families = findQueueFamilies(device);
    if (!families.complete()) continue;
    physical_device_ = device;
    break;
  }
  if (physical_device_ == VK_NULL_HANDLE) {
    std::cerr << "no device with graphics+present support\n";
    return false;
  }

  QueueFamilies families = findQueueFamilies(physical_device_);
  const std::set<std::uint32_t> unique = {families.graphics,
                                          families.present};
  std::vector<VkDeviceQueueCreateInfo> queue_infos;
  const float priority = 1.0f;
  for (std::uint32_t family : unique) {
    VkDeviceQueueCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    info.queueFamilyIndex = family;
    info.queueCount = 1;
    info.pQueuePriorities = &priority;
    queue_infos.push_back(info);
  }
  const std::array<const char*, 1> device_extensions = {
      VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount =
      static_cast<std::uint32_t>(queue_infos.size());
  device_info.pQueueCreateInfos = queue_infos.data();
  device_info.enabledExtensionCount =
      static_cast<std::uint32_t>(device_extensions.size());
  device_info.ppEnabledExtensionNames = device_extensions.data();
  VK_CHECK(vkCreateDevice(physical_device_, &device_info, nullptr, &device_));
  vkGetDeviceQueue(device_, families.graphics, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, families.present, 0, &present_queue_);

  createSwapchain();
  createImageViews();
  createRenderPass();
  createPipeline();
  createFramebuffers();
  createCommandPool();
  createCommandBuffers();
  createSyncObjects();
  return true;
}

void VulkanApp::createSwapchain() {
  VkSurfaceCapabilitiesKHR capabilities{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device_, surface_,
                                            &capabilities);
  std::uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device_, surface_,
                                       &format_count, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (const auto& format : formats) {
    if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
        format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = format;
      break;
    }
  }
  swapchain_format_ = chosen.format;
  extent_ = capabilities.currentExtent;
  if (extent_.width == UINT32_MAX) {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    extent_.width = static_cast<std::uint32_t>(
        std::clamp(width, static_cast<int>(capabilities.minImageExtent.width),
                   static_cast<int>(capabilities.maxImageExtent.width)));
    extent_.height = static_cast<std::uint32_t>(
        std::clamp(height, static_cast<int>(capabilities.minImageExtent.height),
                   static_cast<int>(capabilities.maxImageExtent.height)));
  }
  std::uint32_t image_count = capabilities.minImageCount + 1;
  if (capabilities.maxImageCount > 0) {
    image_count = std::min(image_count, capabilities.maxImageCount);
  }
  const QueueFamilies families = findQueueFamilies(physical_device_);
  VkSwapchainCreateInfoKHR info{};
  info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  info.surface = surface_;
  info.minImageCount = image_count;
  info.imageFormat = chosen.format;
  info.imageColorSpace = chosen.colorSpace;
  info.imageExtent = extent_;
  info.imageArrayLayers = 1;
  info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (families.graphics != families.present) {
    const std::uint32_t indices[2] = {families.graphics, families.present};
    info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    info.queueFamilyIndexCount = 2;
    info.pQueueFamilyIndices = indices;
  } else {
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  info.preTransform = capabilities.currentTransform;
  info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  info.clipped = VK_TRUE;
  VK_CHECK(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_));

  std::uint32_t actual_count = 0;
  vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count, nullptr);
  swapchain_images_.resize(actual_count);
  vkGetSwapchainImagesKHR(device_, swapchain_, &actual_count,
                          swapchain_images_.data());
}

void VulkanApp::createImageViews() {
  image_views_.resize(swapchain_images_.size());
  for (std::size_t i = 0; i < swapchain_images_.size(); ++i) {
    VkImageViewCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    info.image = swapchain_images_[i];
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = swapchain_format_;
    info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    info.subresourceRange.baseMipLevel = 0;
    info.subresourceRange.levelCount = 1;
    info.subresourceRange.baseArrayLayer = 0;
    info.subresourceRange.layerCount = 1;
    VK_CHECK(vkCreateImageView(device_, &info, nullptr, &image_views_[i]));
  }
}

void VulkanApp::createRenderPass() {
  VkAttachmentDescription color{};
  color.format = swapchain_format_;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference color_ref{};
  color_ref.attachment = 0;
  color_ref.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &color;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  VK_CHECK(vkCreateRenderPass(device_, &info, nullptr, &render_pass_));
}

void VulkanApp::createPipeline() {
  const std::vector<char> vert_spv =
      readSpv(std::string(CAMTRAK_SHADER_DIR) + "/line.vert.spv");
  const std::vector<char> frag_spv =
      readSpv(std::string(CAMTRAK_SHADER_DIR) + "/line.frag.spv");

  VkShaderModuleCreateInfo module_info{};
  module_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  module_info.codeSize = vert_spv.size();
  module_info.pCode =
      reinterpret_cast<const std::uint32_t*>(vert_spv.data());
  VkShaderModule vert_module;
  VK_CHECK(vkCreateShaderModule(device_, &module_info, nullptr,
                                &vert_module));
  module_info.codeSize = frag_spv.size();
  module_info.pCode =
      reinterpret_cast<const std::uint32_t*>(frag_spv.data());
  VkShaderModule frag_module;
  VK_CHECK(vkCreateShaderModule(device_, &module_info, nullptr,
                                &frag_module));

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert_module;
  stages[0].pName = "main";
  stages[1].sType =
      VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag_module;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vec3);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attribute{};
  attribute.location = 0;
  attribute.binding = 0;
  attribute.format = VK_FORMAT_R32G32B32_SFLOAT;
  attribute.offset = 0;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 1;
  vertex_input.pVertexAttributeDescriptions = &attribute;
  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  assembly.primitiveRestartEnable = VK_FALSE;

  VkViewport viewport{};
  viewport.width = static_cast<float>(extent_.width);
  viewport.height = static_cast<float>(extent_.height);
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{{0, 0}, extent_};
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
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType =
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineColorBlendAttachmentState blend{};
  blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                         VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo color_blend{};
  color_blend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  color_blend.attachmentCount = 1;
  color_blend.pAttachments = &blend;

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_range.offset = 0;
  push_range.size = sizeof(PushConstants);
  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  VK_CHECK(vkCreatePipelineLayout(device_, &layout_info, nullptr,
                                  &pipeline_layout_));

  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pColorBlendState = &color_blend;
  pipeline_info.layout = pipeline_layout_;
  pipeline_info.renderPass = render_pass_;
  pipeline_info.subpass = 0;
  VK_CHECK(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1,
                                     &pipeline_info, nullptr, &pipeline_));
  vkDestroyShaderModule(device_, vert_module, nullptr);
  vkDestroyShaderModule(device_, frag_module, nullptr);
}

void VulkanApp::createFramebuffers() {
  framebuffers_.resize(image_views_.size());
  for (std::size_t i = 0; i < image_views_.size(); ++i) {
    VkImageView attachments[1] = {image_views_[i]};
    VkFramebufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    info.renderPass = render_pass_;
    info.attachmentCount = 1;
    info.pAttachments = attachments;
    info.width = extent_.width;
    info.height = extent_.height;
    info.layers = 1;
    VK_CHECK(vkCreateFramebuffer(device_, &info, nullptr,
                                 &framebuffers_[i]));
  }
}

void VulkanApp::createCommandPool() {
  const QueueFamilies families = findQueueFamilies(physical_device_);
  VkCommandPoolCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  info.queueFamilyIndex = families.graphics;
  VK_CHECK(vkCreateCommandPool(device_, &info, nullptr, &command_pool_));
}

void VulkanApp::createCommandBuffers() {
  command_buffers_.resize(kMaxFramesInFlight);
  VkCommandBufferAllocateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  info.commandPool = command_pool_;
  info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  info.commandBufferCount = kMaxFramesInFlight;
  VK_CHECK(vkAllocateCommandBuffers(device_, &info,
                                    command_buffers_.data()));
}

void VulkanApp::createSyncObjects() {
  image_available_.resize(kMaxFramesInFlight);
  render_finished_.resize(kMaxFramesInFlight);
  in_flight_.resize(kMaxFramesInFlight);
  VkSemaphoreCreateInfo semaphore_info{};
  semaphore_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (int i = 0; i < kMaxFramesInFlight; ++i) {
    VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                               &image_available_[i]));
    VK_CHECK(vkCreateSemaphore(device_, &semaphore_info, nullptr,
                               &render_finished_[i]));
    VK_CHECK(vkCreateFence(device_, &fence_info, nullptr, &in_flight_[i]));
  }
}

void VulkanApp::recordCommands(VkCommandBuffer command_buffer,
                               const std::vector<RenderLine>& lines) {
  std::size_t total_vertices = 0;
  for (const RenderLine& line : lines) total_vertices += line.points.size();
  ensureVertexCapacity(total_vertices * sizeof(Vec3));
  if (total_vertices > 0) {
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(device_, vertex_memory_, 0,
                         total_vertices * sizeof(Vec3), 0, &mapped));
    std::size_t offset = 0;
    for (const RenderLine& line : lines) {
      std::memcpy(static_cast<char*>(mapped) + offset * sizeof(Vec3),
                  line.points.data(),
                  line.points.size() * sizeof(Vec3));
      offset += line.points.size();
    }
    vkUnmapMemory(device_, vertex_memory_);
  }

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  VK_CHECK(vkBeginCommandBuffer(command_buffer, &begin_info));

  VkClearValue clear;
  clear.color = {{0.07f, 0.08f, 0.10f, 1.0f}};
  VkRenderPassBeginInfo render_info{};
  render_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  render_info.renderPass = render_pass_;
  render_info.framebuffer = framebuffers_[image_index_];
  render_info.renderArea.extent = extent_;
  render_info.clearValueCount = 1;
  render_info.pClearValues = &clear;
  vkCmdBeginRenderPass(command_buffer, &render_info,
                       VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    pipeline_);
  VkDeviceSize binding_offset = 0;
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer_,
                         &binding_offset);

  std::uint32_t vertex_offset = 0;
  for (const RenderLine& line : lines) {
    if (line.points.size() < 2) continue;
    PushConstants constants{};
    std::memset(constants.view_proj, 0, sizeof(constants.view_proj));
    // Simple fixed orthographic projection of the (x, y, z) trajectory.
    const float aspect =
        static_cast<float>(extent_.width) /
        static_cast<float>(extent_.height == 0 ? 1 : extent_.height);
    const float scale_x = 1.0f / std::max(1.0f, aspect);
    const float scale_y = 1.0f;
    const float m[16] = {
        scale_x, 0, 0, 0,
        0, scale_y, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};
    std::memcpy(constants.view_proj, m, sizeof(m));
    constants.color[0] = line.r;
    constants.color[1] = line.g;
    constants.color[2] = line.b;
    constants.color[3] = 1.0f;
    vkCmdPushConstants(command_buffer, pipeline_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0,
                       sizeof(PushConstants), &constants);
    vkCmdDraw(command_buffer,
              static_cast<std::uint32_t>(line.points.size()), 1,
              vertex_offset, 0);
    vertex_offset += static_cast<std::uint32_t>(line.points.size());
  }
  vkCmdEndRenderPass(command_buffer);
  VK_CHECK(vkEndCommandBuffer(command_buffer));
}

void VulkanApp::drawFrame(const std::vector<RenderLine>& lines) {
  vkWaitForFences(device_, 1, &in_flight_[frame_index_], VK_TRUE,
                  UINT64_MAX);
  VK_CHECK(vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX,
                                 image_available_[frame_index_],
                                 VK_NULL_HANDLE, &image_index_));
  vkResetFences(device_, 1, &in_flight_[frame_index_]);
  vkResetCommandBuffer(command_buffers_[frame_index_], 0);
  recordCommands(command_buffers_[frame_index_], lines);

  VkSemaphore wait_semaphores[1] = {image_available_[frame_index_]};
  VkPipelineStageFlags wait_stages[1] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.waitSemaphoreCount = 1;
  submit_info.pWaitSemaphores = wait_semaphores;
  submit_info.pWaitDstStageMask = wait_stages;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffers_[frame_index_];
  VkSemaphore signal_semaphores[1] = {render_finished_[frame_index_]};
  submit_info.signalSemaphoreCount = 1;
  submit_info.pSignalSemaphores = signal_semaphores;
  VK_CHECK(vkQueueSubmit(graphics_queue_, 1, &submit_info,
                         in_flight_[frame_index_]));

  VkSwapchainKHR swapchains[1] = {swapchain_};
  VkPresentInfoKHR present_info{};
  present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
  present_info.waitSemaphoreCount = 1;
  present_info.pWaitSemaphores = signal_semaphores;
  present_info.swapchainCount = 1;
  present_info.pSwapchains = swapchains;
  present_info.pImageIndices = &image_index_;
  vkQueuePresentKHR(present_queue_, &present_info);
  frame_index_ = (frame_index_ + 1) % kMaxFramesInFlight;
}

void VulkanApp::shutdown() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    for (int i = 0; i < kMaxFramesInFlight; ++i) {
      if (!image_available_.empty())
        vkDestroySemaphore(device_, image_available_[i], nullptr);
      if (!render_finished_.empty())
        vkDestroySemaphore(device_, render_finished_[i], nullptr);
      if (!in_flight_.empty())
        vkDestroyFence(device_, in_flight_[i], nullptr);
    }
    if (command_pool_ != VK_NULL_HANDLE)
      vkDestroyCommandPool(device_, command_pool_, nullptr);
    if (vertex_buffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, vertex_buffer_, nullptr);
      vkFreeMemory(device_, vertex_memory_, nullptr);
    }
    for (VkFramebuffer framebuffer : framebuffers_)
      vkDestroyFramebuffer(device_, framebuffer, nullptr);
    if (pipeline_ != VK_NULL_HANDLE)
      vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipeline_layout_ != VK_NULL_HANDLE)
      vkDestroyPipelineLayout(device_, pipeline_layout_, nullptr);
    if (render_pass_ != VK_NULL_HANDLE)
      vkDestroyRenderPass(device_, render_pass_, nullptr);
    for (VkImageView view : image_views_)
      vkDestroyImageView(device_, view, nullptr);
    if (swapchain_ != VK_NULL_HANDLE)
      vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE)
    vkDestroySurfaceKHR(instance_, surface_, nullptr);
  if (instance_ != VK_NULL_HANDLE)
    vkDestroyInstance(instance_, nullptr);
  instance_ = VK_NULL_HANDLE;
  surface_ = VK_NULL_HANDLE;
  physical_device_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  swapchain_ = VK_NULL_HANDLE;
  render_pass_ = VK_NULL_HANDLE;
  pipeline_layout_ = VK_NULL_HANDLE;
  pipeline_ = VK_NULL_HANDLE;
  command_pool_ = VK_NULL_HANDLE;
  vertex_buffer_ = VK_NULL_HANDLE;
  vertex_memory_ = VK_NULL_HANDLE;
  vertex_buffer_capacity_ = 0;
  frame_index_ = 0;
  image_index_ = 0;
}

}  // namespace camtrak::gui
