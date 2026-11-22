#include "renderer.hpp"

#include "hash.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace msphere {
namespace {

constexpr uint32_t kSphereLatitudeSegments = 32;
constexpr uint32_t kSphereLongitudeSegments = 64;
constexpr float kSphereRadius = 0.42F;
constexpr uint32_t kTimestampQueryCount = 2;
constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

struct Vertex {
  float position[3];
  float normal[3];
};

struct Instance {
  float position[3];
  uint32_t material_index;
};

struct GpuMaterial {
  float base_color_roughness[4];
  float properties[4];
};

struct GridLayout {
  uint32_t columns = 1;
  uint32_t rows = 1;
  float half_width = 1.0F;
  float half_height = 1.0F;
  std::vector<Instance> instances;
};

struct Renderer {
  GLFWwindow* window = nullptr;
  VkInstance instance = VK_NULL_HANDLE;
  VkPhysicalDevice physical_device = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
  uint32_t queue_family = 0;
  VkQueue queue = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor_set = VK_NULL_HANDLE;
  VkShaderModule vertex_shader = VK_NULL_HANDLE;
  VkShaderModule fragment_shader = VK_NULL_HANDLE;

  VkBuffer vertex_buffer = VK_NULL_HANDLE;
  VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
  VkBuffer index_buffer = VK_NULL_HANDLE;
  VkDeviceMemory index_memory = VK_NULL_HANDLE;
  VkBuffer instance_buffer = VK_NULL_HANDLE;
  VkDeviceMemory instance_memory = VK_NULL_HANDLE;
  VkBuffer material_buffer = VK_NULL_HANDLE;
  VkDeviceMemory material_memory = VK_NULL_HANDLE;
  VkBuffer readback_buffer = VK_NULL_HANDLE;
  VkDeviceMemory readback_memory = VK_NULL_HANDLE;
  void* instance_mapped = nullptr;
  void* material_mapped = nullptr;
  void* readback_mapped = nullptr;

  VkImage reflection_image = VK_NULL_HANDLE;
  VkDeviceMemory reflection_memory = VK_NULL_HANDLE;
  VkImageView reflection_view = VK_NULL_HANDLE;
  VkSampler reflection_sampler = VK_NULL_HANDLE;
  VkImage color_image = VK_NULL_HANDLE;
  VkDeviceMemory color_memory = VK_NULL_HANDLE;
  VkImageView color_view = VK_NULL_HANDLE;
  VkImage depth_image = VK_NULL_HANDLE;
  VkDeviceMemory depth_memory = VK_NULL_HANDLE;
  VkImageView depth_view = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkQueryPool query_pool = VK_NULL_HANDLE;

  VkExtent2D extent{};
  uint32_t index_count = 0;
  uint32_t material_count = 0;
  GridLayout grid;
  RendererInfo info;
  std::vector<BufferEvidence> buffer_evidence;
  std::vector<ImageEvidence> image_evidence;
  bool timestamp_supported = false;
  std::string error;

  ~Renderer() { destroy(); }

  bool init(const ParsedInput& input, const RenderOptions& options);
  void destroy();
  bool render_frame(const std::string& name,
                    const std::vector<Material>& materials,
                    uint64_t sequence, FrameEvidence* frame);

  bool create_instance_and_device();
  bool create_swapchainless_targets();
  bool create_pipeline();
  bool create_mesh_and_instances(const ParsedInput& input);
  bool create_material_buffer(const ParsedInput& input);
  bool create_reflection_texture(const ParsedInput& input);
  bool create_command_pool();
  bool create_readback_buffer();
  bool create_query_pool();
  bool upload_reflection_texture(const ParsedInput& input);
  bool allocate_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                       VkMemoryPropertyFlags properties, VkBuffer* buffer,
                       VkDeviceMemory* memory);
  bool allocate_image(VkFormat format, VkExtent2D size, VkImageUsageFlags usage,
                      VkImage* image, VkDeviceMemory* memory);
  uint32_t find_memory_type(uint32_t type_bits,
                            VkMemoryPropertyFlags properties) const;
  VkCommandBuffer begin_one_time();
  bool end_one_time(VkCommandBuffer command_buffer);
};

std::string g_last_glfw_error;

void glfw_error_callback(int code, const char* description) {
  g_last_glfw_error = "GLFW error " + std::to_string(code) + ": " +
                      (description != nullptr ? description : "unknown");
}

std::string vk_result_name(VkResult result) {
  switch (result) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_EVENT_SET:
      return "VK_EVENT_SET";
    case VK_EVENT_RESET:
      return "VK_EVENT_RESET";
    case VK_INCOMPLETE:
      return "VK_INCOMPLETE";
    case VK_ERROR_OUT_OF_HOST_MEMORY:
      return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
      return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED:
      return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST:
      return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED:
      return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT:
      return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT:
      return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT:
      return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER:
      return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS:
      return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED:
      return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL:
      return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_SURFACE_LOST_KHR:
      return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR:
      return "VK_ERROR_OUT_OF_DATE_KHR";
    case VK_ERROR_UNKNOWN:
      return "VK_ERROR_UNKNOWN";
    default:
      return "VK_ERROR_" + std::to_string(static_cast<int>(result));
  }
}

bool vk_ok(VkResult result, const char* operation, std::string* error) {
  if (result == VK_SUCCESS) {
    return true;
  }
  if (error != nullptr) {
    *error = std::string(operation) + " failed: " + vk_result_name(result);
  }
  return false;
}

std::string format_version(uint32_t version) {
  std::ostringstream out;
  out << VK_VERSION_MAJOR(version) << '.' << VK_VERSION_MINOR(version) << '.'
      << VK_VERSION_PATCH(version);
  return out.str();
}

std::string bytes_to_hex(const uint8_t* data, size_t size) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (size_t i = 0; i < size; ++i) {
    out << std::setw(2) << static_cast<unsigned>(data[i]);
  }
  return out.str();
}

