#include "mi/vulkan_backend.hpp"

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mi {
namespace {

struct alignas(16) GpuPayload {
  std::uint32_t magic{0x4d495055};
  std::uint32_t tx{0};
  std::uint64_t version{0};
  std::array<float, 4> color{};
  float roughness{0};
  float metallic{0};
  std::uint32_t shader_variant{0};
  std::uint32_t reserved{0};
  std::array<char, 32> hash_prefix{};
};

static_assert(sizeof(GpuPayload) % 4 == 0, "Vulkan buffer size must be 4-byte aligned");

std::string version_string(std::uint32_t version) {
  std::ostringstream out;
  out << VK_VERSION_MAJOR(version) << "." << VK_VERSION_MINOR(version) << "."
      << VK_VERSION_PATCH(version);
  return out.str();
}

std::string hex_u32(std::uint32_t value) {
  std::ostringstream out;
  out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
  return out.str();
}

bool has_graphics_queue(VkPhysicalDevice device, std::uint32_t* family) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
  const VkQueueFlags required =
      VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
  for (std::uint32_t i = 0; i < count; ++i) {
    if ((families[i].queueFlags & required) == required) {
      *family = i;
      return true;
    }
  }
  return false;
}

std::string device_hint() {
  const char* value = std::getenv("MI_VULKAN_DEVICE");
  return value == nullptr ? std::string{} : std::string{value};
}

struct TxResource {
  VkBuffer staging_buffer{VK_NULL_HANDLE};
  VkDeviceMemory staging_memory{VK_NULL_HANDLE};
  VkBuffer device_buffer{VK_NULL_HANDLE};
  VkDeviceMemory device_memory{VK_NULL_HANDLE};
  VkBuffer readback_buffer{VK_NULL_HANDLE};
  VkDeviceMemory readback_memory{VK_NULL_HANDLE};
  VkCommandBuffer command_buffer{VK_NULL_HANDLE};
  VkFence fence{VK_NULL_HANDLE};
  GpuPayload payload{};
  bool submitted{false};
  bool verified{false};
};

struct ActiveResource {
  VkBuffer buffer{VK_NULL_HANDLE};
  VkDeviceMemory memory{VK_NULL_HANDLE};
  std::uint64_t tx{0};
};

}  // namespace

class VulkanGpuBackend final : public GpuBackend {
 public:
  explicit VulkanGpuBackend(bool visible);
  ~VulkanGpuBackend() override;

  BackendInfo info() const override;
  void reset() override;
  bool begin_upload(const Transaction& tx, std::string* detail) override;
  bool complete_upload(const Transaction& tx, std::string* detail) override;
  bool commit_upload(const Transaction& tx, std::string* detail) override;
  bool cancel_upload(const Transaction& tx, std::string* detail) override;
  void poll() override;

 private:
  void initialize(bool visible);
  void cleanup();
  bool create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                     VkMemoryPropertyFlags properties, VkBuffer* buffer,
                     VkDeviceMemory* memory, std::string* error);
  std::uint32_t find_memory_type(std::uint32_t bits,
                                 VkMemoryPropertyFlags properties) const;
  GpuPayload make_payload(const Transaction& tx) const;
  bool wait_and_verify(TxResource* resource, std::string* detail);
  void destroy_tx(TxResource* resource);
  bool fail(std::string* detail, const std::string& message);

  bool visible_{false};
  bool glfw_ready_{false};
  GLFWwindow* window_{nullptr};
  VkInstance instance_{VK_NULL_HANDLE};
  VkPhysicalDevice physical_{VK_NULL_HANDLE};
  VkDevice device_{VK_NULL_HANDLE};
  VkQueue queue_{VK_NULL_HANDLE};
  VkCommandPool command_pool_{VK_NULL_HANDLE};
  std::uint32_t queue_family_{0};
  BackendInfo info_;
  std::map<std::uint64_t, TxResource> staged_;
  ActiveResource active_;
};

