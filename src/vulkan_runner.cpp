#include "vulkan_runner.h"

#include <GLFW/glfw3.h>

#include <atomic>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>
#include <thread>

namespace audit {

namespace {

std::string vkResultName(VkResult result) {
  switch (result) {
    case VK_SUCCESS:
      return "VK_SUCCESS";
    case VK_NOT_READY:
      return "VK_NOT_READY";
    case VK_TIMEOUT:
      return "VK_TIMEOUT";
    case VK_INCOMPLETE:
      return "VK_INCOMPLETE";
    default: {
      std::ostringstream oss;
      oss << "VkResult(" << static_cast<int>(result) << ")";
      return oss.str();
    }
  }
}

std::string utcTimestamp() {
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
  gmtime_r(&t, &tm);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buffer;
}

}  // namespace

VulkanRunner::~VulkanRunner() { shutdown(); }

void VulkanRunner::shutdown() {
  if (device_ != VK_NULL_HANDLE) vkDeviceWaitIdle(device_);
  if (framebuffer_ != VK_NULL_HANDLE)
    vkDestroyFramebuffer(device_, framebuffer_, nullptr);
  if (renderPass_ != VK_NULL_HANDLE)
    vkDestroyRenderPass(device_, renderPass_, nullptr);
  if (colorView_ != VK_NULL_HANDLE)
    vkDestroyImageView(device_, colorView_, nullptr);
  if (colorImage_ != VK_NULL_HANDLE)
    vkDestroyImage(device_, colorImage_, nullptr);
  if (colorMemory_ != VK_NULL_HANDLE)
    vkFreeMemory(device_, colorMemory_, nullptr);
  if (queryPool_ != VK_NULL_HANDLE)
    vkDestroyQueryPool(device_, queryPool_, nullptr);
  if (commandPool_ != VK_NULL_HANDLE)
    vkDestroyCommandPool(device_, commandPool_, nullptr);
  if (device_ != VK_NULL_HANDLE) vkDestroyDevice(device_, nullptr);
  if (instance_ != VK_NULL_HANDLE) vkDestroyInstance(instance_, nullptr);
  if (window_ != nullptr) glfwDestroyWindow(window_);
  if (glfwReady_) glfwTerminate();
  framebuffer_ = VK_NULL_HANDLE;
  renderPass_ = VK_NULL_HANDLE;
  colorView_ = VK_NULL_HANDLE;
  colorImage_ = VK_NULL_HANDLE;
  colorMemory_ = VK_NULL_HANDLE;
  queryPool_ = VK_NULL_HANDLE;
  commandPool_ = VK_NULL_HANDLE;
  device_ = VK_NULL_HANDLE;
  instance_ = VK_NULL_HANDLE;
  window_ = nullptr;
  glfwReady_ = false;
}

bool VulkanRunner::initialize(std::string& error) {
  if (!createGlfwWindow()) {
    error = "GLFW 初始化或窗口创建失败";
    return false;
  }
  if (!createInstance()) {
    error = "Vulkan 实例创建失败";
    return false;
  }
  if (!pickPhysicalDevice()) {
    error = "未找到支持图形队列的物理设备";
    return false;
  }
  if (!createLogicalDevice()) {
    error = "逻辑设备创建失败";
    return false;
  }
  if (!createRenderingArtifacts()) {
    error = "渲染通道 / 查询池等资源创建失败";
    return false;
  }
  return true;
}

bool VulkanRunner::createGlfwWindow() {
  if (glfwInit() != GLFW_TRUE) return false;
  glfwReady_ = true;
  int major = 0, minor = 0, rev = 0;
  glfwGetVersion(&major, &minor, &rev);
  std::ostringstream oss;
  oss << major << "." << minor << "." << rev;
  environment_.glfwVersion = oss.str();

  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
  window_ = glfwCreateWindow(640, 480, "occlusion-audit", nullptr, nullptr);
  if (window_ == nullptr) {
    environment_.windowModeReason =
        "GLFW_NO_API 隐藏窗口创建失败，进入无窗口降级路径";
    return false;
  }
  environment_.hiddenWindow = true;
  environment_.windowModeReason =
      "使用 GLFW_NO_API + GLFW_VISIBLE=FALSE 的离屏窗口承载 Vulkan";
  return true;
}

bool VulkanRunner::createInstance() {
  uint32_t requiredCount = 0;
  const char** required =
      glfwGetRequiredInstanceExtensions(&requiredCount);
  std::vector<const char*> extensions;
  for (uint32_t i = 0; i < requiredCount; ++i) extensions.push_back(required[i]);

  VkApplicationInfo appInfo{};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "occlusion-audit";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "audit-engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_2;
  environment_.instanceApi = "Vulkan 1.2 (遮挡查询核心 API)";

  VkInstanceCreateInfo ci{};
  ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  ci.pApplicationInfo = &appInfo;
  ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  ci.ppEnabledExtensionNames = extensions.data();
  if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) return false;