uint32_t float_bits(float value) {
  uint32_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

GridLayout make_grid(uint32_t material_count) {
  GridLayout layout;
  const double ideal_columns =
      std::sqrt(static_cast<double>(material_count) * 16.0 / 9.0);
  layout.columns =
      std::max(1U, std::min(8U, static_cast<uint32_t>(std::ceil(ideal_columns))));
  layout.rows = (material_count + layout.columns - 1U) / layout.columns;
  layout.half_height =
      std::max(1.20F, (static_cast<float>(layout.rows - 1U) * 0.5F) + 0.78F);
  layout.half_width = layout.half_height * 16.0F / 9.0F;
  const float required_half_width =
      (static_cast<float>(layout.columns - 1U) * 0.5F) + 0.78F;
  if (layout.half_width < required_half_width) {
    layout.half_width = required_half_width;
    layout.half_height = layout.half_width * 9.0F / 16.0F;
  }
  layout.instances.resize(material_count);
  for (uint32_t i = 0; i < material_count; ++i) {
    const uint32_t column = i % layout.columns;
    const uint32_t row = i / layout.columns;
    layout.instances[i].position[0] =
        (static_cast<float>(column) -
         (static_cast<float>(layout.columns) - 1.0F) * 0.5F);
    layout.instances[i].position[1] =
        ((static_cast<float>(layout.rows) - 1.0F) * 0.5F -
         static_cast<float>(row));
    layout.instances[i].position[2] = 0.0F;
    layout.instances[i].material_index = i;
  }
  return layout;
}

std::vector<uint8_t> read_binary_file(const std::string& path,
                                      std::string* error) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot open shader: " + path;
    }
    return {};
  }
  const std::streamsize size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(data.data()), size);
  if (!stream) {
    if (error != nullptr) {
      *error = "cannot read shader: " + path;
    }
    return {};
  }
  return data;
}

const char* glfw_platform_name(int platform) {
  switch (platform) {
    case GLFW_PLATFORM_WIN32:
      return "Win32";
    case GLFW_PLATFORM_COCOA:
      return "Cocoa";
    case GLFW_PLATFORM_WAYLAND:
      return "Wayland";
    case GLFW_PLATFORM_X11:
      return "X11";
    case GLFW_PLATFORM_NULL:
      return "Null";
    default:
      return "Unknown";
  }
}

void Renderer::destroy() {
  if (device != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device);
  }
  if (query_pool != VK_NULL_HANDLE) {
    vkDestroyQueryPool(device, query_pool, nullptr);
    query_pool = VK_NULL_HANDLE;
  }
  if (framebuffer != VK_NULL_HANDLE) {
    vkDestroyFramebuffer(device, framebuffer, nullptr);
    framebuffer = VK_NULL_HANDLE;
  }
  if (pipeline != VK_NULL_HANDLE) {
    vkDestroyPipeline(device, pipeline, nullptr);
    pipeline = VK_NULL_HANDLE;
  }
  if (render_pass != VK_NULL_HANDLE) {
    vkDestroyRenderPass(device, render_pass, nullptr);
    render_pass = VK_NULL_HANDLE;
  }
  if (pipeline_layout != VK_NULL_HANDLE) {
    vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    pipeline_layout = VK_NULL_HANDLE;
  }
  if (descriptor_pool != VK_NULL_HANDLE) {
    vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    descriptor_pool = VK_NULL_HANDLE;
  }
  if (descriptor_layout != VK_NULL_HANDLE) {
    vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    descriptor_layout = VK_NULL_HANDLE;
  }
  if (vertex_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, vertex_shader, nullptr);
    vertex_shader = VK_NULL_HANDLE;
  }
  if (fragment_shader != VK_NULL_HANDLE) {
    vkDestroyShaderModule(device, fragment_shader, nullptr);
    fragment_shader = VK_NULL_HANDLE;
  }
  if (reflection_sampler != VK_NULL_HANDLE) {
    vkDestroySampler(device, reflection_sampler, nullptr);
    reflection_sampler = VK_NULL_HANDLE;
  }
  if (reflection_view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, reflection_view, nullptr);
    reflection_view = VK_NULL_HANDLE;
  }
  if (reflection_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, reflection_image, nullptr);
    reflection_image = VK_NULL_HANDLE;
  }
  if (reflection_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, reflection_memory, nullptr);
    reflection_memory = VK_NULL_HANDLE;
  }
  if (color_view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, color_view, nullptr);
    color_view = VK_NULL_HANDLE;
  }
  if (color_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, color_image, nullptr);
    color_image = VK_NULL_HANDLE;
  }
  if (color_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, color_memory, nullptr);
    color_memory = VK_NULL_HANDLE;
  }
  if (depth_view != VK_NULL_HANDLE) {
    vkDestroyImageView(device, depth_view, nullptr);
    depth_view = VK_NULL_HANDLE;
  }
  if (depth_image != VK_NULL_HANDLE) {
    vkDestroyImage(device, depth_image, nullptr);
    depth_image = VK_NULL_HANDLE;
  }
  if (depth_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device, depth_memory, nullptr);
    depth_memory = VK_NULL_HANDLE;
  }
  if (material_mapped != nullptr) {
    vkUnmapMemory(device, material_memory);
    material_mapped = nullptr;
  }
  if (instance_mapped != nullptr) {
    vkUnmapMemory(device, instance_memory);
    instance_mapped = nullptr;
  }
  if (readback_mapped != nullptr) {
    vkUnmapMemory(device, readback_memory);
    readback_mapped = nullptr;
  }
  const std::array<std::pair<VkBuffer, VkDeviceMemory>, 5> buffers = {{
      {vertex_buffer, vertex_memory},
      {index_buffer, index_memory},
      {instance_buffer, instance_memory},
      {material_buffer, material_memory},
      {readback_buffer, readback_memory},
  }};
  for (const auto& entry : buffers) {
    if (entry.first != VK_NULL_HANDLE) {
      vkDestroyBuffer(device, entry.first, nullptr);
    }
    if (entry.second != VK_NULL_HANDLE) {
      vkFreeMemory(device, entry.second, nullptr);
    }
  }
  vertex_buffer = index_buffer = instance_buffer = material_buffer =
      readback_buffer = VK_NULL_HANDLE;
  vertex_memory = index_memory = instance_memory = material_memory =
      readback_memory = VK_NULL_HANDLE;
  if (command_pool != VK_NULL_HANDLE) {
    vkDestroyCommandPool(device, command_pool, nullptr);
    command_pool = VK_NULL_HANDLE;
  }
  if (device != VK_NULL_HANDLE) {
    vkDestroyDevice(device, nullptr);
    device = VK_NULL_HANDLE;
  }
  if (instance != VK_NULL_HANDLE) {
    vkDestroyInstance(instance, nullptr);
    instance = VK_NULL_HANDLE;
  }
  if (window != nullptr) {
    glfwDestroyWindow(window);
    window = nullptr;
  }
  glfwTerminate();
}

uint32_t Renderer::find_memory_type(
    uint32_t type_bits, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory_properties{};
  vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties);
  for (uint32_t i = 0; i < memory_properties.memoryTypeCount; ++i) {
    if ((type_bits & (1U << i)) != 0U &&
        (memory_properties.memoryTypes[i].propertyFlags & properties) ==
            properties) {
      return i;
    }
  }
  return std::numeric_limits<uint32_t>::max();
}

