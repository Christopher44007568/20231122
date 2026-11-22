#include "rtsync/renderer.hpp"

#include "rtsync/image.hpp"
#include "rtsync/sha256.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;

namespace rtsync {
namespace {

constexpr std::uint32_t kWidth = 320;
constexpr std::uint32_t kHeight = 200;

struct Vertex {
  float x;
  float y;
  float r;
  float g;
  float b;
  float a;
};

std::string api_version_text(std::uint32_t version) {
  std::ostringstream out;
  out << VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "."
      << VK_VERSION_PATCH(version);
  return out.str();
}

void add_rect(std::vector<Vertex>* vertices, float x0, float y0, float x1,
              float y1, float r, float g, float b) {
  const Vertex a{x0, y0, r, g, b, 1.0f};
  const Vertex bb{x1, y0, r, g, b, 1.0f};
  const Vertex c{x1, y1, r, g, b, 1.0f};
  const Vertex d{x0, y1, r, g, b, 1.0f};
  vertices->push_back(a);
  vertices->push_back(bb);
  vertices->push_back(c);
  vertices->push_back(a);
  vertices->push_back(c);
  vertices->push_back(d);
}

std::vector<Vertex> golden_vertices() {
  std::vector<Vertex> vertices;
  add_rect(&vertices, -0.92f, -0.48f, -0.48f, 0.48f, 0.88f, 0.24f, 0.20f);
  add_rect(&vertices, -0.27f, -0.48f, 0.22f, 0.18f, 0.20f, 0.82f, 0.38f);
  add_rect(&vertices, 0.38f, -0.48f, 0.90f, 0.48f, 0.20f, 0.42f, 0.90f);
  return vertices;
}

std::vector<Vertex> observed_vertices() {
  std::vector<Vertex> vertices;
  add_rect(&vertices, -0.92f, -0.48f, -0.48f, 0.48f, 0.88f, 0.24f, 0.20f);
  add_rect(&vertices, -0.10f, -0.72f, 0.38f, -0.05f, 0.62f, 0.22f, 0.12f);
  add_rect(&vertices, 0.38f, -0.48f, 0.90f, 0.48f, 0.20f, 0.42f, 0.90f);
  return vertices;
}

bool read_binary(const std::string& path, std::vector<char>* bytes,
                 std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    *error = "cannot open shader " + path;
    return false;
  }
  bytes->assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
  if (bytes->empty() || bytes->size() % 4 != 0) {
    *error = "invalid shader bytecode " + path;
    return false;
  }
  return true;
}

bool find_memory_type(VkPhysicalDevice physical, std::uint32_t bits,
                      VkMemoryPropertyFlags properties,
                      std::uint32_t* index) {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    if ((bits & (1u << i)) != 0 &&
        (memory.memoryTypes[i].propertyFlags & properties) == properties) {
      *index = i;
      return true;
    }
  }
  return false;
}

bool create_buffer(VkPhysicalDevice physical, VkDevice device,
                   VkDeviceSize size, VkBufferUsageFlags usage,
                   VkMemoryPropertyFlags properties, VkBuffer* buffer,
                   VkDeviceMemory* memory, std::string* error) {
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  info.size = size;
  info.usage = usage;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (vkCreateBuffer(device, &info, nullptr, buffer) != VK_SUCCESS) {
    *error = "vkCreateBuffer failed";
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, *buffer, &requirements);
  std::uint32_t type = 0;
  if (!find_memory_type(physical, requirements.memoryTypeBits, properties,
                        &type)) {
    *error = "no suitable buffer memory type";
    return false;
  }
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &allocate, nullptr, memory) != VK_SUCCESS) {
    *error = "vkAllocateMemory(buffer) failed";
    return false;
  }
  vkBindBufferMemory(device, *buffer, *memory, 0);
  return true;
}

bool create_image(VkPhysicalDevice physical, VkDevice device,
                  VkImage* image, VkDeviceMemory* memory, std::string* error) {
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  info.imageType = VK_IMAGE_TYPE_2D;
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.extent = {kWidth, kHeight, 1};
  info.mipLevels = 1;
  info.arrayLayers = 1;
  info.samples = VK_SAMPLE_COUNT_1_BIT;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
               VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device, &info, nullptr, image) != VK_SUCCESS) {
    *error = "vkCreateImage failed";
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, *image, &requirements);
  std::uint32_t type = 0;
  if (!find_memory_type(physical, requirements.memoryTypeBits,
                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &type)) {
    *error = "no device-local image memory type";
    return false;
  }
  VkMemoryAllocateInfo allocate{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  allocate.allocationSize = requirements.size;
  allocate.memoryTypeIndex = type;
  if (vkAllocateMemory(device, &allocate, nullptr, memory) != VK_SUCCESS) {
    *error = "vkAllocateMemory(image) failed";
    return false;
  }
  vkBindImageMemory(device, *image, *memory, 0);
  return true;
}

}  // namespace