  for (uint32_t i = 0; i < requiredCount; ++i) {
    if (std::strcmp(required[i], VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
      environment_.surfaceSupported = true;
    }
  }
  return true;
}

bool VulkanRunner::pickPhysicalDevice() {
  uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) return false;
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  for (VkPhysicalDevice device : devices) {
    uint32_t qfCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(qfCount);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &qfCount, families.data());
    for (uint32_t i = 0; i < qfCount; ++i) {
      if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        physicalDevice_ = device;
        queueFamily_ = i;
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(device, &props);
        environment_.deviceName = props.deviceName;
        environment_.apiVersion = props.apiVersion;
        environment_.driverVersion = props.driverVersion;
        return true;
      }
    }
  }
  return false;
}

bool VulkanRunner::createLogicalDevice() {
  float priority = 1.0f;
  VkDeviceQueueCreateInfo qci{};
  qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  qci.queueFamilyIndex = queueFamily_;
  qci.queueCount = 1;
  qci.pQueuePriorities = &priority;

  VkPhysicalDeviceFeatures features{};
  VkDeviceCreateInfo dci{};
  dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  dci.queueCreateInfoCount = 1;
  dci.pQueueCreateInfos = &qci;
  dci.pEnabledFeatures = &features;
  return vkCreateDevice(physicalDevice_, &dci, nullptr, &device_) ==
         VK_SUCCESS;
}

bool VulkanRunner::createRenderingArtifacts() {
  vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

  VkCommandPoolCreateInfo poolInfo{};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  poolInfo.queueFamilyIndex = queueFamily_;
  if (vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_) !=
      VK_SUCCESS) {
    return false;
  }

  VkQueryPoolCreateInfo queryInfo{};
  queryInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  queryInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
  queryInfo.queryCount = kQueryCount;
  if (vkCreateQueryPool(device_, &queryInfo, nullptr, &queryPool_) !=
      VK_SUCCESS) {
    return false;
  }

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
  imageInfo.extent = {16, 16, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                    VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(device_, &imageInfo, nullptr, &colorImage_) !=
      VK_SUCCESS) {
    return false;
  }

  VkMemoryRequirements memReq{};
  vkGetImageMemoryRequirements(device_, colorImage_, &memReq);
  VkPhysicalDeviceMemoryProperties memProps{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memProps);
  uint32_t memoryType = 0;
  bool found = false;
  for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
    if ((memReq.memoryTypeBits & (1u << i)) &&
        (memProps.memoryTypes[i].propertyFlags &
         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      memoryType = i;
      found = true;
      break;
    }
  }
  if (!found) return false;
  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memReq.size;
  allocInfo.memoryTypeIndex = memoryType;
  if (vkAllocateMemory(device_, &allocInfo, nullptr, &colorMemory_) !=
      VK_SUCCESS) {
    return false;
  }
  if (vkBindImageMemory(device_, colorImage_, colorMemory_, 0) !=
      VK_SUCCESS) {
    return false;
  }

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = colorImage_;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = imageInfo.format;
  viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  if (vkCreateImageView(device_, &viewInfo, nullptr, &colorView_) !=
      VK_SUCCESS) {
    return false;
  }

  VkAttachmentDescription attachment{};
  attachment.format = imageInfo.format;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference colorRef{
      0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;
  VkRenderPassCreateInfo rpInfo{};
  rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  rpInfo.attachmentCount = 1;
  rpInfo.pAttachments = &attachment;
  rpInfo.subpassCount = 1;
  rpInfo.pSubpasses = &subpass;
  if (vkCreateRenderPass(device_, &rpInfo, nullptr, &renderPass_) !=
      VK_SUCCESS) {
    return false;
  }

  VkFramebufferCreateInfo fbInfo{};
  fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
  fbInfo.renderPass = renderPass_;
  fbInfo.attachmentCount = 1;
  fbInfo.pAttachments = &colorView_;
  fbInfo.width = 16;
  fbInfo.height = 16;
  fbInfo.layers = 1;
  if (vkCreateFramebuffer(device_, &fbInfo, nullptr, &framebuffer_) !=
      VK_SUCCESS) {
    return false;
  }
  return true;
}