bool Renderer::allocate_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                               VkMemoryPropertyFlags properties,
                               VkBuffer* buffer, VkDeviceMemory* memory) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  if (!vk_ok(vkCreateBuffer(device, &buffer_info, nullptr, buffer),
             "vkCreateBuffer", &error)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, *buffer, &requirements);
  const uint32_t memory_type =
      find_memory_type(requirements.memoryTypeBits, properties);
  if (memory_type == std::numeric_limits<uint32_t>::max()) {
    error = "no compatible buffer memory type";
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (!vk_ok(vkAllocateMemory(device, &allocate_info, nullptr, memory),
             "vkAllocateMemory(buffer)", &error)) {
    return false;
  }
  if (!vk_ok(vkBindBufferMemory(device, *buffer, *memory, 0),
             "vkBindBufferMemory", &error)) {
    return false;
  }
  return true;
}

bool Renderer::allocate_image(VkFormat format, VkExtent2D size,
                              VkImageUsageFlags usage, VkImage* image,
                              VkDeviceMemory* memory) {
  VkImageCreateInfo image_info{};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = format;
  image_info.extent = {size.width, size.height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (!vk_ok(vkCreateImage(device, &image_info, nullptr, image),
             "vkCreateImage", &error)) {
    return false;
  }
  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, *image, &requirements);
  uint32_t memory_type =
      find_memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (memory_type == std::numeric_limits<uint32_t>::max()) {
    memory_type = find_memory_type(requirements.memoryTypeBits, 0);
  }
  if (memory_type == std::numeric_limits<uint32_t>::max()) {
    error = "no compatible image memory type";
    return false;
  }
  VkMemoryAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocate_info.allocationSize = requirements.size;
  allocate_info.memoryTypeIndex = memory_type;
  if (!vk_ok(vkAllocateMemory(device, &allocate_info, nullptr, memory),
             "vkAllocateMemory(image)", &error)) {
    return false;
  }
  if (!vk_ok(vkBindImageMemory(device, *image, *memory, 0),
             "vkBindImageMemory", &error)) {
    return false;
  }
  return true;
}

VkCommandBuffer Renderer::begin_one_time() {
  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = command_pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  if (!vk_ok(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer),
             "vkAllocateCommandBuffers", &error)) {
    return VK_NULL_HANDLE;
  }
  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!vk_ok(vkBeginCommandBuffer(command_buffer, &begin_info),
             "vkBeginCommandBuffer", &error)) {
    return VK_NULL_HANDLE;
  }
  return command_buffer;
}

bool Renderer::end_one_time(VkCommandBuffer command_buffer) {
  if (!vk_ok(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer",
             &error)) {
    return false;
  }
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  if (!vk_ok(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE),
             "vkQueueSubmit(one-time)", &error)) {
    return false;
  }
  if (!vk_ok(vkQueueWaitIdle(queue), "vkQueueWaitIdle", &error)) {
    return false;
  }
  vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
  return true;
}

bool Renderer::create_instance_and_device() {
  VkApplicationInfo application_info{};
  application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  application_info.pApplicationName = "msphere_energy";
  application_info.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.pEngineName = "msphere_energy";
  application_info.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  application_info.apiVersion = VK_API_VERSION_1_1;

  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &application_info;
  if (!vk_ok(vkCreateInstance(&instance_info, nullptr, &instance),
             "vkCreateInstance", &error)) {
    return false;
  }

  uint32_t physical_count = 0;
  if (!vk_ok(vkEnumeratePhysicalDevices(instance, &physical_count, nullptr),
             "vkEnumeratePhysicalDevices(count)", &error)) {
    return false;
  }
  if (physical_count == 0) {
    error = "no Vulkan physical devices";
    return false;
  }
  std::vector<VkPhysicalDevice> physical_devices(physical_count);
  if (!vk_ok(vkEnumeratePhysicalDevices(instance, &physical_count,
                                        physical_devices.data()),
             "vkEnumeratePhysicalDevices", &error)) {
    return false;
  }

  bool found = false;
  for (VkPhysicalDevice candidate : physical_devices) {
    uint32_t family_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count, nullptr);
    std::vector<VkQueueFamilyProperties> families(family_count);
    vkGetPhysicalDeviceQueueFamilyProperties(candidate, &family_count,
                                             families.data());
    for (uint32_t i = 0; i < family_count; ++i) {
      if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0U) {
        physical_device = candidate;
        queue_family = i;
        timestamp_supported = families[i].timestampValidBits != 0U;
        found = true;
        break;
      }
    }
    if (found) {
      break;
    }
  }
  if (!found) {
    error = "no Vulkan device with a graphics queue";
    return false;
  }

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_device, &properties);
  info.device_name = properties.deviceName;
  info.vendor_id = properties.vendorID;
  info.device_id = properties.deviceID;
  info.vulkan_api_version = format_version(properties.apiVersion);
  info.timestamp_period_ns = properties.limits.timestampPeriod;
  info.timestamp_supported =
      timestamp_supported && properties.limits.timestampPeriod > 0.0F;
  info.queue_family_index = queue_family;
  std::ostringstream driver;
  driver << "0x" << std::hex << std::setfill('0') << std::setw(8)
         << properties.driverVersion << " (" << std::dec
         << properties.driverVersion << ")";
  info.driver_name = driver.str();

  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = queue_family;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  if (!vk_ok(vkCreateDevice(physical_device, &device_info, nullptr, &device),
             "vkCreateDevice", &error)) {
    return false;
  }
  vkGetDeviceQueue(device, queue_family, 0, &queue);
  return true;
}

bool Renderer::create_command_pool() {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = queue_family;
  return vk_ok(vkCreateCommandPool(device, &pool_info, nullptr, &command_pool),
               "vkCreateCommandPool", &error);
}

bool Renderer::create_swapchainless_targets() {
  if (!allocate_image(kColorFormat, extent,
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                      &color_image, &color_memory)) {
    return false;
  }
  if (!allocate_image(kDepthFormat, extent,
                      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                      &depth_image, &depth_memory)) {
    return false;
  }

  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = kColorFormat;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY};
  view_info.image = color_image;
  if (!vk_ok(vkCreateImageView(device, &view_info, nullptr, &color_view),
             "vkCreateImageView(color)", &error)) {
    return false;
  }
  view_info.format = kDepthFormat;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  view_info.image = depth_image;
  if (!vk_ok(vkCreateImageView(device, &view_info, nullptr, &depth_view),
             "vkCreateImageView(depth)", &error)) {
    return false;
  }

  VkSamplerCreateInfo sampler_info{};
  sampler_info.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  sampler_info.magFilter = VK_FILTER_LINEAR;
  sampler_info.minFilter = VK_FILTER_LINEAR;
  sampler_info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
  sampler_info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  sampler_info.maxAnisotropy = 1.0F;
  sampler_info.compareOp = VK_COMPARE_OP_NEVER;
  sampler_info.minLod = 0.0F;
  sampler_info.maxLod = 0.0F;
  if (!vk_ok(vkCreateSampler(device, &sampler_info, nullptr,
                             &reflection_sampler),
             "vkCreateSampler", &error)) {
    return false;
  }

  image_evidence.push_back({"color_framebuffer",
                            reinterpret_cast<uint64_t>(color_image),
                            reinterpret_cast<uint64_t>(color_view),
                            extent.width, extent.height, "R8G8B8A8_UNORM",
                            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                            "UNDEFINED", "COLOR_ATTACHMENT_OPTIMAL",
                            "UNDEFINED->COLOR_ATTACHMENT_OPTIMAL; "
                            "render; ->TRANSFER_SRC_OPTIMAL; readback; "
                            "->COLOR_ATTACHMENT_OPTIMAL; repeat"});
  image_evidence.push_back({"depth_framebuffer",
                            reinterpret_cast<uint64_t>(depth_image),
                            reinterpret_cast<uint64_t>(depth_view),
                            extent.width, extent.height, "D32_SFLOAT",
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            "UNDEFINED",
                            "DEPTH_STENCIL_ATTACHMENT_OPTIMAL",
                            "UNDEFINED->DEPTH_STENCIL_ATTACHMENT_OPTIMAL; "
                            "render; repeat"});
  return true;
}