namespace {

std::string glfw_last_error;

void glfw_error_callback(int code, const char* description) {
  glfw_last_error = "GLFW error " + std::to_string(code) + ": " +
                    (description == nullptr ? "unknown" : description);
}

void check_vk(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) {
    throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                             std::to_string(static_cast<int>(result)));
  }
}

}  // namespace

VulkanGpuBackend::VulkanGpuBackend(bool visible) : visible_(visible) {
  try {
    initialize(visible);
  } catch (...) {
    cleanup();
    throw;
  }
}

VulkanGpuBackend::~VulkanGpuBackend() { cleanup(); }

void VulkanGpuBackend::initialize(bool visible) {
  glfwSetErrorCallback(glfw_error_callback);
#if GLFW_VERSION_MAJOR > 3 || \
    (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
  if (std::getenv("DISPLAY") != nullptr) {
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
  }
#endif
  if (glfwInit() != GLFW_TRUE) {
    throw std::runtime_error(
        "GLFW initialization failed. " + glfw_last_error +
        " Check DISPLAY/WAYLAND_DISPLAY or install libglfw3-dev.");
  }
  glfw_ready_ = true;
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  window_ = glfwCreateWindow(640, 360, "Material Instance Hot Update Replay",
                             nullptr, nullptr);
  if (window_ == nullptr) {
    throw std::runtime_error("GLFW window creation failed. " + glfw_last_error);
  }
  if (glfwVulkanSupported() != GLFW_TRUE) {
    throw std::runtime_error(
        "GLFW reports that Vulkan is unavailable. Install a Vulkan loader/ICD "
        "or set VK_ICD_FILENAMES.");
  }

  std::uint32_t extension_count = 0;
  const char** extensions = glfwGetRequiredInstanceExtensions(&extension_count);
  VkApplicationInfo app{};
  app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  app.pApplicationName = "material-instance-hotupdate";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "mi-replay";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo instance_info{};
  instance_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  instance_info.pApplicationInfo = &app;
  instance_info.enabledExtensionCount = extension_count;
  instance_info.ppEnabledExtensionNames = extensions;
  check_vk(vkCreateInstance(&instance_info, nullptr, &instance_),
           "vkCreateInstance");

  std::uint32_t device_count = 0;
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, nullptr),
           "vkEnumeratePhysicalDevices");
  if (device_count == 0) {
    throw std::runtime_error(
        "No Vulkan physical device. Set VK_ICD_FILENAMES to an installed ICD, "
        "for example /usr/share/vulkan/icd.d/lvp_icd.json.");
  }
  std::vector<VkPhysicalDevice> devices(device_count);
  check_vk(vkEnumeratePhysicalDevices(instance_, &device_count, devices.data()),
           "vkEnumeratePhysicalDevices(data)");

  const std::string hint = device_hint();
  int best_score = -1;
  for (VkPhysicalDevice candidate : devices) {
    std::uint32_t family = 0;
    if (!has_graphics_queue(candidate, &family)) {
      continue;
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(candidate, &properties);
    const std::string name = properties.deviceName;
    if (!hint.empty() && name.find(hint) == std::string::npos) {
      continue;
    }
    int score = 10;
    if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
      score = 100;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) {
      score = 60;
    } else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU) {
      score = 20;
    }
    if (score > best_score) {
      best_score = score;
      physical_ = candidate;
      queue_family_ = family;
    }
  }
  if (physical_ == VK_NULL_HANDLE) {
    throw std::runtime_error(
        "No Vulkan device has graphics+compute+transfer queue. "
        "MI_VULKAN_DEVICE may be too restrictive.");
  }

  const float priority = 1.0F;
  VkDeviceQueueCreateInfo queue_info{};
  queue_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queue_info.queueFamilyIndex = queue_family_;
  queue_info.queueCount = 1;
  queue_info.pQueuePriorities = &priority;
  VkDeviceCreateInfo device_info{};
  device_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  device_info.queueCreateInfoCount = 1;
  device_info.pQueueCreateInfos = &queue_info;
  check_vk(vkCreateDevice(physical_, &device_info, nullptr, &device_),
           "vkCreateDevice");
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = queue_family_;
  check_vk(vkCreateCommandPool(device_, &pool_info, nullptr, &command_pool_),
           "vkCreateCommandPool");

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physical_, &properties);
  info_.name = "vulkan";
  info_.device = properties.deviceName;
  info_.driver = "vendor=" + hex_u32(properties.vendorID) +
                 " device=" + hex_u32(properties.deviceID) +
                 " driver=" + hex_u32(properties.driverVersion);
  info_.api_version = version_string(properties.apiVersion);
  info_.asynchronous_submit = true;
}