void VulkanRunner::appendStep(ScenarioRecord& rec, const std::string& stage,
                              const std::string& detail,
                              const std::string& ref) {
  rec.steps.push_back(
      ProcessingStep{static_cast<int>(rec.steps.size()) + 1, stage, detail,
                     ref});
}

QueryResultEvidence VulkanRunner::readQuery(uint32_t slot) {
  struct QueryPayload {
    uint64_t samples;
    uint64_t availability;
  } payload{0xDEADBEEFCAFEBABEull, 0x0123456789ABCDEFull};
  const VkQueryResultFlags flags =
      VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
  VkResult result = vkGetQueryPoolResults(
      device_, queryPool_, slot, 1, sizeof(payload), &payload,
      sizeof(payload), flags);
  QueryResultEvidence evidence;
  evidence.slot = slot;
  evidence.vkResult = static_cast<int>(result);
  evidence.resultFlags = flags;
  evidence.sampleCount = payload.samples;
  evidence.availability = payload.availability;
  evidence.readable = (result == VK_SUCCESS) && (payload.availability == 1);
  return evidence;
}

namespace {

VkCommandBuffer beginOneShot(VkDevice device, VkCommandPool pool) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = pool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  VkCommandBuffer cmd = VK_NULL_HANDLE;
  vkAllocateCommandBuffers(device, &allocInfo, &cmd);
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
  vkBeginCommandBuffer(cmd, &beginInfo);
  return cmd;
}

void beginPass(VkCommandBuffer cmd, VkRenderPass renderPass,
               VkFramebuffer framebuffer) {
  VkRenderPassBeginInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  info.renderPass = renderPass;
  info.framebuffer = framebuffer;
  info.renderArea = {{0, 0}, {16, 16}};
  VkClearValue clear{};
  clear.color = {{0.05f, 0.07f, 0.10f, 1.0f}};
  info.clearValueCount = 1;
  info.pClearValues = &clear;
  vkCmdBeginRenderPass(cmd, &info, VK_SUBPASS_CONTENTS_INLINE);
}

}  // namespace