bool Renderer::create_mesh_and_instances(const ParsedInput& input) {
  std::vector<Vertex> vertices;
  std::vector<uint16_t> indices;
  vertices.reserve((kSphereLatitudeSegments + 1U) *
                   (kSphereLongitudeSegments + 1U));
  for (uint32_t latitude = 0; latitude <= kSphereLatitudeSegments; ++latitude) {
    const float theta =
        static_cast<float>(latitude) *
        static_cast<float>(3.14159265358979323846) /
        static_cast<float>(kSphereLatitudeSegments);
    const float sin_theta = std::sin(theta);
    const float cos_theta = std::cos(theta);
    for (uint32_t longitude = 0; longitude <= kSphereLongitudeSegments;
         ++longitude) {
      const float phi =
          static_cast<float>(longitude) * static_cast<float>(2.0 * 3.14159265358979323846) /
          static_cast<float>(kSphereLongitudeSegments);
      Vertex vertex{};
      vertex.normal[0] = sin_theta * std::cos(phi);
      vertex.normal[1] = cos_theta;
      vertex.normal[2] = sin_theta * std::sin(phi);
      vertex.position[0] = vertex.normal[0] * kSphereRadius;
      vertex.position[1] = vertex.normal[1] * kSphereRadius;
      vertex.position[2] = vertex.normal[2] * kSphereRadius;
      vertices.push_back(vertex);
    }
  }
  for (uint32_t latitude = 0; latitude < kSphereLatitudeSegments; ++latitude) {
    for (uint32_t longitude = 0; longitude < kSphereLongitudeSegments;
         ++longitude) {
      const uint16_t a = static_cast<uint16_t>(
          latitude * (kSphereLongitudeSegments + 1U) + longitude);
      const uint16_t b = static_cast<uint16_t>(a + kSphereLongitudeSegments + 1U);
      const uint16_t c = static_cast<uint16_t>(a + 1U);
      const uint16_t d = static_cast<uint16_t>(b + 1U);
      indices.insert(indices.end(), {a, b, c, b, d, c});
    }
  }
  index_count = static_cast<uint32_t>(indices.size());

  const VkDeviceSize vertex_size =
      static_cast<VkDeviceSize>(vertices.size() * sizeof(Vertex));
  const VkDeviceSize index_size =
      static_cast<VkDeviceSize>(indices.size() * sizeof(uint16_t));
  if (!allocate_buffer(vertex_size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &vertex_buffer, &vertex_memory)) {
    return false;
  }
  if (!allocate_buffer(index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &index_buffer, &index_memory)) {
    return false;
  }
  void* vertex_mapped = nullptr;
  void* index_mapped = nullptr;
  if (!vk_ok(vkMapMemory(device, vertex_memory, 0, vertex_size, 0,
                         &vertex_mapped),
             "vkMapMemory(vertices)", &error)) {
    return false;
  }
  std::memcpy(vertex_mapped, vertices.data(), static_cast<size_t>(vertex_size));
  vkUnmapMemory(device, vertex_memory);
  if (!vk_ok(vkMapMemory(device, index_memory, 0, index_size, 0, &index_mapped),
             "vkMapMemory(indices)", &error)) {
    return false;
  }
  std::memcpy(index_mapped, indices.data(), static_cast<size_t>(index_size));
  vkUnmapMemory(device, index_memory);

  grid = make_grid(input.material_count);
  const VkDeviceSize instance_size =
      static_cast<VkDeviceSize>(grid.instances.size() * sizeof(Instance));
  if (!allocate_buffer(instance_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &instance_buffer, &instance_memory)) {
    return false;
  }
  if (!vk_ok(vkMapMemory(device, instance_memory, 0, instance_size, 0,
                         &instance_mapped),
             "vkMapMemory(instances)", &error)) {
    return false;
  }
  std::memcpy(instance_mapped, grid.instances.data(),
              static_cast<size_t>(instance_size));
  const auto* instance_bytes =
      reinterpret_cast<const uint8_t*>(grid.instances.data());
  buffer_evidence.push_back(
      {"vertex_buffer", reinterpret_cast<uint64_t>(vertex_buffer), vertex_size,
       static_cast<uint64_t>(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT),
       sha256_hex(reinterpret_cast<const uint8_t*>(vertices.data()),
                  static_cast<size_t>(vertex_size))});
  buffer_evidence.push_back(
      {"index_buffer", reinterpret_cast<uint64_t>(index_buffer), index_size,
       static_cast<uint64_t>(VK_BUFFER_USAGE_INDEX_BUFFER_BIT),
       sha256_hex(reinterpret_cast<const uint8_t*>(indices.data()),
                  static_cast<size_t>(index_size))});
  buffer_evidence.push_back(
      {"instance_buffer", reinterpret_cast<uint64_t>(instance_buffer),
       instance_size,
       static_cast<uint64_t>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
       sha256_hex(instance_bytes, static_cast<size_t>(instance_size))});
  return true;
}

