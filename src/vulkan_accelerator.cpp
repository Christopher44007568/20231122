#include "vulkan_accelerator.hpp"

#include "model.hpp"
#include "pose_error_spv.inc"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace kfc {

namespace {

struct GpuPose {
  float rotation[4] = {1.0F, 0.0F, 0.0F, 0.0F};
  float translation[4] = {0.0F, 0.0F, 0.0F, 0.0F};
};

struct alignas(16) GpuFrame {
  GpuPose bones[8];
  float timeUs = 0.0F;
  float pad[3] = {0.0F, 0.0F, 0.0F};
};

static_assert(sizeof(GpuFrame) == 272, "GPU frame layout must match shader");
static_assert(sizeof(GpuPose) == 32, "GPU pose layout must match shader");

constexpr std::size_t kFrameCapacityInitial = 512;
constexpr VkDeviceSize kBoundBufferSize = sizeof(GpuFrame) * 2;
constexpr VkDeviceSize kErrorBufferInitial = sizeof(float) * 4 * 512;

[[noreturn]] void failVk(const char* operation, VkResult result) {
  throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                           std::to_string(static_cast<int>(result)));
}

void requireVk(VkResult result, const char* operation) {
  if (result != VK_SUCCESS) failVk(operation, result);
}

std::uint32_t findMemoryType(VkPhysicalDevice physical,
                             std::uint32_t typeBits,
                             VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memory{};
  vkGetPhysicalDeviceMemoryProperties(physical, &memory);
  for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
    if ((typeBits & (1U << index)) != 0U &&
        (memory.memoryTypes[index].propertyFlags & properties) == properties) {
      return index;
    }
  }
  throw std::runtime_error("no compatible Vulkan memory type");
}

GpuFrame toGpu(const PoseFrame& frame) {
  GpuFrame result;
  for (std::size_t bone = 0; bone < kBoneCount; ++bone) {
    result.bones[bone].rotation[0] =
        static_cast<float>(frame.bones[bone].rotation.w);
    result.bones[bone].rotation[1] =
        static_cast<float>(frame.bones[bone].rotation.x);
    result.bones[bone].rotation[2] =
        static_cast<float>(frame.bones[bone].rotation.y);
    result.bones[bone].rotation[3] =
        static_cast<float>(frame.bones[bone].rotation.z);
    result.bones[bone].translation[0] =
        static_cast<float>(frame.bones[bone].translation.x);
    result.bones[bone].translation[1] =
        static_cast<float>(frame.bones[bone].translation.y);
    result.bones[bone].translation[2] =
        static_cast<float>(frame.bones[bone].translation.z);
  }
  result.timeUs = static_cast<float>(frame.timeUs);
  return result;
}

}  // namespace

VulkanAccelerator::~VulkanAccelerator() { cleanup(); }

std::size_t VulkanAccelerator::gpuFrameStride() const { return sizeof(GpuFrame); }

