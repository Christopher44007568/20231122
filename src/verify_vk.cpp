#include "verify_vk.hpp"

#include <cstring>
#include <iostream>

#include "sha256.hpp"
#include "verify_comp_spv.hpp"

namespace nmts {
namespace {

std::string vk_err(VkResult r) {
  return std::string("VkResult=") + std::to_string(int(r));
}

struct ParamBlock {
  std::uint32_t total_instances;
  std::uint32_t first_instance;
  float eps_orthogonal;
  float eps_unit;
  float eps_normal_z;
};

}  // namespace

VulkanBackend* VulkanBackend::create(const BackendOptions& opt,
                                     DegradeReason& reason,
                                     std::string& detail,
                                     BackendCounters& counters) {
  auto* b = new VulkanBackend();
  b->opt_ = opt;
  b->counters_ = &counters;
  if (!b->init(opt, reason, detail, counters)) {
    b->destroy();
    delete b;
    return nullptr;
  }
  return b;
}

bool VulkanBackend::init(const BackendOptions& opt, DegradeReason& reason,
                         std::string& detail, BackendCounters& counters) {
  if (opt.force_cpu) {
    reason = kDegradeForcedByFlag;
    detail = "NMTS_FORCE_CPU set";
    return false;
  }
  if (!glfwInit()) {
    reason = kDegradeGlfwUnavailable;
    detail = "glfwInit returned GLFW_FALSE";
    return false;
  }
  glfw_initialized_ = true;
  if (!glfwVulkanSupported()) {
    reason = kDegradeNoPhysicalDevice;
    detail = "glfwVulkanSupported reported no Vulkan loader";
    return false;
  }

  uint32_t ext_count = 0;
  const char** exts = glfwGetRequiredInstanceExtensions(&ext_count);
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = "nmts_verify";
  app.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  app.pEngineName = "nmts";
  app.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  app.apiVersion = VK_API_VERSION_1_1;
  VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  ici.pApplicationInfo = &app;
  ici.enabledExtensionCount = ext_count;
  ici.ppEnabledExtensionNames = exts;
  VkResult r = vkCreateInstance(&ici, nullptr, &instance_);
  if (r != VK_SUCCESS) {
    reason = kDegradeNoPhysicalDevice;
    detail = "vkCreateInstance " + vk_err(r);
    return false;
  }

  uint32_t dev_count = 0;
  vkEnumeratePhysicalDevices(instance_, &dev_count, nullptr);
  if (dev_count == 0) {
    reason = kDegradeNoPhysicalDevice;
    detail = "vkEnumeratePhysicalDevices returned zero devices";
    return false;
  }
  std::vector<VkPhysicalDevice> devs(dev_count);
  vkEnumeratePhysicalDevices(instance_, &dev_count, devs.data());
  physical_ = devs.front();

  uint32_t qf_count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, nullptr);
  std::vector<VkQueueFamilyProperties> qfs(qf_count);
  vkGetPhysicalDeviceQueueFamilyProperties(physical_, &qf_count, qfs.data());
  queue_family_ = UINT32_MAX;
  for (uint32_t i = 0; i < qf_count; ++i) {
    if (qfs[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
      queue_family_ = i;
      break;
    }
  }
  if (queue_family_ == UINT32_MAX) {
    reason = kDegradeNoComputeQueue;
    detail = "no compute-capable queue family";
    return false;
  }
  float prio = 1.0f;
  VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
  qci.queueFamilyIndex = queue_family_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &prio;
  VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  r = vkCreateDevice(physical_, &dci, nullptr, &device_);
  if (r != VK_SUCCESS) {
    reason = kDegradeAllocationFailed;
    detail = "vkCreateDevice " + vk_err(r);
    counters.allocation_failures++;
    return false;
  }
  vkGetDeviceQueue(device_, queue_family_, 0, &queue_);

  VkCommandPoolCreateInfo cpci{
      VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  cpci.queueFamilyIndex = queue_family_;
  r = vkCreateCommandPool(device_, &cpci, nullptr, &cmd_pool_);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "vkCreateCommandPool " + vk_err(r);
    return false;
  }

  VkDescriptorSetLayoutBinding binds[4]{};
  for (uint32_t i = 0; i < 4; ++i) {
    binds[i].binding = i;
    binds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    binds[i].descriptorCount = 1;
    binds[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  }
  VkDescriptorSetLayoutCreateInfo dlci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  dlci.bindingCount = 4;
  dlci.pBindings = binds;
  r = vkCreateDescriptorSetLayout(device_, &dlci, nullptr, &desc_layout_);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "descriptor set layout " + vk_err(r);
    return false;
  }
  VkPipelineLayoutCreateInfo plci{
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  plci.setLayoutCount = 1;
  plci.pSetLayouts = &desc_layout_;
  r = vkCreatePipelineLayout(device_, &plci, nullptr, &pipe_layout_);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "pipeline layout " + vk_err(r);
    return false;
  }
  VkShaderModuleCreateInfo smci{
      VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  smci.codeSize = kVerifySpvSize;
  smci.pCode = reinterpret_cast<const uint32_t*>(kVerifySpv);
  VkShaderModule sm = VK_NULL_HANDLE;
  r = vkCreateShaderModule(device_, &smci, nullptr, &sm);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "shader module " + vk_err(r);
    return false;
  }
  VkComputePipelineCreateInfo cpci2{
      VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
  cpci2.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  cpci2.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  cpci2.stage.module = sm;
  cpci2.stage.pName = "main";
  cpci2.layout = pipe_layout_;
  r = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &cpci2,
                               nullptr, &pipeline_);
  vkDestroyShaderModule(device_, sm, nullptr);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "compute pipeline " + vk_err(r);
    return false;
  }

  VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};
  VkDescriptorPoolCreateInfo dpci{
      VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  dpci.maxSets = 1;
  dpci.poolSizeCount = 1;
  dpci.pPoolSizes = &ps;
  r = vkCreateDescriptorPool(device_, &dpci, nullptr, &desc_pool_);
  if (r != VK_SUCCESS) {
    reason = kDegradePipelineFailed;
    detail = "descriptor pool " + vk_err(r);
    return false;
  }
  return true;
}

uint32_t VulkanBackend::find_memory_type(uint32_t type_bits,
                                         VkMemoryPropertyFlags props) {
  VkPhysicalDeviceMemoryProperties mp;
  vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
  for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
    if ((type_bits & (1u << i)) &&
        (mp.memoryTypes[i].propertyFlags & props) == props)
      return i;
  }
  return UINT32_MAX;
}

bool VulkanBackend::create_storage_buffer(VkDeviceSize size, VkBuffer& buf,
                                          VkDeviceMemory& mem,
                                          std::string& err) {
  counters_->allocation_attempts++;
  if (opt_.force_gpu_oom) {
    counters_->allocation_failures++;
    err = "simulated GPU OOM (NMTS_FORCE_GPU_OOM)";
    return false;
  }
  VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bci.size = size;
  bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
  bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  VkResult r = vkCreateBuffer(device_, &bci, nullptr, &buf);
  if (r != VK_SUCCESS) {
    counters_->allocation_failures++;
    err = "vkCreateBuffer " + vk_err(r);
    return false;
  }
  VkMemoryRequirements mr;
  vkGetBufferMemoryRequirements(device_, buf, &mr);
  uint32_t mt =
      find_memory_type(mr.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (mt == UINT32_MAX) {
    counters_->allocation_failures++;
    err = "no host-visible memory type";
    return false;
  }
  VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
  mai.allocationSize = mr.size;
  mai.memoryTypeIndex = mt;
  r = vkAllocateMemory(device_, &mai, nullptr, &mem);
  if (r != VK_SUCCESS) {
    counters_->allocation_failures++;
    err = "vkAllocateMemory " + vk_err(r);
    return false;
  }
  vkBindBufferMemory(device_, buf, mem, 0);
  return true;
}

void VulkanBackend::destroy() {
  if (device_) {
    vkDeviceWaitIdle(device_);
    if (desc_pool_) vkDestroyDescriptorPool(device_, desc_pool_, nullptr);
    if (pipeline_) vkDestroyPipeline(device_, pipeline_, nullptr);
    if (pipe_layout_)
      vkDestroyPipelineLayout(device_, pipe_layout_, nullptr);
    if (desc_layout_)
      vkDestroyDescriptorSetLayout(device_, desc_layout_, nullptr);
    if (cmd_pool_) vkDestroyCommandPool(device_, cmd_pool_, nullptr);
    vkDestroyDevice(device_, nullptr);
  }
  if (instance_) vkDestroyInstance(instance_, nullptr);
  if (glfw_initialized_) glfwTerminate();
  instance_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  glfw_initialized_ = false;
}

VulkanBackend::~VulkanBackend() { destroy(); }

bool VulkanBackend::verify_range(const Material& m,
                                 std::uint32_t first,
                                 std::uint32_t count,
                                 std::vector<ResultRecord>& out,
                                 std::string& err) {
  if (count == 0) return true;
  VkDeviceSize inst_bytes =
      VkDeviceSize(count) * kInstanceSize;
  VkDeviceSize px_bytes =
      VkDeviceSize(m.header.width * m.header.height) * kPixelSize;
  VkDeviceSize res_bytes = VkDeviceSize(count) * kResultRecordSize;
  VkDeviceSize param_bytes = sizeof(ParamBlock);

  VkBuffer b_inst = VK_NULL_HANDLE, b_px = VK_NULL_HANDLE;
  VkBuffer b_res = VK_NULL_HANDLE, b_param = VK_NULL_HANDLE;
  VkDeviceMemory m_inst = VK_NULL_HANDLE, m_px = VK_NULL_HANDLE;
  VkDeviceMemory m_res = VK_NULL_HANDLE, m_param = VK_NULL_HANDLE;
  bool ok = create_storage_buffer(inst_bytes, b_inst, m_inst, err) &&
            create_storage_buffer(px_bytes, b_px, m_px, err) &&
            create_storage_buffer(res_bytes, b_res, m_res, err) &&
            create_storage_buffer(param_bytes, b_param, m_param, err);
  if (!ok) {
    if (b_inst) vkDestroyBuffer(device_, b_inst, nullptr);
    if (b_px) vkDestroyBuffer(device_, b_px, nullptr);
    if (b_res) vkDestroyBuffer(device_, b_res, nullptr);
    if (b_param) vkDestroyBuffer(device_, b_param, nullptr);
    if (m_inst) vkFreeMemory(device_, m_inst, nullptr);
    if (m_px) vkFreeMemory(device_, m_px, nullptr);
    if (m_res) vkFreeMemory(device_, m_res, nullptr);
    if (m_param) vkFreeMemory(device_, m_param, nullptr);
    return false;
  }

  void* mapped = nullptr;
  vkMapMemory(device_, m_inst, 0, inst_bytes, 0, &mapped);
  std::memcpy(mapped, m.instances.data() + first, inst_bytes);
  vkUnmapMemory(device_, m_inst);
  vkMapMemory(device_, m_px, 0, px_bytes, 0, &mapped);
  std::memcpy(mapped, m.pixels.data(), px_bytes);
  vkUnmapMemory(device_, m_px);
  ParamBlock param{count, first, m.header.eps_orthogonal,
                   m.header.eps_unit, m.header.eps_normal_z};
  vkMapMemory(device_, m_param, 0, param_bytes, 0, &mapped);
  std::memcpy(mapped, &param, sizeof(param));
  vkUnmapMemory(device_, m_param);

  VkDescriptorSetLayout layouts[1] = {desc_layout_};
  VkDescriptorSetAllocateInfo dsai{
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  dsai.descriptorPool = desc_pool_;
  dsai.descriptorSetCount = 1;
  dsai.pSetLayouts = layouts;
  VkDescriptorSet ds = VK_NULL_HANDLE;
  if (vkAllocateDescriptorSets(device_, &dsai, &ds) != VK_SUCCESS) {
    err = "vkAllocateDescriptorSets failed";
    ok = false;
  }
  VkDescriptorBufferInfo bi[4] = {{b_inst, 0, inst_bytes},
                                  {b_px, 0, px_bytes},
                                  {b_res, 0, res_bytes},
                                  {b_param, 0, param_bytes}};
  VkWriteDescriptorSet wds[4]{};
  for (int i = 0; i < 4 && ok; ++i) {
    wds[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wds[i].dstSet = ds;
    wds[i].dstBinding = uint32_t(i);
    wds[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    wds[i].descriptorCount = 1;
    wds[i].pBufferInfo = &bi[i];
  }
  if (ok) vkUpdateDescriptorSets(device_, 4, wds, 0, nullptr);

  VkCommandBufferAllocateInfo cbai{
      VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
  cbai.commandPool = cmd_pool_;
  cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  cbai.commandBufferCount = 1;
  VkCommandBuffer cb = VK_NULL_HANDLE;
  if (ok &&
      vkAllocateCommandBuffers(device_, &cbai, &cb) != VK_SUCCESS) {
    err = "vkAllocateCommandBuffers failed";
    ok = false;
  }
  if (ok) {
    VkCommandBufferBeginInfo cbbi{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    cbbi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &cbbi);
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipe_layout_, 0, 1, &ds, 0, nullptr);
    uint32_t groups = (count + 31u) / 32u;
    vkCmdDispatch(cb, groups, 1, 1);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkResult sr = VK_NOT_READY;
    for (int attempt = 0;
         attempt < 8 && (sr == VK_NOT_READY || sr == VK_TIMEOUT);
         ++attempt)
      sr = vkQueueSubmit(queue_, 1, &si, VK_NULL_HANDLE);
    if (sr != VK_SUCCESS) {
      err = "vkQueueSubmit " + vk_err(sr);
      ok = false;
    } else {
      vkQueueWaitIdle(queue_);
    }
  }

  if (ok) {
    vkMapMemory(device_, m_res, 0, res_bytes, 0, &mapped);
    out.resize(count);
    std::memcpy(out.data(), mapped, res_bytes);
    vkUnmapMemory(device_, m_res);
    // Host-side fixups: SHA-256 digest and original-file offsets.
    for (uint32_t i = 0; i < count; ++i) {
      uint32_t global_idx = first + i;
      const InstanceRecord& q = m.instances[global_idx];
      const auto& px = m.pixels;
      std::size_t poff = std::size_t(q.pixel_index) * kPixelSize;
      Sha256 h;
      h.update(reinterpret_cast<const std::uint8_t*>(&q), sizeof(q));
      h.update(px.data() + poff, 4);
      auto digest = h.finish();
      std::memcpy(out[i].check_digest, digest.data(), 32);
      out[i].set_instance_offset(
          m.header.instance_offset +
          std::uint64_t(global_idx) * kInstanceSize);
      out[i].set_evidence_offset(
          m.header.pixel_offset +
          std::uint64_t(q.pixel_index) * kPixelSize);
    }
  }

  vkDestroyBuffer(device_, b_inst, nullptr);
  vkDestroyBuffer(device_, b_px, nullptr);
  vkDestroyBuffer(device_, b_res, nullptr);
  vkDestroyBuffer(device_, b_param, nullptr);
  vkFreeMemory(device_, m_inst, nullptr);
  vkFreeMemory(device_, m_px, nullptr);
  vkFreeMemory(device_, m_res, nullptr);
  vkFreeMemory(device_, m_param, nullptr);
  return ok;
}

}  // namespace nmts