bool Renderer::create_material_buffer(const ParsedInput& input) {
  std::vector<GpuMaterial> materials(input.material_count);
  for (uint32_t i = 0; i < input.material_count; ++i) {
    const Material& source = input.materials[i];
    materials[i].base_color_roughness[0] = source.base_color[0];
    materials[i].base_color_roughness[1] = source.base_color[1];
    materials[i].base_color_roughness[2] = source.base_color[2];
    materials[i].base_color_roughness[3] = source.roughness;
    materials[i].properties[0] = source.metallic;
    materials[i].properties[1] = source.reflectance;
    materials[i].properties[2] = input.energy_integrals[i];
    materials[i].properties[3] = source.albedo_scale;
  }
  const VkDeviceSize size =
      static_cast<VkDeviceSize>(materials.size() * sizeof(GpuMaterial));
  if (!allocate_buffer(size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &material_buffer, &material_memory)) {
    return false;
  }
  if (!vk_ok(vkMapMemory(device, material_memory, 0, size, 0, &material_mapped),
             "vkMapMemory(materials)", &error)) {
    return false;
  }
  std::memcpy(material_mapped, materials.data(), static_cast<size_t>(size));
  buffer_evidence.push_back(
      {"material_buffer", reinterpret_cast<uint64_t>(material_buffer), size,
       static_cast<uint64_t>(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT),
       sha256_hex(reinterpret_cast<const uint8_t*>(materials.data()),
                  static_cast<size_t>(size))});
  return true;
}

bool Renderer::create_reflection_texture(const ParsedInput& input) {
  const VkExtent2D reflection_extent{input.reflection_width,
                                     input.reflection_height};
  if (!allocate_image(VK_FORMAT_R8G8B8A8_UNORM, reflection_extent,
                      VK_IMAGE_USAGE_SAMPLED_BIT |
                          VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                      &reflection_image, &reflection_memory)) {
    return false;
  }
  VkImageViewCreateInfo view_info{};
  view_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  view_info.image = reflection_image;
  view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
  view_info.format = VK_FORMAT_R8G8B8A8_UNORM;
  view_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  view_info.subresourceRange.levelCount = 1;
  view_info.subresourceRange.layerCount = 1;
  view_info.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY,
                          VK_COMPONENT_SWIZZLE_IDENTITY};
  if (!vk_ok(vkCreateImageView(device, &view_info, nullptr, &reflection_view),
             "vkCreateImageView(reflection)", &error)) {
    return false;
  }
  image_evidence.push_back(
      {"reflection_sphere", reinterpret_cast<uint64_t>(reflection_image),
       reinterpret_cast<uint64_t>(reflection_view),
       reflection_extent.width, reflection_extent.height, "R8G8B8A8_UNORM",
       static_cast<uint64_t>(VK_IMAGE_USAGE_SAMPLED_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT),
       "UNDEFINED", "SHADER_READ_ONLY_OPTIMAL",
       "UNDEFINED->TRANSFER_DST_OPTIMAL->SHADER_READ_ONLY_OPTIMAL"});
  return true;
}

bool Renderer::upload_reflection_texture(const ParsedInput& input) {
  const VkDeviceSize image_bytes =
      static_cast<VkDeviceSize>(input.reflection_rgba.size());
  VkBuffer staging = VK_NULL_HANDLE;
  VkDeviceMemory staging_memory = VK_NULL_HANDLE;
  if (!allocate_buffer(image_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &staging, &staging_memory)) {
    return false;
  }
  void* mapped = nullptr;
  if (!vk_ok(vkMapMemory(device, staging_memory, 0, image_bytes, 0, &mapped),
             "vkMapMemory(reflection staging)", &error)) {
    return false;
  }
  std::memcpy(mapped, input.reflection_rgba.data(),
              static_cast<size_t>(image_bytes));
  vkUnmapMemory(device, staging_memory);

  VkCommandBuffer command_buffer = begin_one_time();
  if (command_buffer == VK_NULL_HANDLE) {
    return false;
  }
  VkImageMemoryBarrier to_transfer{};
  to_transfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  to_transfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  to_transfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_transfer.image = reflection_image;
  to_transfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_transfer.subresourceRange.levelCount = 1;
  to_transfer.subresourceRange.layerCount = 1;
  to_transfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                       VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &to_transfer);

  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {input.reflection_width, input.reflection_height, 1};
  vkCmdCopyBufferToImage(command_buffer, staging, reflection_image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

  VkImageMemoryBarrier to_shader{};
  to_shader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  to_shader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
  to_shader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  to_shader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_shader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  to_shader.image = reflection_image;
  to_shader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  to_shader.subresourceRange.levelCount = 1;
  to_shader.subresourceRange.layerCount = 1;
  to_shader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  to_shader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &to_shader);
  const bool uploaded = end_one_time(command_buffer);
  vkDestroyBuffer(device, staging, nullptr);
  vkFreeMemory(device, staging_memory, nullptr);
  return uploaded;
}

bool Renderer::create_readback_buffer() {
  const VkDeviceSize size =
      static_cast<VkDeviceSize>(extent.width) * extent.height * 4U;
  if (!allocate_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &readback_buffer, &readback_memory)) {
    return false;
  }
  if (!vk_ok(vkMapMemory(device, readback_memory, 0, size, 0, &readback_mapped),
             "vkMapMemory(readback)", &error)) {
    return false;
  }
  buffer_evidence.push_back(
      {"readback_buffer", reinterpret_cast<uint64_t>(readback_buffer), size,
       static_cast<uint64_t>(VK_BUFFER_USAGE_TRANSFER_DST_BIT),
       sha256_hex(static_cast<const uint8_t*>(readback_mapped),
                  static_cast<size_t>(size))});
  return true;
}

bool Renderer::create_query_pool() {
  if (!info.timestamp_supported) {
    timestamp_supported = false;
    return true;
  }
  VkQueryPoolCreateInfo query_info{};
  query_info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  query_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  query_info.queryCount = kTimestampQueryCount;
  if (!vk_ok(vkCreateQueryPool(device, &query_info, nullptr, &query_pool),
             "vkCreateQueryPool", &error)) {
    timestamp_supported = false;
    return true;
  }
  timestamp_supported = true;
  return true;
}