ScenarioRecord VulkanRunner::runCompleted() {
  ScenarioRecord rec;
  rec.id = "S1-completed";
  rec.kind = ScenarioKind::Completed;
  rec.title = "场景 1：遮挡查询完整执行（基准对照）";
  rec.input = RawQueryInput{0, "box-A 可见性统计",
                            "begin/end 成对录制并提交、等待完成", true, true,
                            true};
  appendStep(rec, "reset", "vkResetQueryPool 重置槽 0", "EV-POOL-RESET");
  appendStep(rec, "record",
             "vkCmdBeginQuery(slot=0) 与 vkCmdEndQuery(slot=0) 成对录制",
             "EV-CMD-BEGINEND");
  appendStep(rec, "submit", "vkQueueSubmit 提交并以 fence 等待执行完成",
             "EV-SUBMIT-WAIT");
  appendStep(rec, "readback",
             "vkGetQueryPoolResults 使用 64 位 + WITH_AVAILABILITY 读取",
             "EV-READBACK");

  vkResetQueryPool(device_, queryPool_, 0, kQueryCount);
  VkCommandBuffer cmd = beginOneShot(device_, commandPool_);
  beginPass(cmd, renderPass_, framebuffer_);
  vkCmdBeginQuery(cmd, queryPool_, 0, 0);
  vkCmdEndQuery(cmd, queryPool_, 0);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(device_, &fenceInfo, nullptr, &fence);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  vkQueueSubmit(queue_, 1, &submit, fence);
  vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
  rec.result = readQuery(0);
  vkDestroyFence(device_, fence, nullptr);
  vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

  if (rec.result.readable && rec.result.sampleCount > 0) {
    rec.verdict = AutoVerdict::NotOccluded;
    rec.verdictReason = "查询已完成且通过样本数大于 0";
  } else if (rec.result.readable) {
    rec.verdict = AutoVerdict::Occluded;
    rec.verdictReason = "查询已完成且通过样本数为 0";
  } else {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason = "读取失败，无法形成自动结论";
  }
  rec.manualStatus = "待人工确认";
  rec.manualNote = "请核对样本数是否与场景几何（清屏后无绘制）一致。";
  rec.followUpActions = {
      "基准场景已成功：将本次原始读取归档为后续取消/边界场景的对照基线"};
  return rec;
}

ScenarioRecord VulkanRunner::runRecordingCancelled() {
  ScenarioRecord rec;
  rec.id = "S2-cancel-recording";
  rec.kind = ScenarioKind::CancelledDuringRecording;
  rec.title = "场景 2：录制阶段中途取消（命令缓冲从未提交）";
  rec.input = RawQueryInput{
      1, "box-B 可见性统计",
      "录制 begin 后收到取消信号，vkResetCommandBuffer 放弃录制", true,
      false, false};
  appendStep(rec, "reset", "vkResetQueryPool 重置槽 1", "EV-POOL-RESET");
  appendStep(rec, "record", "vkCmdBeginQuery(slot=1) 已录制，尚未 end",
             "EV-CMD-BEGIN-ONLY");
  appendStep(rec, "cancel",
             "收到取消信号：vkResetCommandBuffer 丢弃整段录制，不提交队列",
             "EV-CANCEL-RECORDING");
  appendStep(rec, "readback", "对槽 1 执行读取，验证可用性位",
             "EV-READBACK-CANCELLED");

  vkResetQueryPool(device_, queryPool_, 0, kQueryCount);
  VkCommandBuffer cmd = beginOneShot(device_, commandPool_);
  beginPass(cmd, renderPass_, framebuffer_);
  vkCmdBeginQuery(cmd, queryPool_, 1, 0);
  VkResult resetResult = vkResetCommandBuffer(cmd, 0);
  appendStep(rec, "cancel",
             std::string("vkResetCommandBuffer 返回 ") +
                 vkResultName(resetResult),
             "EV-CANCEL-RECORDING-VK");
  rec.result = readQuery(1);
  vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

  if (!rec.result.readable) {
    rec.verdict = AutoVerdict::Cancelled;
    rec.verdictReason =
        "命令缓冲在录制阶段被重置且从未提交，GPU 未执行该查询";
  } else {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason = "取消后查询仍可读，与预期不符，需要核查驱动行为";
  }
  rec.manualStatus = "待人工确认";
  rec.manualNote = "确认取消信号来源与时间戳，确认无其它提交路径触达槽 1。";
  rec.followUpActions = {
      "复核取消信号的产生日志（EV-CANCEL-RECORDING）并归档",
      "若仍需可见性结论，重新录制完整 begin/end 后再次提交",
      "在应用层为该对象标记“本次无统计数据”，禁止复用上帧结果"};
  return rec;
}