bool VulkanAccelerator::initialize(std::string* error) {
  try {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "skeleton-keyframe-lab";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "kfc-compute";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    requireVk(vkCreateInstance(&instanceInfo, nullptr, &instance_),
              "vkCreateInstance");

    std::uint32_t physicalCount = 0;
    requireVk(vkEnumeratePhysicalDevices(instance_, &physicalCount, nullptr),
              "vkEnumeratePhysicalDevices");
    if (physicalCount == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    requireVk(vkEnumeratePhysicalDevices(instance_, &physicalCount,
                                         physicalDevices.data()),
              "vkEnumeratePhysicalDevices");

    for (VkPhysicalDevice candidate : physicalDevices) {
      std::uint32_t familyCount = 0;
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
      std::vector<VkQueueFamilyProperties> families(familyCount);
      vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount,
                                               families.data());
      for (std::uint32_t family = 0; family < familyCount; ++family) {
        if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U) {
          physicalDevice_ = candidate;
          queueFamily_ = family;
          break;
        }
      }
      if (physicalDevice_ != VK_NULL_HANDLE) break;
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
      throw std::runtime_error("no Vulkan compute queue");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    probe_.deviceName = properties.deviceName;
    probe_.apiVersion = properties.apiVersion;
    probe_.driverVersion = properties.driverVersion;
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
    for (std::uint32_t heap = 0; heap < memory.memoryHeapCount; ++heap) {
      if ((memory.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) !=
          0U) {
        probe_.localHeapBytes += memory.memoryHeaps[heap].size;
      }
      if ((memory.memoryHeaps[heap].flags &
           VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) == 0U) {
        for (std::uint32_t type = 0; type < memory.memoryTypeCount; ++type) {
          if (memory.memoryTypes[type].heapIndex == heap &&
              (memory.memoryTypes[type].propertyFlags &
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U) {
            probe_.hostVisibleHeapBytes += memory.memoryHeaps[heap].size;
            break;
          }
        }
      }
    }

    const float priority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    requireVk(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_),
              "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    VkDescriptorSetLayoutBinding bindings[3]{};
    for (std::uint32_t i = 0; i < 3; ++i) {
      bindings[i].binding = i;
      bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
      bindings[i].descriptorCount = 1;
      bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 3;
    layoutInfo.pBindings = bindings;
    requireVk(vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr,
                                          &descriptorLayout_),
              "vkCreateDescriptorSetLayout");

    VkShaderModuleCreateInfo shaderInfo{};
    shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shaderInfo.codeSize = sizeof(kPoseErrorSpirv);
    shaderInfo.pCode = kPoseErrorSpirv;
    requireVk(vkCreateShaderModule(device_, &shaderInfo, nullptr, &shaderModule_),
              "vkCreateShaderModule");

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset = 0;
    push.size = sizeof(std::uint32_t) * 2;
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorLayout_;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &push;
    requireVk(vkCreatePipelineLayout(device_, &pipelineLayoutInfo, nullptr,
                                     &pipelineLayout_),
              "vkCreatePipelineLayout");

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = shaderModule_;
    pipelineInfo.stage.pName = "main";
    pipelineInfo.layout = pipelineLayout_;
    requireVk(vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                       nullptr, &pipeline_),
              "vkCreateComputePipelines");

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamily_;
    requireVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
              "vkCreateCommandPool");
    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commandPool_;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    requireVk(vkAllocateCommandBuffers(device_, &commandInfo, &commandBuffer_),
              "vkAllocateCommandBuffers");

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 3;
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.maxSets = 1;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    requireVk(vkCreateDescriptorPool(device_, &descriptorPoolInfo, nullptr,
                                     &descriptorPool_),
              "vkCreateDescriptorPool");
    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool_;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &descriptorLayout_;
    requireVk(vkAllocateDescriptorSets(device_, &allocateInfo, &descriptorSet_),
              "vkAllocateDescriptorSets");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    requireVk(vkCreateFence(device_, &fenceInfo, nullptr, &fence_),
              "vkCreateFence");
    probe_.available = true;
    detail_ = std::string(probe_.deviceName) +
              "; host-visible staging; FP32 SPIR-V compute";
    return true;
  } catch (const std::exception& exception) {
    if (error != nullptr) *error = exception.what();
    cleanup();
    return false;
  }
}

std::unique_ptr<VulkanAccelerator> VulkanAccelerator::create(std::string* error) {
  std::unique_ptr<VulkanAccelerator> accelerator(new VulkanAccelerator());
  std::string localError;
  if (!accelerator->initialize(&localError)) {
    if (error != nullptr) *error = localError;
    return nullptr;
  }
  return accelerator;
}

void VulkanAccelerator::cleanup() {
  if (device_ != VK_NULL_HANDLE) {
    vkDeviceWaitIdle(device_);
    if (frameMapped_ != nullptr && frameMemory_ != VK_NULL_HANDLE) {
      vkUnmapMemory(device_, frameMemory_);
    }
    if (boundMapped_ != nullptr && boundMemory_ != VK_NULL_HANDLE) {
      vkUnmapMemory(device_, boundMemory_);
    }
    if (errorMapped_ != nullptr && errorMemory_ != VK_NULL_HANDLE) {
      vkUnmapMemory(device_, errorMemory_);
    }
    if (frameBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, frameBuffer_, nullptr);
    if (frameMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, frameMemory_, nullptr);
    if (boundBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, boundBuffer_, nullptr);
    if (boundMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, boundMemory_, nullptr);
    if (errorBuffer_ != VK_NULL_HANDLE) vkDestroyBuffer(device_, errorBuffer_, nullptr);
    if (errorMemory_ != VK_NULL_HANDLE) vkFreeMemory(device_, errorMemory_, nullptr);
    if (fence_ != VK_NULL_HANDLE) vkDestroyFence(device_, fence_, nullptr);
    if (descriptorPool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
    }
    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
    if (pipeline_ != VK_NULL_HANDLE) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipelineLayout_ != VK_NULL_HANDLE) {
      vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    }
    if (shaderModule_ != VK_NULL_HANDLE) {
      vkDestroyShaderModule(device_, shaderModule_, nullptr);
    }
    if (descriptorLayout_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorSetLayout(device_, descriptorLayout_, nullptr);
    }
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
  device_ = VK_NULL_HANDLE;
  instance_ = VK_NULL_HANDLE;
}