bool Renderer::create_pipeline() {
  auto load_shader = [this](const std::string& path,
                            VkShaderModule* module) -> bool {
    const std::vector<uint8_t> code = read_binary_file(path, &error);
    if (code.empty()) {
      return false;
    }
    VkShaderModuleCreateInfo create_info{};
    create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.codeSize = code.size();
    create_info.pCode = reinterpret_cast<const uint32_t*>(code.data());
    return vk_ok(vkCreateShaderModule(device, &create_info, nullptr, module),
                 "vkCreateShaderModule", &error);
  };
  if (!load_shader(std::string(MSPHERE_ASSET_DIR) +
                       "/shaders/material.vert.spv",
                   &vertex_shader) ||
      !load_shader(std::string(MSPHERE_ASSET_DIR) +
                       "/shaders/material.frag.spv",
                   &fragment_shader)) {
    return false;
  }

  const VkDescriptorSetLayoutBinding bindings[3] = {
      {0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT,
       nullptr},
      {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
       VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
      {2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT,
       nullptr},
  };
  VkDescriptorSetLayoutCreateInfo descriptor_info{};
  descriptor_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptor_info.bindingCount = 3;
  descriptor_info.pBindings = bindings;
  if (!vk_ok(vkCreateDescriptorSetLayout(device, &descriptor_info, nullptr,
                                         &descriptor_layout),
             "vkCreateDescriptorSetLayout", &error)) {
    return false;
  }

  VkPushConstantRange push_range{};
  push_range.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  push_range.offset = 0;
  push_range.size = sizeof(float) * 2U;
  VkPipelineLayoutCreateInfo layout_info{};
  layout_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layout_info.setLayoutCount = 1;
  layout_info.pSetLayouts = &descriptor_layout;
  layout_info.pushConstantRangeCount = 1;
  layout_info.pPushConstantRanges = &push_range;
  if (!vk_ok(vkCreatePipelineLayout(device, &layout_info, nullptr,
                                    &pipeline_layout),
             "vkCreatePipelineLayout", &error)) {
    return false;
  }

  VkAttachmentDescription attachments[2]{};
  attachments[0].format = kColorFormat;
  attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  attachments[1].format = kDepthFormat;
  attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[1].initialLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  attachments[1].finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_reference{};
  color_reference.attachment = 0;
  color_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  VkAttachmentReference depth_reference{};
  depth_reference.attachment = 1;
  depth_reference.layout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_reference;
  subpass.pDepthStencilAttachment = &depth_reference;
  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  VkRenderPassCreateInfo render_pass_info{};
  render_pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  render_pass_info.attachmentCount = 2;
  render_pass_info.pAttachments = attachments;
  render_pass_info.subpassCount = 1;
  render_pass_info.pSubpasses = &subpass;
  render_pass_info.dependencyCount = 1;
  render_pass_info.pDependencies = &dependency;
  if (!vk_ok(vkCreateRenderPass(device, &render_pass_info, nullptr,
                                &render_pass),
             "vkCreateRenderPass", &error)) {
    return false;
  }

  const VkImageView framebuffer_views[2] = {color_view, depth_view};
  VkFramebufferCreateInfo framebuffer_info{};
  framebuffer_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  framebuffer_info.renderPass = render_pass;
  framebuffer_info.attachmentCount = 2;
  framebuffer_info.pAttachments = framebuffer_views;
  framebuffer_info.width = extent.width;
  framebuffer_info.height = extent.height;
  framebuffer_info.layers = 1;
  if (!vk_ok(vkCreateFramebuffer(device, &framebuffer_info, nullptr,
                                 &framebuffer),
             "vkCreateFramebuffer", &error)) {
    return false;
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertex_shader;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragment_shader;
  stages[1].pName = "main";

  VkVertexInputBindingDescription vertex_binding{};
  vertex_binding.binding = 0;
  vertex_binding.stride = sizeof(Vertex);
  vertex_binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  VkVertexInputAttributeDescription attributes[2]{};
  attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                   static_cast<uint32_t>(sizeof(float) * 3U)};
  VkPipelineVertexInputStateCreateInfo vertex_input{};
  vertex_input.sType =
      VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertex_input.vertexBindingDescriptionCount = 1;
  vertex_input.pVertexBindingDescriptions = &vertex_binding;
  vertex_input.vertexAttributeDescriptionCount = 2;
  vertex_input.pVertexAttributeDescriptions = attributes;
  VkPipelineInputAssemblyStateCreateInfo assembly{};
  assembly.sType =
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkViewport viewport{0.0F, 0.0F, static_cast<float>(extent.width),
                      static_cast<float>(extent.height), 0.0F, 1.0F};
  VkRect2D scissor{{0, 0}, extent};
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
  VkPipelineDepthStencilStateCreateInfo depth_state{};
  depth_state.sType =
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depth_state.depthTestEnable = VK_TRUE;
  depth_state.depthWriteEnable = VK_TRUE;
  depth_state.depthCompareOp = VK_COMPARE_OP_LESS;
  VkPipelineColorBlendAttachmentState blend_attachment{};
  blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                    VK_COLOR_COMPONENT_G_BIT |
                                    VK_COLOR_COMPONENT_B_BIT |
                                    VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo blend{};
  blend.sType =
      VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  blend.attachmentCount = 1;
  blend.pAttachments = &blend_attachment;
  VkGraphicsPipelineCreateInfo pipeline_info{};
  pipeline_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipeline_info.stageCount = 2;
  pipeline_info.pStages = stages;
  pipeline_info.pVertexInputState = &vertex_input;
  pipeline_info.pInputAssemblyState = &assembly;
  pipeline_info.pViewportState = &viewport_state;
  pipeline_info.pRasterizationState = &raster;
  pipeline_info.pMultisampleState = &multisample;
  pipeline_info.pDepthStencilState = &depth_state;
  pipeline_info.pColorBlendState = &blend;
  pipeline_info.layout = pipeline_layout;
  pipeline_info.renderPass = render_pass;
  if (!vk_ok(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                       &pipeline_info, nullptr, &pipeline),
             "vkCreateGraphicsPipelines", &error)) {
    return false;
  }

  const VkDescriptorPoolSize pool_sizes[2] = {
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1},
  };
  VkDescriptorPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  pool_info.maxSets = 1;
  pool_info.poolSizeCount = 2;
  pool_info.pPoolSizes = pool_sizes;
  if (!vk_ok(vkCreateDescriptorPool(device, &pool_info, nullptr,
                                    &descriptor_pool),
             "vkCreateDescriptorPool", &error)) {
    return false;
  }
  VkDescriptorSetAllocateInfo set_info{};
  set_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  set_info.descriptorPool = descriptor_pool;
  set_info.descriptorSetCount = 1;
  set_info.pSetLayouts = &descriptor_layout;
  if (!vk_ok(vkAllocateDescriptorSets(device, &set_info, &descriptor_set),
             "vkAllocateDescriptorSets", &error)) {
    return false;
  }

  VkDescriptorBufferInfo material_info{};
  material_info.buffer = material_buffer;
  material_info.range = VK_WHOLE_SIZE;
  VkDescriptorImageInfo reflection_info{};
  reflection_info.sampler = reflection_sampler;
  reflection_info.imageView = reflection_view;
  reflection_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  VkDescriptorBufferInfo instance_info{};
  instance_info.buffer = instance_buffer;
  instance_info.range = VK_WHOLE_SIZE;
  VkWriteDescriptorSet writes[3]{};
  writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[0].dstSet = descriptor_set;
  writes[0].dstBinding = 0;
  writes[0].descriptorCount = 1;
  writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[0].pBufferInfo = &material_info;
  writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[1].dstSet = descriptor_set;
  writes[1].dstBinding = 1;
  writes[1].descriptorCount = 1;
  writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  writes[1].pImageInfo = &reflection_info;
  writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  writes[2].dstSet = descriptor_set;
  writes[2].dstBinding = 2;
  writes[2].descriptorCount = 1;
  writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
  writes[2].pBufferInfo = &instance_info;
  vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
  info.color_format = "R8G8B8A8_UNORM";
  return true;
}