namespace {

struct Renderer {
  GLFWwindow* window = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  VkQueue queue = VK_NULL_HANDLE;
  std::uint32_t queue_family = 0;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkShaderModule vertex_shader = VK_NULL_HANDLE;
  VkShaderModule fragment_shader = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkImage color_image = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkBuffer vertex_buffer = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
  VkBuffer readback_buffer = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  VkFence fence = VK_NULL_HANDLE;
  std::uint32_t golden_vertices = 0;
  std::uint32_t observed_vertices = 0;
  std::uint32_t observed_offset = 0;
  bool glfw_up = false;

  ~Renderer() { destroy(); }

  void destroy() {
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    if (device != VK_NULL_HANDLE) {
      if (fence != VK_NULL_HANDLE) vkDestroyFence(device, fence, nullptr);
      if (readback_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, readback_buffer, nullptr);
      }
      if (readback_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, readback_memory, nullptr);
      }
      if (vertex_buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device, vertex_buffer, nullptr);
      }
      if (vertex_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, vertex_memory, nullptr);
      }
      if (pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(device, pipeline, nullptr);
      }
      if (pipeline_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
      }
      if (vertex_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, vertex_shader, nullptr);
      }
      if (fragment_shader != VK_NULL_HANDLE) {
        vkDestroyShaderModule(device, fragment_shader, nullptr);
      }
      if (framebuffer != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(device, framebuffer, nullptr);
      }
      if (color_view != VK_NULL_HANDLE) {
        vkDestroyImageView(device, color_view, nullptr);
      }
      if (color_image != VK_NULL_HANDLE) {
        vkDestroyImage(device, color_image, nullptr);
      }
      if (color_memory != VK_NULL_HANDLE) {
        vkFreeMemory(device, color_memory, nullptr);
      }
      if (render_pass != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device, render_pass, nullptr);
      }
      if (command_pool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(device, command_pool, nullptr);
      }
      vkDestroyDevice(device, nullptr);
    }
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
    if (window != nullptr) glfwDestroyWindow(window);
    if (glfw_up) glfwTerminate();
  }
};

VkShaderModule load_shader(VkDevice device, const std::string& path,
                           std::string* error) {
  std::vector<char> bytes;
  if (!read_binary(path, &bytes, error)) return VK_NULL_HANDLE;
  VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  info.codeSize = bytes.size();
  info.pCode = reinterpret_cast<const std::uint32_t*>(bytes.data());
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS) {
    *error = "vkCreateShaderModule failed for " + path;
    return VK_NULL_HANDLE;
  }
  return module;
}