ScenarioRecord VulkanRunner::runExecutionCancelled() {
  ScenarioRecord rec;
  rec.id = "S3-cancel-execution";
  rec.kind = ScenarioKind::CancelledDuringExecution;
  rec.title = "场景 3：执行阶段中途取消（帧在途时关闭读取窗口）";
  rec.input = RawQueryInput{
      2, "box-C 可见性统计",
      "完整录制并提交；工作线程等待帧完成期间，主线程下达取消信号",
      true, true, true};
  appendStep(rec, "reset", "vkResetQueryPool 重置槽 2", "EV-POOL-RESET");
  appendStep(rec, "record", "槽 2 成对录制 begin/end",
             "EV-CMD-BEGINEND");
  appendStep(rec, "submit",
             "工作线程 vkQueueSubmit 提交帧并关联未信号 fence",
             "EV-SUBMIT-FENCE");
  appendStep(rec, "cancel",
             "主线程在帧完成前置位取消标志；工作线程以 timeout=0 轮询，"
             "命中 VK_TIMEOUT 且取消标志有效时放弃本次读取",
             "EV-CANCEL-EXECUTION");

  vkResetQueryPool(device_, queryPool_, 0, kQueryCount);
  VkCommandBuffer cmd = beginOneShot(device_, commandPool_);
  beginPass(cmd, renderPass_, framebuffer_);
  vkCmdBeginQuery(cmd, queryPool_, 2, 0);
  vkCmdEndQuery(cmd, queryPool_, 2);
  vkCmdEndRenderPass(cmd);
  vkEndCommandBuffer(cmd);

  VkFenceCreateInfo fenceInfo{};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  VkFence fence = VK_NULL_HANDLE;
  vkCreateFence(device_, &fenceInfo, nullptr, &fence);
  VkSubmitInfo submit{};
  submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;

  std::atomic<bool> submitted{false};
  std::atomic<bool> cancelRequested{false};
  std::atomic<int> workerOutcome{-1};
  std::atomic<int> observedFenceResult{VK_NOT_READY};

  std::thread worker([&]() {
    vkQueueSubmit(queue_, 1, &submit, fence);
    submitted.store(true);
    for (int attempt = 0; attempt < 1000000; ++attempt) {
      VkResult poll = vkWaitForFences(device_, 1, &fence, VK_TRUE, 0);
      observedFenceResult.store(poll);
      if (poll == VK_TIMEOUT && cancelRequested.load()) {
        workerOutcome.store(2);
        return;
      }
      if (poll == VK_SUCCESS) {
        workerOutcome.store(0);
        return;
      }
    }
    workerOutcome.store(3);
  });

  while (!submitted.load()) std::this_thread::yield();
  cancelRequested.store(true);
  worker.join();

  appendStep(rec, "cancel",
             std::string("工作线程退出码=") +
                 std::to_string(workerOutcome.load()) +
                 "（2=在 fence 未完成时被取消），最后观测 "
                 "vkWaitForFences(timeout=0)=" +
                 vkResultName(static_cast<VkResult>(
                     observedFenceResult.load())),
             "EV-FENCE-ZERO-TIMEOUT");

  if (workerOutcome.load() == 2) {
    rec.result = readQuery(2);
  } else {
    vkWaitForFences(device_, 1, &fence, VK_TRUE, UINT64_MAX);
    rec.result = readQuery(2);
    appendStep(rec, "cancel",
               "取消信号到达时帧已完成，取消窗口未命中，保留实际读取",
               "EV-CANCEL-MISSED");
  }

  VkResult drain =
      vkWaitForFences(device_, 1, &fence, VK_TRUE, 30000000000ull);
  appendStep(rec, "drain",
             std::string("取消后排空队列 vkWaitForFences=") +
                 vkResultName(drain),
             "EV-DRAIN");
  QueryResultEvidence postDrain = readQuery(2);
  appendStep(rec, "drain",
             "排空后复查槽 2：vkResult=" +
                 vkResultName(static_cast<VkResult>(postDrain.vkResult)) +
                 " availability=" +
                 std::to_string(postDrain.availability) + " samples=" +
                 std::to_string(postDrain.sampleCount),
             "EV-DRAIN-READBACK");

  vkDestroyFence(device_, fence, nullptr);
  vkFreeCommandBuffers(device_, commandPool_, 1, &cmd);

  if (workerOutcome.load() == 2 && !rec.result.readable) {
    rec.verdict = AutoVerdict::Cancelled;
    rec.verdictReason =
        "取消信号在帧完成前到达，fence 仍为 VK_TIMEOUT 且查询不可用，"
        "本次统计按执行阶段取消处理";
  } else if (workerOutcome.load() == 2) {
    rec.verdict = AutoVerdict::Cancelled;
    rec.verdictReason =
        "取消信号在 fence 完成前到达并被工作线程接受；软件驱动已同步写回"
        "查询数据，但统计消费已被取消，结论仍为取消";
  } else {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason = "取消窗口未命中，需要人工复核时序后再下结论";
  }
  rec.manualStatus = "待人工确认";
  rec.manualNote =
      "对比取消点（EV-FENCE-ZERO-TIMEOUT）与排空后复查（EV-DRAIN-READBACK）"
      "两份证据，确认取消信号与帧完成的先后关系。";
  rec.followUpActions = {
      "将查询消费与 fence 完成状态绑定，未完成时禁止读取样本数",
      "若仍需可见性结论，在下一帧重新录制并提交该查询",
      "业务需要即时返回时，输出“无结论”状态而非零样本遮挡结论"};
  return rec;
}