bool Renderer::init(const ParsedInput& input, const RenderOptions& options) {
  extent = {options.width, options.height};
  material_count = input.material_count;
  glfwSetErrorCallback(glfw_error_callback);
  glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
  if (glfwInit() != GLFW_TRUE) {
    glfwInitHint(GLFW_PLATFORM, GLFW_ANY_PLATFORM);
    if (glfwInit() != GLFW_TRUE) {
      error = "glfwInit failed: " +
              (g_last_glfw_error.empty() ? std::string("unknown") :
                                           g_last_glfw_error);
      return false;
    }
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  window = glfwCreateWindow(64, 64, "msphere_energy-offscreen", nullptr,
                            nullptr);
  if (window == nullptr) {
    error = "glfwCreateWindow failed: " +
            (g_last_glfw_error.empty() ? std::string("unknown") :
                                         g_last_glfw_error);
    return false;
  }
  info.glfw_version = glfwGetVersionString();
  info.glfw_platform = glfw_platform_name(glfwGetPlatform());

  if (!create_instance_and_device() || !create_command_pool() ||
      !create_swapchainless_targets() ||
      !create_mesh_and_instances(input) || !create_material_buffer(input) ||
      !create_reflection_texture(input) || !upload_reflection_texture(input) ||
      !create_readback_buffer() || !create_query_pool() || !create_pipeline()) {
    return false;
  }
  info.frame_count = 2;
  return true;
}

bool Renderer::render_frame(const std::string& name,
                            const std::vector<Material>& materials,
                            uint64_t sequence, FrameEvidence* frame) {
  if (materials.size() != material_count) {
    error = "material count changed between frames";
    return false;
  }
  std::vector<GpuMaterial> gpu_materials(material_count);
  for (uint32_t i = 0; i < material_count; ++i) {
    const Material& source = materials[i];
    gpu_materials[i].base_color_roughness[0] = source.base_color[0];
    gpu_materials[i].base_color_roughness[1] = source.base_color[1];
    gpu_materials[i].base_color_roughness[2] = source.base_color[2];
    gpu_materials[i].base_color_roughness[3] = source.roughness;
    gpu_materials[i].properties[0] = source.metallic;
    gpu_materials[i].properties[1] = source.reflectance;
    gpu_materials[i].properties[2] = source.energy_integral;
    gpu_materials[i].properties[3] = source.albedo_scale;
  }
  const auto* gpu_bytes =
      reinterpret_cast<const uint8_t*>(gpu_materials.data());
  const size_t gpu_size = gpu_materials.size() * sizeof(GpuMaterial);
  std::memcpy(material_mapped, gpu_bytes, gpu_size);

  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = command_pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;
  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  if (!vk_ok(vkAllocateCommandBuffers(device, &allocate_info, &command_buffer),
             "vkAllocateCommandBuffers(frame)", &error)) {
    return false;
  }
  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  if (!vk_ok(vkCreateFence(device, &fence_info, nullptr, &fence),
             "vkCreateFence", &error)) {
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
    return false;
  }
  auto cleanup = [&]() {
    vkDestroyFence(device, fence, nullptr);
    vkFreeCommandBuffers(device, command_pool, 1, &command_buffer);
  };
  auto fail = [&](const std::string& message) {
    cleanup();
    error = message;
    return false;
  };

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  if (!vk_ok(vkBeginCommandBuffer(command_buffer, &begin_info),
             "vkBeginCommandBuffer(frame)", &error)) {
    return fail(error);
  }
  if (timestamp_supported) {
    vkCmdResetQueryPool(command_buffer, query_pool, 0, kTimestampQueryCount);
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        query_pool, 0);
  }

  auto image_barrier = [&](VkImage image, VkImageAspectFlags aspect,
                           VkImageLayout old_layout,
                           VkImageLayout new_layout,
                           VkPipelineStageFlags src_stage,
                           VkPipelineStageFlags dst_stage,
                           VkAccessFlags src_access,
                           VkAccessFlags dst_access) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = old_layout;
    barrier.newLayout = new_layout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = aspect;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = src_access;
    barrier.dstAccessMask = dst_access;
    vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0, nullptr,
                         0, nullptr, 1, &barrier);
  };

  const bool first_frame = sequence == 1;
  const VkImageLayout original_color_layout =
      first_frame ? VK_IMAGE_LAYOUT_UNDEFINED
                  : VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  const VkImageLayout original_depth_layout =
      first_frame ? VK_IMAGE_LAYOUT_UNDEFINED
                  : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  const VkPipelineStageFlags acquire_src_stage =
      first_frame ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                  : VK_PIPELINE_STAGE_TRANSFER_BIT;
  image_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT, original_color_layout,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, acquire_src_stage,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  image_barrier(depth_image, VK_IMAGE_ASPECT_DEPTH_BIT, original_depth_layout,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                acquire_src_stage,
                VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

  const VkClearValue clear_values[2] = {
      {{{0.006F, 0.009F, 0.015F, 1.0F}}},
      {{1.0F, 0}},
  };
  VkRenderPassBeginInfo pass_info{};
  pass_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  pass_info.renderPass = render_pass;
  pass_info.framebuffer = framebuffer;
  pass_info.renderArea = {{0, 0}, extent};
  pass_info.clearValueCount = 2;
  pass_info.pClearValues = clear_values;
  vkCmdBeginRenderPass(command_buffer, &pass_info, VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
  vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipeline_layout, 0, 1, &descriptor_set, 0, nullptr);
  const VkDeviceSize vertex_offset = 0;
  vkCmdBindVertexBuffers(command_buffer, 0, 1, &vertex_buffer, &vertex_offset);
  vkCmdBindIndexBuffer(command_buffer, index_buffer, 0, VK_INDEX_TYPE_UINT16);
  const float push_constants[2] = {grid.half_width, grid.half_height};
  vkCmdPushConstants(command_buffer, pipeline_layout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constants),
                     push_constants);
  vkCmdDrawIndexed(command_buffer, index_count, material_count, 0, 0, 0);
  vkCmdEndRenderPass(command_buffer);
  if (timestamp_supported) {
    vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                        query_pool, 1);
  }

  image_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT);
  VkBufferImageCopy copy{};
  copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  copy.imageSubresource.layerCount = 1;
  copy.imageExtent = {extent.width, extent.height, 1};
  vkCmdCopyImageToBuffer(command_buffer, color_image,
                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         readback_buffer, 1, &copy);
  image_barrier(color_image, VK_IMAGE_ASPECT_COLOR_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  VkBufferMemoryBarrier buffer_barrier{};
  buffer_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  buffer_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_barrier.buffer = readback_buffer;
  buffer_barrier.offset = 0;
  buffer_barrier.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr, 1,
                       &buffer_barrier, 0, nullptr);

  if (!vk_ok(vkEndCommandBuffer(command_buffer), "vkEndCommandBuffer(frame)",
             &error)) {
    return fail(error);
  }
  VkSubmitInfo submit_info{};
  submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit_info.commandBufferCount = 1;
  submit_info.pCommandBuffers = &command_buffer;
  if (!vk_ok(vkQueueSubmit(queue, 1, &submit_info, fence),
             "vkQueueSubmit(frame)", &error)) {
    return fail(error);
  }
  if (!vk_ok(vkWaitForFences(device, 1, &fence, VK_TRUE,
                             std::numeric_limits<uint64_t>::max()),
             "vkWaitForFences", &error)) {
    return fail(error);
  }

  GpuTiming timing;
  if (timestamp_supported && query_pool != VK_NULL_HANDLE) {
    std::array<uint64_t, kTimestampQueryCount> ticks{};
    const VkResult query_result = vkGetQueryPoolResults(
        device, query_pool, 0, kTimestampQueryCount,
        sizeof(ticks), ticks.data(), sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);
    if (query_result == VK_SUCCESS) {
      timing.valid = true;
      timing.start_ticks = ticks[0];
      timing.end_ticks = ticks[1];
      timing.elapsed_ns =
          static_cast<double>(ticks[1] - ticks[0]) *
          static_cast<double>(info.timestamp_period_ns);
    }
  }

  frame->name = name;
  frame->image.width = extent.width;
  frame->image.height = extent.height;
  const size_t image_size =
      static_cast<size_t>(extent.width) * extent.height * 4U;
  frame->image.pixels.assign(static_cast<const uint8_t*>(readback_mapped),
                             static_cast<const uint8_t*>(readback_mapped) +
                                 image_size);
  frame->draw.sequence = sequence;
  frame->draw.frame = name;
  frame->draw.first_index = 0;
  frame->draw.index_count = index_count;
  frame->draw.vertex_offset = 0;
  frame->draw.first_instance = 0;
  frame->draw.instance_count = material_count;
  frame->draw.command_buffer =
      reinterpret_cast<uint64_t>(command_buffer);
  frame->draw.framebuffer = reinterpret_cast<uint64_t>(framebuffer);
  frame->draw.color_image = reinterpret_cast<uint64_t>(color_image);
  frame->draw.depth_image = reinterpret_cast<uint64_t>(depth_image);
  frame->draw.material_buffer = reinterpret_cast<uint64_t>(material_buffer);
  frame->draw.instance_buffer = reinterpret_cast<uint64_t>(instance_buffer);
  frame->draw.material_byte_offset = 0;
  frame->draw.instance_byte_offset = 0;
  frame->draw.material_content_sha256 = sha256_hex(gpu_bytes, gpu_size);
  frame->draw.gpu = timing;
  cleanup();
  return true;
}

}  // namespace