void VulkanGpuBackend::cleanup() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    for (auto& entry : staged_) {
      destroy_tx(&entry.second);
    }
    staged_.clear();
    if (active_.buffer != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, active_.buffer, nullptr);
    }
    if (active_.memory != VK_NULL_HANDLE) {
      vkFreeMemory(device_, active_.memory, nullptr);
    }
    active_ = ActiveResource{};
    if (command_pool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, command_pool_, nullptr);
      command_pool_ = VK_NULL_HANDLE;
    }
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (instance_ != VK_NULL_HANDLE) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
  if (window_ != nullptr) {
    glfwDestroyWindow(window_);
    window_ = nullptr;
  }
  if (glfw_ready_) {
    glfwTerminate();
    glfw_ready_ = false;
  }
}

BackendInfo VulkanGpuBackend::info() const { return info_; }

std::uint32_t VulkanGpuBackend::find_memory_type(
    std::uint32_t bits, VkMemoryPropertyFlags properties) const {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
  for (std::uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
    if ((bits & (1U << i)) != 0 &&
        (memory.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  return UINT32_MAX;
}

bool VulkanGpuBackend::create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                     VkMemoryPropertyFlags properties,
                                     VkBuffer* buffer, VkDeviceMemory* memory,
                                     std::string* error) {
  VkBufferCreateInfo buffer_info{};
  buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  buffer_info.size = size;
  buffer_info.usage = usage;
  buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult result =
      vkCreateBuffer(device_, &buffer_info, nullptr, buffer);
  if (result != VK_SUCCESS) {
    *error = "vkCreateBuffer failed: " + std::to_string(result);
    return false;
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, *buffer, &requirements);
  const std::uint32_t type =
      find_memory_type(requirements.memoryTypeBits, properties);
  if (type == UINT32_MAX) {
    *error = "no compatible Vulkan memory type for buffer";
    return false;
  }
  VkMemoryAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocation.allocationSize = requirements.size;
  allocation.memoryTypeIndex = type;
  result = vkAllocateMemory(device_, &allocation, nullptr, memory);
  if (result != VK_SUCCESS) {
    *error = "vkAllocateMemory failed: " + std::to_string(result);
    return false;
  }
  result = vkBindBufferMemory(device_, *buffer, *memory, 0);
  if (result != VK_SUCCESS) {
    *error = "vkBindBufferMemory failed: " + std::to_string(result);
    return false;
  }
  return true;
}

GpuPayload VulkanGpuBackend::make_payload(const Transaction& tx) const {
  GpuPayload payload;
  payload.tx = static_cast<std::uint32_t>(tx.id);
  payload.version = tx.request.version;
  payload.color = tx.request.params.base_color;
  payload.roughness = tx.request.params.roughness;
  payload.metallic = tx.request.params.metallic;
  payload.shader_variant = tx.request.params.shader_variant;
  const std::size_t copy_size =
      std::min(payload.hash_prefix.size(), tx.payload_hash.size());
  std::memcpy(payload.hash_prefix.data(), tx.payload_hash.data(), copy_size);
  return payload;
}

bool VulkanGpuBackend::fail(std::string* detail, const std::string& message) {
  if (detail != nullptr) {
    *detail = message;
  }
  return false;
}

void VulkanGpuBackend::destroy_tx(TxResource* resource) {
  if (resource == nullptr || device_ == VK_NULL_HANDLE) {
    return;
  }
  if (resource->fence != VK_NULL_HANDLE) {
    vkDestroyFence(device_, resource->fence, nullptr);
  }
  if (resource->command_buffer != VK_NULL_HANDLE &&
      command_pool_ != VK_NULL_HANDLE) {
    vkFreeCommandBuffers(device_, command_pool_, 1, &resource->command_buffer);
  }
  if (resource->staging_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, resource->staging_buffer, nullptr);
  }
  if (resource->staging_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, resource->staging_memory, nullptr);
  }
  if (resource->readback_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, resource->readback_buffer, nullptr);
  }
  if (resource->readback_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, resource->readback_memory, nullptr);
  }
  if (resource->device_buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, resource->device_buffer, nullptr);
  }
  if (resource->device_memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, resource->device_memory, nullptr);
  }
  *resource = TxResource{};
}