bool VulkanAccelerator::uploadFrames(
    const std::vector<PoseFrame>& frames) {
  if (uploadedFrames_ == static_cast<const void*>(&frames) &&
      uploadedFrameCount_ == frames.size()) {
    return true;
  }
  std::size_t capacity = std::max<std::size_t>(kFrameCapacityInitial, 1);
  while (capacity < frames.size()) capacity *= 2;
  const VkDeviceSize frameBytes = static_cast<VkDeviceSize>(capacity) *
                                  static_cast<VkDeviceSize>(sizeof(GpuFrame));
  const VkDeviceSize errorBytes = static_cast<VkDeviceSize>(capacity) * 16U;

  if (capacity != frameCapacity_ || frameBuffer_ == VK_NULL_HANDLE) {
    if (frameMapped_ != nullptr) vkUnmapMemory(device_, frameMemory_);
    if (frameBuffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, frameBuffer_, nullptr);
      frameBuffer_ = VK_NULL_HANDLE;
    }
    if (frameMemory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, frameMemory_, nullptr);
      frameMemory_ = VK_NULL_HANDLE;
    }
    if (errorMapped_ != nullptr) vkUnmapMemory(device_, errorMemory_);
    if (errorBuffer_ != VK_NULL_HANDLE) {
      vkDestroyBuffer(device_, errorBuffer_, nullptr);
      errorBuffer_ = VK_NULL_HANDLE;
    }
    if (errorMemory_ != VK_NULL_HANDLE) {
      vkFreeMemory(device_, errorMemory_, nullptr);
      errorMemory_ = VK_NULL_HANDLE;
    }

    auto createHostBuffer = [&](VkDeviceSize size, VkBuffer* buffer,
                                VkDeviceMemory* memory, void** mapped) {
      VkBufferCreateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
      info.size = size;
      info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
      info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
      requireVk(vkCreateBuffer(device_, &info, nullptr, buffer),
                "vkCreateBuffer");
      VkMemoryRequirements requirements{};
      vkGetBufferMemoryRequirements(device_, *buffer, &requirements);
      VkMemoryAllocateInfo allocation{};
      allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
      allocation.allocationSize = requirements.size;
      allocation.memoryTypeIndex =
          findMemoryType(physicalDevice_, requirements.memoryTypeBits,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
      requireVk(vkAllocateMemory(device_, &allocation, nullptr, memory),
                "vkAllocateMemory");
      requireVk(vkBindBufferMemory(device_, *buffer, *memory, 0),
                "vkBindBufferMemory");
      requireVk(vkMapMemory(device_, *memory, 0, VK_WHOLE_SIZE, 0, mapped),
                "vkMapMemory");
    };
    createHostBuffer(frameBytes, &frameBuffer_, &frameMemory_, &frameMapped_);
    createHostBuffer(errorBytes, &errorBuffer_, &errorMemory_, &errorMapped_);
    frameCapacity_ = capacity;
  }
  if (boundBuffer_ == VK_NULL_HANDLE) {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = kBoundBufferSize;
    info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    requireVk(vkCreateBuffer(device_, &info, nullptr, &boundBuffer_),
              "vkCreateBuffer bounds");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, boundBuffer_, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        findMemoryType(physicalDevice_, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    requireVk(vkAllocateMemory(device_, &allocation, nullptr, &boundMemory_),
              "vkAllocateMemory bounds");
    requireVk(vkBindBufferMemory(device_, boundBuffer_, boundMemory_, 0),
              "vkBindBufferMemory bounds");
    requireVk(vkMapMemory(device_, boundMemory_, 0, VK_WHOLE_SIZE, 0,
                          &boundMapped_),
              "vkMapMemory bounds");
  }

  auto* destination = static_cast<GpuFrame*>(frameMapped_);
  for (std::size_t i = 0; i < frames.size(); ++i) {
    destination[i] = toGpu(frames[i]);
  }
  VkDescriptorBufferInfo frameInfo{frameBuffer_, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo boundInfo{boundBuffer_, 0, VK_WHOLE_SIZE};
  VkDescriptorBufferInfo errorInfo{errorBuffer_, 0, VK_WHOLE_SIZE};
  VkWriteDescriptorSet writes[3]{};
  const VkDescriptorBufferInfo* infos[3] = {&frameInfo, &boundInfo, &errorInfo};
  for (std::uint32_t i = 0; i < 3; ++i) {
    writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[i].dstSet = descriptorSet_;
    writes[i].dstBinding = i;
    writes[i].descriptorCount = 1;
    writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[i].pBufferInfo = infos[i];
  }
  vkUpdateDescriptorSets(device_, 3, writes, 0, nullptr);
  uploadedFrames_ = &frames;
  uploadedFrameCount_ = frames.size();
  return true;
}

bool VulkanAccelerator::uploadBounds(const PoseFrame& start,
                                     const PoseFrame& end) {
  if (boundMapped_ == nullptr) return false;
  auto* bounds = static_cast<GpuFrame*>(boundMapped_);
  bounds[0] = toGpu(start);
  bounds[1] = toGpu(end);
  return true;
}

IntervalError VulkanAccelerator::evaluate(
    const std::vector<PoseFrame>& frames, std::size_t startIndex,
    std::size_t endIndex) {
  IntervalError result;
  if (startIndex >= endIndex || endIndex >= frames.size()) return result;
  uploadFrames(frames);
  uploadBounds(frames[startIndex], frames[endIndex]);

  requireVk(vkResetCommandBuffer(commandBuffer_, 0),
            "vkResetCommandBuffer");
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  requireVk(vkBeginCommandBuffer(commandBuffer_, &beginInfo),
            "vkBeginCommandBuffer");
  vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
  vkCmdBindDescriptorSets(commandBuffer_, VK_PIPELINE_BIND_POINT_COMPUTE,
                          pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
  VkMemoryBarrier hostToCompute{};
  hostToCompute.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  hostToCompute.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  hostToCompute.dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_HOST_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &hostToCompute, 0, nullptr, 0, nullptr);
  const std::uint32_t indices[2] = {
      static_cast<std::uint32_t>(startIndex),
      static_cast<std::uint32_t>(endIndex)};
  vkCmdPushConstants(commandBuffer_, pipelineLayout_,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(indices), indices);
  const std::uint32_t workgroups =
      (static_cast<std::uint32_t>(endIndex) + 63U) / 64U;
  vkCmdDispatch(commandBuffer_, workgroups, 1, 1);
  VkMemoryBarrier computeToHost{};
  computeToHost.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  computeToHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  computeToHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(commandBuffer_, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &computeToHost, 0,
                       nullptr, 0, nullptr);
  requireVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

  requireVk(vkResetFences(device_, 1, &fence_), "vkResetFences");
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &commandBuffer_;
  requireVk(vkQueueSubmit(queue_, 1, &submit, fence_), "vkQueueSubmit");
  requireVk(vkWaitForFences(device_, 1, &fence_, VK_TRUE,
                            std::numeric_limits<std::uint64_t>::max()),
            "vkWaitForFences");

  const auto* errors = static_cast<const float*>(errorMapped_);
  for (std::size_t index = startIndex + 1; index < endIndex; ++index) {
    result.maxAngularDeg = std::max(
        result.maxAngularDeg,
        static_cast<double>(errors[index * 4]));
    result.maxTranslationM = std::max(
        result.maxTranslationM,
        static_cast<double>(errors[index * 4 + 1]));
  }
  return result;
}

const std::string& VulkanAccelerator::name() const {
  static const std::string value = "vulkan-compute";
  return value;
}

const std::string& VulkanAccelerator::detail() const { return detail_; }

VulkanProbe VulkanAccelerator::probeOnly() {
  VulkanProbe result;
  VkInstance instance = VK_NULL_HANDLE;
  try {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "skeleton-keyframe-lab-probe";
    application.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    requireVk(vkCreateInstance(&info, nullptr, &instance), "vkCreateInstance");
    std::uint32_t count = 0;
    requireVk(vkEnumeratePhysicalDevices(instance, &count, nullptr),
              "vkEnumeratePhysicalDevices");
    if (count == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    requireVk(vkEnumeratePhysicalDevices(instance, &count, devices.data()),
              "vkEnumeratePhysicalDevices");
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(devices[0], &properties);
    result.deviceName = properties.deviceName;
    result.apiVersion = properties.apiVersion;
    result.driverVersion = properties.driverVersion;
    result.available = true;
    vkDestroyInstance(instance, nullptr);
  } catch (const std::exception& exception) {
    result.error = exception.what();
    if (instance != VK_NULL_HANDLE) vkDestroyInstance(instance, nullptr);
  }
  return result;
}

}  // namespace kfc