RenderResult render_frames(const ParsedInput& input,
                           const RenderOptions& options) {
  RenderResult result;
  if (options.width == 0 || options.height == 0 || options.width > 4096 ||
      options.height > 4096) {
    result.error = "framebuffer extent must be within 1..4096";
    return result;
  }
  if (input.material_count == 0) {
    result.error = "cannot render an input with zero materials";
    return result;
  }
  const uint32_t target_instance =
      std::min(options.anomaly_instance, input.material_count - 1U);
  const uint64_t anomaly_offset =
      input.material_offset + static_cast<uint64_t>(target_instance) * kMaterialSize;
  float original_red = 0.0F;
  std::memcpy(&original_red, input.bytes.data() + anomaly_offset,
              sizeof(original_red));
  std::vector<Material> baseline_materials = input.materials;
  std::vector<Material> anomaly_materials = input.materials;
  anomaly_materials[target_instance].base_color[0] = options.anomaly_red;

  AnomalyEvidence anomaly;
  anomaly.instance_index = target_instance;
  anomaly.material_index = target_instance;
  anomaly.material_count = input.material_count;
  anomaly.field = "base_color.r";
  anomaly.source_file_offset = anomaly_offset;
  anomaly.original_value = original_red;
  anomaly.mutated_value = options.anomaly_red;
  anomaly.original_bits = float_bits(original_red);
  anomaly.mutated_bits = float_bits(options.anomaly_red);
  anomaly.original_bytes_hex =
      bytes_to_hex(input.bytes.data() + anomaly_offset, 4);
  anomaly.mutated_bytes_hex =
      bytes_to_hex(reinterpret_cast<const uint8_t*>(&options.anomaly_red), 4);
  result.anomaly = anomaly;

  Renderer renderer;
  if (!renderer.init(input, options)) {
    result.error = renderer.error;
    return result;
  }
  result.info = renderer.info;
  result.buffers = renderer.buffer_evidence;
  result.images = renderer.image_evidence;
  const float half_width = renderer.grid.half_width;
  const float half_height = renderer.grid.half_height;
  const float radius_x =
      kSphereRadius / (2.0F * half_width) * static_cast<float>(options.width);
  const float radius_y =
      kSphereRadius / (2.0F * half_height) * static_cast<float>(options.height);
  for (uint32_t i = 0; i < renderer.grid.instances.size(); ++i) {
    const Instance& instance = renderer.grid.instances[i];
    const float center_x =
        (0.5F + instance.position[0] / (2.0F * half_width)) *
        static_cast<float>(options.width);
    const float center_y =
        (0.5F + instance.position[1] / (2.0F * half_height)) *
        static_cast<float>(options.height);
    const float pad = 3.0F;
    const uint32_t x0 = static_cast<uint32_t>(
        std::max(0.0F, std::floor(center_x - radius_x - pad)));
    const uint32_t y0 = static_cast<uint32_t>(
        std::max(0.0F, std::floor(center_y - radius_y - pad)));
    const uint32_t x1 = std::min(
        options.width - 1U,
        static_cast<uint32_t>(std::ceil(center_x + radius_x + pad)));
    const uint32_t y1 = std::min(
        options.height - 1U,
        static_cast<uint32_t>(std::ceil(center_y + radius_y + pad)));
    result.placements.push_back(
        {i, instance.position[0], instance.position[1], x0, y0, x1, y1});
  }

  FrameEvidence baseline;
  if (!renderer.render_frame("baseline", baseline_materials, 1, &baseline)) {
    result.error = renderer.error;
    return result;
  }
  result.frames.push_back(std::move(baseline));

  FrameEvidence anomaly_frame;
  if (!renderer.render_frame("anomaly", anomaly_materials, 2, &anomaly_frame)) {
    result.error = renderer.error;
    return result;
  }
  result.frames.push_back(std::move(anomaly_frame));
  result.ok = true;
  return result;
}

}  // namespace msphere