void VulkanGpuBackend::reset() {
  if (device_ == VK_NULL_HANDLE) {
    return;
  }
  vkDeviceWaitIdle(device_);
  for (auto& entry : staged_) {
    destroy_tx(&entry.second);
  }
  staged_.clear();
  if (active_.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, active_.buffer, nullptr);
  }
  if (active_.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, active_.memory, nullptr);
  }
  active_ = ActiveResource{};
}

void VulkanGpuBackend::poll() {
  if (window_ != nullptr) {
    glfwPollEvents();
  }
}

bool VulkanGpuBackend::begin_upload(const Transaction& tx, std::string* detail) {
  if (staged_.count(tx.id) != 0) {
    return fail(detail, "transaction already has a staged GPU resource");
  }
  TxResource resource;
  resource.payload = make_payload(tx);
  const VkDeviceSize size = sizeof(GpuPayload);
  std::string error;

  if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &resource.staging_buffer, &resource.staging_memory,
                     &error)) {
    destroy_tx(&resource);
    return fail(detail, error);
  }
  if (!create_buffer(size,
                     VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                         VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     &resource.device_buffer, &resource.device_memory, &error)) {
    destroy_tx(&resource);
    return fail(detail, error);
  }
  if (!create_buffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                     VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                         VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                     &resource.readback_buffer, &resource.readback_memory,
                     &error)) {
    destroy_tx(&resource);
    return fail(detail, error);
  }

  void* mapped = nullptr;
  VkResult result = vkMapMemory(device_, resource.staging_memory, 0, size, 0,
                                &mapped);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail,
                "vkMapMemory(staging) failed: " + std::to_string(result));
  }
  std::memcpy(mapped, &resource.payload, sizeof(GpuPayload));
  vkUnmapMemory(device_, resource.staging_memory);

  VkCommandBufferAllocateInfo allocation{};
  allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocation.commandPool = command_pool_;
  allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocation.commandBufferCount = 1;
  result =
      vkAllocateCommandBuffers(device_, &allocation, &resource.command_buffer);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail,
                "vkAllocateCommandBuffers failed: " + std::to_string(result));
  }

  VkFenceCreateInfo fence_info{};
  fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  result = vkCreateFence(device_, &fence_info, nullptr, &resource.fence);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail, "vkCreateFence failed: " + std::to_string(result));
  }

  VkCommandBufferBeginInfo begin{};
  begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  result = vkBeginCommandBuffer(resource.command_buffer, &begin);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail,
                "vkBeginCommandBuffer failed: " + std::to_string(result));
  }

  VkBufferCopy region{};
  region.size = size;
  vkCmdCopyBuffer(resource.command_buffer, resource.staging_buffer,
                  resource.device_buffer, 1, &region);
  vkCmdCopyBuffer(resource.command_buffer, resource.device_buffer,
                  resource.readback_buffer, 1, &region);
  VkMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(resource.command_buffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &barrier, 0, nullptr,
                       0, nullptr);
  result = vkEndCommandBuffer(resource.command_buffer);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail,
                "vkEndCommandBuffer failed: " + std::to_string(result));
  }

  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &resource.command_buffer;
  result = vkQueueSubmit(queue_, 1, &submit, resource.fence);
  if (result != VK_SUCCESS) {
    destroy_tx(&resource);
    return fail(detail, "vkQueueSubmit failed: " + std::to_string(result));
  }
  resource.submitted = true;
  staged_[tx.id] = resource;
  if (detail != nullptr) {
    *detail = "staging->device->readback copy submitted for tx " +
              std::to_string(tx.id) + " (" + std::to_string(size) + " bytes)";
  }
  return true;
}

