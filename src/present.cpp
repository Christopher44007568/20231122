#include "present.h"
#include <GLFW/glfw3.h>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <vector>

int runPresent(const SceneParams& params, int timeoutSeconds) {
  if (glfwInit() != GLFW_TRUE) {
    std::fprintf(stderr, "glfwInit failed (headless?). Check DISPLAY/WAYLAND.\n");
    return 2;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(int(params.width), int(params.height),
                                        "csm_lab - cascade visualization",
                                        nullptr, nullptr);
  if (!window) {
    std::fprintf(stderr, "glfwCreateWindow failed\n");
    glfwTerminate();
    return 2;
  }

  uint32_t extCount = 0;
  const char** glfwExts = glfwGetRequiredInstanceExtensions(&extCount);
  std::vector<std::string> instExts;
  for (uint32_t i = 0; i < extCount; ++i) instExts.push_back(glfwExts[i]);

  std::string err;
  VkContext vk;
  if (!vkInit(vk, instExts, false, err, {"VK_KHR_swapchain"})) {
    std::fprintf(stderr, "vkInit: %s\n", err.c_str());
    glfwDestroyWindow(window);
    glfwTerminate();
    return 2;
  }

  VkSurfaceKHR surface = VK_NULL_HANDLE;
  if (glfwCreateWindowSurface(vk.instance, window, nullptr, &surface) != VK_SUCCESS) {
    std::fprintf(stderr, "glfwCreateWindowSurface failed\n");
    vkDestroy(vk);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 2;
  }

  VkBool32 presentOk = VK_FALSE;
  vkGetPhysicalDeviceSurfaceSupportKHR(vk.phys, vk.queueFamily, surface, &presentOk);
  if (!presentOk) {
    std::fprintf(stderr, "queue family does not support present\n");
    vkDestroySurfaceKHR(vk.instance, surface, nullptr);
    vkDestroy(vk);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 2;
  }

  VkSurfaceCapabilitiesKHR caps;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vk.phys, surface, &caps);
  uint32_t fmtCount = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys, surface, &fmtCount, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(fmtCount);
  vkGetPhysicalDeviceSurfaceFormatsKHR(vk.phys, surface, &fmtCount, formats.data());
  VkSurfaceFormatKHR chosen = formats[0];
  for (auto& f : formats)
    if (f.format == VK_FORMAT_B8G8R8A8_UNORM) chosen = f;

  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface;
  sci.minImageCount = caps.minImageCount + 1;
  sci.imageFormat = chosen.format;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = caps.currentExtent.width != 0xFFFFFFFFu
                        ? caps.currentExtent
                        : VkExtent2D{params.width, params.height};
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  VkSwapchainKHR swapchain = VK_NULL_HANDLE;
  if (vkCreateSwapchainKHR(vk.device, &sci, nullptr, &swapchain) != VK_SUCCESS) {
    std::fprintf(stderr, "vkCreateSwapchainKHR failed\n");
    vkDestroySurfaceKHR(vk.instance, surface, nullptr);
    vkDestroy(vk);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 2;
  }
  uint32_t imgCount = 0;
  vkGetSwapchainImagesKHR(vk.device, swapchain, &imgCount, nullptr);
  std::vector<VkImage> swapImages(imgCount);
  vkGetSwapchainImagesKHR(vk.device, swapchain, &imgCount, swapImages.data());

  CsmRenderer renderer;
  if (!renderer.init(vk, params, err)) {
    std::fprintf(stderr, "renderer init: %s\n", err.c_str());
    return 2;
  }
  RenderJob job;
  job.params = params;
  std::vector<uint8_t> rgba;
  if (!renderer.render(job, rgba, err)) {
    std::fprintf(stderr, "render: %s\n", err.c_str());
    return 2;
  }

  bool swapRB = (chosen.format == VK_FORMAT_B8G8R8A8_UNORM ||
                 chosen.format == VK_FORMAT_B8G8R8A8_SRGB);
  if (swapRB)
    for (size_t i = 0; i + 3 < rgba.size(); i += 4)
      std::swap(rgba[i], rgba[i + 2]);
  VkDeviceSize imgSize = VkDeviceSize(params.width) * params.height * 4;
  GpuBuffer staging = createBuffer(vk, imgSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  void* mapped = nullptr;
  vkMapMemory(vk.device, staging.mem, 0, imgSize, 0, &mapped);
  std::memcpy(mapped, rgba.data(), size_t(imgSize));
  vkUnmapMemory(vk.device, staging.mem);

  VkSemaphoreCreateInfo semCi{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkSemaphore semAcq = VK_NULL_HANDLE, semDone = VK_NULL_HANDLE;
  vkCreateSemaphore(vk.device, &semCi, nullptr, &semAcq);
  vkCreateSemaphore(vk.device, &semCi, nullptr, &semDone);

  auto start = std::chrono::steady_clock::now();
  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    auto elapsed = std::chrono::steady_clock::now() - start;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >
        timeoutSeconds)
      break;
    uint32_t idx = 0;
    if (vkAcquireNextImageKHR(vk.device, swapchain, UINT64_MAX, semAcq,
                              VK_NULL_HANDLE, &idx) != VK_SUCCESS)
      continue;
    VkCommandBuffer cmd = beginOneShot(vk);
    VkImageMemoryBarrier toDst{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toDst.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toDst.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toDst.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toDst.image = swapImages[idx];
    toDst.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    toDst.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0,
                         nullptr, 1, &toDst);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {params.width, params.height, 1};
    vkCmdCopyBufferToImage(cmd, staging.buf, swapImages[idx],
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    VkImageMemoryBarrier toPresent = toDst;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
                         0, nullptr, 1, &toPresent);
    vkEndCommandBuffer(cmd);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    si.pWaitSemaphores = &semAcq;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &semDone;
    vkQueueSubmit(vk.queue, 1, &si, VK_NULL_HANDLE);

    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &semDone;
    pi.swapchainCount = 1;
    pi.pSwapchains = &swapchain;
    pi.pImageIndices = &idx;
    vkQueuePresentKHR(vk.queue, &pi);
    vkQueueWaitIdle(vk.queue);
    vkFreeCommandBuffers(vk.device, vk.cmdPool, 1, &cmd);
  }

  vkDeviceWaitIdle(vk.device);
  vkDestroySemaphore(vk.device, semAcq, nullptr);
  vkDestroySemaphore(vk.device, semDone, nullptr);
  destroyBuffer(vk, staging);
  renderer.destroy();
  vkDestroySwapchainKHR(vk.device, swapchain, nullptr);
  vkDestroySurfaceKHR(vk.instance, surface, nullptr);
  vkDestroy(vk);
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