ScenarioRecord VulkanRunner::runBoundaryUndecidable() {
  ScenarioRecord rec;
  rec.id = "S4-boundary-undecidable";
  rec.kind = ScenarioKind::BoundaryUndecidable;
  rec.title = "场景 4：无法判断的边界（查询槽从未被任何命令触达）";
  rec.input = RawQueryInput{
      3, "box-D 可见性统计",
      "控制流未进入渲染分支：无 begin/end、无提交，仅有池重置",
      false, false, false};
  appendStep(rec, "reset", "vkResetQueryPool 覆盖槽 3", "EV-POOL-RESET");
  appendStep(rec, "branch",
             "控制流评估结果为跳过：未录制任何 begin/end 命令",
             "EV-BRANCH-SKIPPED");
  appendStep(rec, "readback",
             "仍对槽 3 执行读取，保留 NOT_READY 与 availability=0 证据",
             "EV-READBACK-BOUNDARY");

  vkResetQueryPool(device_, queryPool_, 0, kQueryCount);
  rec.result = readQuery(3);

  if (!rec.result.readable &&
      static_cast<VkResult>(rec.result.vkResult) == VK_NOT_READY) {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason =
        "查询既未开始也未完成，VK_NOT_READY 与 availability=0 同时成立，"
        "无法区分“被遮挡”与“未执行”";
  } else if (!rec.result.readable) {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason = "查询不可用，证据不足，保持无法判断";
  } else {
    rec.verdict = AutoVerdict::Undecidable;
    rec.verdictReason = "空槽意外可读，驱动状态异常，禁止自动下结论";
  }
  rec.manualStatus = "待人工确认";
  rec.manualNote =
      "关键证据：vkResult=VK_NOT_READY、availability=0、样本数为哨兵值。"
      "请确认跳过分支是否符合预期。";
  rec.followUpActions = {
      "检查渲染通道的可见性剔除分支，确认 box-D 是否应当参与本帧",
      "在跳过分支中显式写入“未统计”标记，避免与零样本遮挡混淆",
      "如需结论，在下一帧强制为 box-D 录制遮挡查询并等待可用性位",
      "保留本次原始读取证据，不得用默认值 0 填充样本数"};
  return rec;
}

AuditBundle VulkanRunner::runAllScenarios() {
  if (window_ != nullptr) glfwPollEvents();
  AuditBundle bundle;
  bundle.runId = "run-" +
                 std::to_string(static_cast<long long>(
                     std::chrono::system_clock::now().time_since_epoch().count()));
  bundle.createdAtUtc = utcTimestamp();
  bundle.environment = environment_;
  bundle.scenarios.push_back(runCompleted());
  bundle.scenarios.push_back(runRecordingCancelled());
  bundle.scenarios.push_back(runExecutionCancelled());
  bundle.scenarios.push_back(runBoundaryUndecidable());
  if (window_ != nullptr) glfwPollEvents();
  return bundle;
}

}  // namespace audit