bool VulkanGpuBackend::wait_and_verify(TxResource* resource,
                                       std::string* detail) {
  if (resource == nullptr) {
    return fail(detail, "no staged GPU resource");
  }
  if (!resource->submitted) {
    return fail(detail, "GPU command was not submitted");
  }
  if (resource->verified) {
    return true;
  }
  const VkResult wait = vkWaitForFences(device_, 1, &resource->fence, VK_TRUE,
                                        10'000'000'000ULL);
  if (wait == VK_TIMEOUT) {
    return fail(detail, "Vulkan fence timed out after 10 seconds");
  }
  if (wait != VK_SUCCESS) {
    return fail(detail,
                "vkWaitForFences failed: " + std::to_string(wait));
  }

  void* mapped = nullptr;
  VkResult result =
      vkMapMemory(device_, resource->readback_memory, 0, sizeof(GpuPayload), 0,
                  &mapped);
  if (result != VK_SUCCESS) {
    return fail(detail,
                "vkMapMemory(readback) failed: " + std::to_string(result));
  }
  const bool matches =
      std::memcmp(mapped, &resource->payload, sizeof(GpuPayload)) == 0;
  vkUnmapMemory(device_, resource->readback_memory);
  if (!matches) {
    return fail(detail, "GPU readback does not match the submitted payload");
  }
  resource->verified = true;
  if (detail != nullptr) {
    *detail = "GPU fence signalled and readback payload matched";
  }
  return true;
}

bool VulkanGpuBackend::complete_upload(const Transaction& tx,
                                       std::string* detail) {
  const auto it = staged_.find(tx.id);
  if (it == staged_.end()) {
    return fail(detail, "no staged GPU resource for transaction");
  }
  return wait_and_verify(&it->second, detail);
}

bool VulkanGpuBackend::commit_upload(const Transaction& tx,
                                     std::string* detail) {
  const auto it = staged_.find(tx.id);
  if (it == staged_.end()) {
    return fail(detail, "no staged GPU resource to commit");
  }
  TxResource& resource = it->second;
  if (!wait_and_verify(&resource, detail)) {
    return false;
  }

  if (active_.buffer != VK_NULL_HANDLE) {
    vkDestroyBuffer(device_, active_.buffer, nullptr);
  }
  if (active_.memory != VK_NULL_HANDLE) {
    vkFreeMemory(device_, active_.memory, nullptr);
  }
  active_.buffer = resource.device_buffer;
  active_.memory = resource.device_memory;
  active_.tx = tx.id;
  resource.device_buffer = VK_NULL_HANDLE;
  resource.device_memory = VK_NULL_HANDLE;
  destroy_tx(&resource);
  staged_.erase(it);

  if (detail != nullptr) {
    *detail = "verified device buffer promoted to active material";
  }
  return true;
}

bool VulkanGpuBackend::cancel_upload(const Transaction& tx,
                                     std::string* detail) {
  const auto it = staged_.find(tx.id);
  if (it == staged_.end()) {
    return fail(detail, "no staged GPU resource to cancel");
  }
  if (it->second.submitted) {
    const VkResult wait = vkWaitForFences(
        device_, 1, &it->second.fence, VK_TRUE, 10'000'000'000ULL);
    if (wait != VK_SUCCESS) {
      return fail(detail,
                  "vkWaitForFences during cancel failed: " +
                      std::to_string(wait));
    }
  }
  destroy_tx(&it->second);
  staged_.erase(it);
  if (detail != nullptr) {
    *detail = "staged GPU buffers and fence released after cancellation";
  }
  return true;
}

std::unique_ptr<GpuBackend> create_vulkan_backend(bool visible) {
  return std::make_unique<VulkanGpuBackend>(visible);
}

}  // namespace mi