bool create_render_pass(Renderer* renderer, std::string* error) {
  VkAttachmentDescription attachment{};
  attachment.format = VK_FORMAT_R8G8B8A8_UNORM;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference reference{
      0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &reference;
  VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  if (vkCreateRenderPass(renderer->device, &info, nullptr,
                         &renderer->render_pass) != VK_SUCCESS) {
    *error = "vkCreateRenderPass failed";
    return false;
  }
  return true;
}

bool create_color_target(Renderer* renderer, std::string* error) {
  if (!create_image(renderer->physical, renderer->device,
                    &renderer->color_image, &renderer->color_memory, error)) {
    return false;
  }
  VkImageViewCreateInfo view_info{
      VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  view_info.image = renderer->color_image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  if (vkCreateImageView(renderer->device, &view_info, nullptr,
                        &renderer->color_view) != VK_SUCCESS) {
    *error = "vkCreateImageView failed";
    return false;
  }
  VkFramebufferCreateInfo framebuffer_info{
      VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
  framebuffer_info.renderPass = renderer->render_pass;
  framebuffer_info.attachmentCount = 1;
  framebuffer_info.pAttachments = &renderer->color_view;
  framebuffer_info.width = kWidth;
  framebuffer_info.height = kHeight;
  framebuffer_info.layers = 1;
  if (vkCreateFramebuffer(renderer->device, &framebuffer_info, nullptr,
                          &renderer->framebuffer) != VK_SUCCESS) {
    *error = "vkCreateFramebuffer failed";
    return false;
  }
  return true;
}

bool build_pipeline(Renderer* renderer, std::string* error) {
  const std::string shader_dir = RTSYNC_SHADER_DIR;
  renderer->vertex_shader =
      load_shader(renderer->device, shader_dir + "/sync.vert.spv", error);
  renderer->fragment_shader =
      load_shader(renderer->device, shader_dir + "/sync.frag.spv", error);
  if (renderer->vertex_shader == VK_NULL_HANDLE ||
      renderer->fragment_shader == VK_NULL_HANDLE) {
    return false;
  }
  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = renderer->vertex_shader;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = renderer->fragment_shader;
  stages[1].pName = "main";
  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(Vertex);
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32_SFLOAT,
                   static_cast<std::uint32_t>(offsetof(Vertex, x))};
  attributes[1] = {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                   static_cast<std::uint32_t>(offsetof(Vertex, r))};
  VkPipelineVertexInputStateCreateInfo vertex_input{
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo assembly{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0f, 0.0f, static_cast<float>(kWidth),
                      static_cast<float>(kHeight), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, {kWidth, kHeight}};
  VkPipelineViewportStateCreateInfo viewport_state{
      VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  viewport_state.viewportCount = 1;
  viewport_state.pViewports = &viewport;
  viewport_state.scissorCount = 1;
  viewport_state.pScissors = &scissor;
  VkPipelineRasterizationStateCreateInfo raster{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;
  VkPipelineMultisampleStateCreateInfo multisample{
      VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask =
      VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
      VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  VkPipelineLayoutCreateInfo layout_info{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  if (vkCreatePipelineLayout(renderer->device, &layout_info, nullptr,
                             &renderer->pipeline_layout) != VK_SUCCESS) {
    *error = "vkCreatePipelineLayout failed";
    return false;
  }
  VkGraphicsPipelineCreateInfo info{
      VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertex_input;
  info.pInputAssemblyState = &assembly;
  info.pViewportState = &viewport_state;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pColorBlendState = &blend;
  info.layout = renderer->pipeline_layout;
  info.renderPass = renderer->render_pass;
  info.subpass = 0;
  if (vkCreateGraphicsPipelines(renderer->device, VK_NULL_HANDLE, 1, &info,
                                nullptr, &renderer->pipeline) != VK_SUCCESS) {
    *error = "vkCreateGraphicsPipelines failed";
    return false;
  }
  return true;
}

bool upload_scene_vertices(Renderer* renderer, std::string* error) {
  const std::vector<Vertex> golden = golden_vertices();
  const std::vector<Vertex> observed = observed_vertices();
  std::vector<Vertex> all = golden;
  all.insert(all.end(), observed.begin(), observed.end());
  renderer->golden_vertices = static_cast<std::uint32_t>(golden.size());
  renderer->observed_vertices = static_cast<std::uint32_t>(observed.size());
  renderer->observed_offset =
      static_cast<std::uint32_t>(golden.size() * sizeof(Vertex));
  const VkDeviceSize size = all.size() * sizeof(Vertex);
  if (!create_buffer(renderer->physical, renderer->device, size,
                     VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &renderer->vertex_buffer, &renderer->vertex_memory,
                     error)) {
    return false;
  }
  void* mapped = nullptr;
  if (vkMapMemory(renderer->device, renderer->vertex_memory, 0, size, 0,
                  &mapped) != VK_SUCCESS) {
    *error = "vkMapMemory(vertex) failed";
    return false;
  }
  std::memcpy(mapped, all.data(), static_cast<std::size_t>(size));
  vkUnmapMemory(renderer->device, renderer->vertex_memory);
  return true;
}

bool initialize_renderer(Renderer* renderer, RenderProbe* probe,
                         std::string* error) {
  if (glfwInit() == GLFW_TRUE) {
    renderer->glfw_up = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    renderer->window =
        glfwCreateWindow(64, 64, "rtsync-offscreen", nullptr, nullptr);
    probe->glfw_initialized = renderer->window != nullptr;
  }

  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "rtsync";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "rtsync";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo instance_info{
      VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  instance_info.pApplicationInfo = &app;
  if (vkCreateInstance(&instance_info, nullptr, &renderer->instance) !=
      VK_SUCCESS) {
    *error = "vkCreateInstance failed";
    return false;
  }
  std::uint32_t physical_count = 0;
  vkEnumeratePhysicalDevices(renderer->instance, &physical_count, nullptr);
  if (physical_count == 0) {
    *error = "no Vulkan physical device";
    return false;
  }
  std::vector<VkPhysicalDevice> physicals(physical_count);
  vkEnumeratePhysicalDevices(renderer->instance, &physical_count,
                             physicals.data());
  bool found = false;
  for (VkPhysicalDevice physical : physicals) {
    std::uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                             nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count,
                                             families.data());
    for (std::uint32_t i = 0; i < family_count; ++i) {
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
        renderer->physical = physical;
        renderer->queue_family = i;
        found = true;
        break;
      }
    }
    if (found) break;
  }
  if (!found) {
    *error = "no Vulkan graphics queue";
    return false;
  }
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(renderer->physical, &properties);
  probe->device_name = properties.deviceName;
  probe->api_version = api_version_text(properties.apiVersion);
  float priority = 1.0f;
  VkDeviceQueueCreateInfo queue_info{
      VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  queue_info.queueFamilyIndex = renderer->queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  if (vkCreateDevice(renderer->physical, &device_info, nullptr,
                     &renderer->device) != VK_SUCCESS) {
    *error = "vkCreateDevice failed";
    return false;
  }
  vkGetDeviceQueue(renderer->device, renderer->queue_family, 0,
                   &renderer->queue);
  VkCommandPoolCreateInfo pool_info{
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = renderer->queue_family;
  if (vkCreateCommandPool(renderer->device, &pool_info, nullptr,
                          &renderer->command_pool) != VK_SUCCESS) {
    *error = "vkCreateCommandPool failed";
    return false;
  }
  VkCommandBufferAllocateInfo command_info{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  command_info.commandPool = renderer->command_pool;
  command_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  command_info.commandBufferCount = 1;
  if (vkAllocateCommandBuffers(renderer->device, &command_info,
                               &renderer->command_buffer) != VK_SUCCESS) {
    *error = "vkAllocateCommandBuffers failed";
    return false;
  }
  VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  if (vkCreateFence(renderer->device, &fence_info, nullptr,
                    &renderer->fence) != VK_SUCCESS) {
    *error = "vkCreateFence failed";
    return false;
  }
  if (!create_render_pass(renderer, error) ||
      !create_color_target(renderer, error) ||
      !build_pipeline(renderer, error) ||
      !upload_scene_vertices(renderer, error)) {
    return false;
  }
  const VkDeviceSize readback_size =
      static_cast<VkDeviceSize>(kWidth) * kHeight * 4;
  if (!create_buffer(renderer->physical, renderer->device, readback_size,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &renderer->readback_buffer, &renderer->readback_memory,
                     error)) {
    return false;
  }
  probe->vulkan_initialized = true;
  probe->width = kWidth;
  probe->height = kHeight;
  return true;
}

bool render_one_frame(Renderer* renderer, std::uint32_t vertex_offset,
                      std::uint32_t vertex_count, const std::string& path,
                      RenderProbe* probe, std::string* hash,
                      std::string* error) {
  if (vkResetCommandBuffer(renderer->command_buffer, 0) != VK_SUCCESS) {
    *error = "vkResetCommandBuffer failed";
    return false;
  }
  VkCommandBufferBeginInfo begin{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (vkBeginCommandBuffer(renderer->command_buffer, &begin) != VK_SUCCESS) {
    *error = "vkBeginCommandBuffer failed";
    return false;
  }
  const VkClearValue clear{{
      0.035f, 0.045f, 0.070f, 1.0f,
  }};
  VkRenderPassBeginInfo pass{
      VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
  pass.renderPass = renderer->render_pass;
  pass.framebuffer = renderer->framebuffer;
  pass.renderArea.extent = {kWidth, kHeight};
  pass.clearValueCount = 1;
  pass.pClearValues = &clear;
  vkCmdBeginRenderPass(renderer->command_buffer, &pass,
                       VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(renderer->command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS, renderer->pipeline);
  const VkDeviceSize offset = vertex_offset;
  vkCmdBindVertexBuffers(renderer->command_buffer, 0, 1,
                         &renderer->vertex_buffer, &offset);
  vkCmdDraw(renderer->command_buffer, vertex_count, 1, 0, 0);
  vkCmdEndRenderPass(renderer->command_buffer);
  VkBufferImageCopy region{};
  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.layerCount = 1;
  region.imageExtent = {kWidth, kHeight, 1};
  vkCmdCopyImageToBuffer(renderer->command_buffer, renderer->color_image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         renderer->readback_buffer, 1, &region);
  if (vkEndCommandBuffer(renderer->command_buffer) != VK_SUCCESS) {
    *error = "vkEndCommandBuffer failed";
    return false;
  }
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &renderer->command_buffer;
  if (vkResetFences(renderer->device, 1, &renderer->fence) != VK_SUCCESS) {
    *error = "vkResetFences failed";
    return false;
  }
  if (vkQueueSubmit(renderer->queue, 1, &submit, renderer->fence) !=
      VK_SUCCESS) {
    *error = "vkQueueSubmit failed";
    return false;
  }
  const auto wait_start = std::chrono::steady_clock::now();
  if (vkWaitForFences(renderer->device, 1, &renderer->fence, VK_TRUE,
                      UINT64_MAX) != VK_SUCCESS) {
    *error = "vkWaitForFences failed";
    return false;
  }
  const auto wait_end = std::chrono::steady_clock::now();
  const VkResult fence_status =
      vkGetFenceStatus(renderer->device, renderer->fence);
  probe->fence_signaled = fence_status == VK_SUCCESS;
  probe->fence_waited_after_submit =
      fence_status == VK_SUCCESS && wait_end >= wait_start;

  const VkDeviceSize readback_size =
      static_cast<VkDeviceSize>(kWidth) * kHeight * 4;
  void* mapped = nullptr;
  if (vkMapMemory(renderer->device, renderer->readback_memory, 0,
                  readback_size, 0, &mapped) != VK_SUCCESS) {
    *error = "vkMapMemory(readback) failed";
    return false;
  }
  const auto* rgba = static_cast<const std::uint8_t*>(mapped);
  Image image;
  image.width = kWidth;
  image.height = kHeight;
  image.rgb.resize(static_cast<std::size_t>(kWidth) * kHeight * 3);
  for (std::size_t i = 0; i < static_cast<std::size_t>(kWidth) * kHeight;
       ++i) {
    image.rgb[i * 3] = rgba[i * 4];
    image.rgb[i * 3 + 1] = rgba[i * 4 + 1];
    image.rgb[i * 3 + 2] = rgba[i * 4 + 2];
  }
  vkUnmapMemory(renderer->device, renderer->readback_memory);
  if (!write_ppm(path, image, error)) return false;
  *hash = sha256_file_hex(path, error);
  return error->empty();
}

}  // namespace

bool render_demo_frames(const std::string& output_dir, RenderProbe* probe,
                        std::string* error) {
  std::error_code ec;
  fs::create_directories(output_dir, ec);
  if (ec) {
    *error = "cannot create render directory: " + ec.message();
    return false;
  }
  Renderer renderer;
  if (!initialize_renderer(&renderer, probe, error)) return false;
  const fs::path golden_path = fs::path(output_dir) / "golden.ppm";
  const fs::path observed_path = fs::path(output_dir) / "observed.ppm";
  if (!render_one_frame(&renderer, 0, renderer.golden_vertices,
                        golden_path.string(), probe,
                        &probe->golden_sha256, error)) {
    return false;
  }
  if (!render_one_frame(&renderer, renderer.observed_offset,
                        renderer.observed_vertices, observed_path.string(),
                        probe, &probe->observed_sha256, error)) {
    return false;
  }
  Json metadata = Json::object();
  metadata["schema"] = "rtsync.gpu_probe.v1";
  metadata["glfw_initialized"] = probe->glfw_initialized;
  metadata["vulkan_initialized"] = probe->vulkan_initialized;
  metadata["device_name"] = probe->device_name;
  metadata["api_version"] = probe->api_version;
  metadata["physical_devices_modelled_as"] = 2;
  metadata["command_buffer_id"] = probe->command_buffer_id;
  metadata["fence_id"] = probe->fence_id;
  metadata["fence_signaled"] = probe->fence_signaled;
  metadata["fence_waited_after_submit"] = probe->fence_waited_after_submit;
  metadata["width"] = probe->width;
  metadata["height"] = probe->height;
  metadata["golden_file"] = "golden.ppm";
  metadata["golden_sha256"] = probe->golden_sha256;
  metadata["observed_file"] = "observed.ppm";
  metadata["observed_sha256"] = probe->observed_sha256;
  metadata["sequence"] = Json::array(
      {"vkBeginCommandBuffer", "vkCmdBeginRenderPass", "vkCmdDraw",
       "vkCmdCopyImageToBuffer", "vkQueueSubmit", "vkWaitForFences",
       "vkGetFenceStatus=signaled"});
  return save_json((fs::path(output_dir) / "gpu_probe.json").string(),
                   metadata, error);
}

}  // namespace rtsync
