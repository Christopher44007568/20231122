#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_vulkan.h"
#include "ui.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

VkAllocationCallbacks* g_Allocator = nullptr;
VkInstance g_Instance = VK_NULL_HANDLE;
VkPhysicalDevice g_PhysicalDevice = VK_NULL_HANDLE;
VkDevice g_Device = VK_NULL_HANDLE;
uint32_t g_QueueFamily = static_cast<uint32_t>(-1);
VkQueue g_Queue = VK_NULL_HANDLE;
VkPipelineCache g_PipelineCache = VK_NULL_HANDLE;
VkDescriptorPool g_DescriptorPool = VK_NULL_HANDLE;
ImGui_ImplVulkanH_Window g_MainWindowData;
uint32_t g_MinImageCount = 2;
bool g_SwapChainRebuild = false;
bool g_CaptureRequested = false;
const char* g_CapturePath = nullptr;

void GlfwErrorCallback(int error, const char* description) {
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

void CheckVkResult(VkResult result) {
    if (result == VK_SUCCESS) {
        return;
    }
    std::fprintf(stderr, "[vulkan] VkResult = %d\n", static_cast<int>(result));
    if (result < 0) {
        std::abort();
    }
}

bool HasExtension(const ImVector<VkExtensionProperties>& properties, const char* name) {
    for (const VkExtensionProperties& property : properties) {
        if (std::strcmp(property.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

void SetupVulkan(ImVector<const char*> instanceExtensions) {
    VkResult result = VK_SUCCESS;
    {
        uint32_t count = 0;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
        properties.resize(static_cast<int>(count));
        vkEnumerateInstanceExtensionProperties(nullptr, &count, properties.Data);

        if (HasExtension(properties, VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME)) {
            instanceExtensions.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);
        }
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (HasExtension(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            instanceExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
#endif

        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Thin-line AA Provenance";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "AA Trace";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.Size);
        createInfo.ppEnabledExtensionNames = instanceExtensions.Data;
        createInfo.enabledLayerCount = 0;

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
        if (HasExtension(properties, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
            createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }
#endif
        result = vkCreateInstance(&createInfo, g_Allocator, &g_Instance);
        CheckVkResult(result);
    }

    g_PhysicalDevice = ImGui_ImplVulkanH_SelectPhysicalDevice(g_Instance);
    IM_ASSERT(g_PhysicalDevice != VK_NULL_HANDLE);
    g_QueueFamily = ImGui_ImplVulkanH_SelectQueueFamilyIndex(g_PhysicalDevice);
    IM_ASSERT(g_QueueFamily != static_cast<uint32_t>(-1));

    {
        ImVector<const char*> deviceExtensions;
        deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        uint32_t count = 0;
        ImVector<VkExtensionProperties> properties;
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &count, nullptr);
        properties.resize(static_cast<int>(count));
        vkEnumerateDeviceExtensionProperties(g_PhysicalDevice, nullptr, &count, properties.Data);
#ifdef VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME
        if (HasExtension(properties, VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME)) {
            deviceExtensions.push_back(VK_KHR_PORTABILITY_SUBSET_EXTENSION_NAME);
        }
#endif

        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queueInfo = {};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = g_QueueFamily;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;

        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = 1;
        createInfo.pQueueCreateInfos = &queueInfo;
        createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.Size);
        createInfo.ppEnabledExtensionNames = deviceExtensions.Data;
        result = vkCreateDevice(g_PhysicalDevice, &createInfo, g_Allocator, &g_Device);
        CheckVkResult(result);
        vkGetDeviceQueue(g_Device, g_QueueFamily, 0, &g_Queue);
    }

    {
        VkDescriptorPoolSize poolSizes[] = {
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE},
        };
        VkDescriptorPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        poolInfo.maxSets = 0;
        for (const VkDescriptorPoolSize& poolSize : poolSizes) {
            poolInfo.maxSets += poolSize.descriptorCount;
        }
        poolInfo.poolSizeCount = static_cast<uint32_t>(IM_ARRAYSIZE(poolSizes));
        poolInfo.pPoolSizes = poolSizes;
        result = vkCreateDescriptorPool(g_Device, &poolInfo, g_Allocator, &g_DescriptorPool);
        CheckVkResult(result);
    }
}

void SetupVulkanWindow(ImGui_ImplVulkanH_Window* windowData, VkSurfaceKHR surface, int width, int height) {
    windowData->Surface = surface;

    VkBool32 supported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(g_PhysicalDevice, g_QueueFamily, surface, &supported);
    if (supported != VK_TRUE) {
        std::fprintf(stderr, "Selected Vulkan device cannot present to the GLFW surface.\n");
        std::exit(2);
    }

    const VkFormat formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8_UNORM,
        VK_FORMAT_R8G8B8_UNORM,
    };
    windowData->SurfaceFormat = ImGui_ImplVulkanH_SelectSurfaceFormat(
        g_PhysicalDevice, surface, formats, static_cast<size_t>(IM_ARRAYSIZE(formats)),
        VK_COLOR_SPACE_SRGB_NONLINEAR_KHR);

    const VkPresentModeKHR presentModes[] = {VK_PRESENT_MODE_FIFO_KHR};
    windowData->PresentMode = ImGui_ImplVulkanH_SelectPresentMode(
        g_PhysicalDevice, surface, presentModes, IM_ARRAYSIZE(presentModes));

    ImGui_ImplVulkanH_CreateOrResizeWindow(
        g_Instance, g_PhysicalDevice, g_Device, windowData, g_QueueFamily,
        g_Allocator, width, height, g_MinImageCount);
}

void CleanupVulkanWindow() {
    ImGui_ImplVulkanH_DestroyWindow(g_Instance, g_Device, &g_MainWindowData, g_Allocator);
}

void CleanupVulkan() {
    vkDestroyDescriptorPool(g_Device, g_DescriptorPool, g_Allocator);
    vkDestroyDevice(g_Device, g_Allocator);
    vkDestroyInstance(g_Instance, g_Allocator);
}

uint32_t FindMemoryType(uint32_t typeBits, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memoryProperties = {};
    vkGetPhysicalDeviceMemoryProperties(g_PhysicalDevice, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0;
}

void SavePpm(const char* path, const std::vector<unsigned char>& rgba, uint32_t width, uint32_t height,
             VkFormat format) {
    std::ofstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Could not open capture path: %s\n", path);
        return;
    }
    file << "P6\n" << width << " " << height << "\n255\n";
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const size_t i = (static_cast<size_t>(y) * width + x) * 4;
            unsigned char pixel[3];
            if (format == VK_FORMAT_R8G8B8A8_UNORM || format == VK_FORMAT_R8G8B8A8_SRGB) {
                pixel[0] = rgba[i + 0];
                pixel[1] = rgba[i + 1];
                pixel[2] = rgba[i + 2];
            } else if (format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB) {
                pixel[0] = rgba[i + 2];
                pixel[1] = rgba[i + 1];
                pixel[2] = rgba[i + 0];
            } else {
                pixel[0] = rgba[i + 0];
                pixel[1] = rgba[i + 1];
                pixel[2] = rgba[i + 2];
            }
            file.write(reinterpret_cast<const char*>(pixel), 3);
        }
    }
    std::fprintf(stderr, "Captured Vulkan frame to %s\n", path);
}

void FrameRender(ImGui_ImplVulkanH_Window* windowData, ImDrawData* drawData) {
    VkSemaphore imageAcquired = windowData->FrameSemaphores[windowData->SemaphoreIndex].ImageAcquiredSemaphore;
    VkSemaphore renderComplete = windowData->FrameSemaphores[windowData->SemaphoreIndex].RenderCompleteSemaphore;
    VkResult result = vkAcquireNextImageKHR(
        g_Device, windowData->Swapchain, UINT64_MAX, imageAcquired, VK_NULL_HANDLE, &windowData->FrameIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        CheckVkResult(result);
    }

    ImGui_ImplVulkanH_Frame* frame = &windowData->Frames[windowData->FrameIndex];
    CheckVkResult(vkWaitForFences(g_Device, 1, &frame->Fence, VK_TRUE, UINT64_MAX));
    CheckVkResult(vkResetFences(g_Device, 1, &frame->Fence));
    CheckVkResult(vkResetCommandPool(g_Device, frame->CommandPool, 0));

    VkCommandBufferBeginInfo beginInfo = {};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    CheckVkResult(vkBeginCommandBuffer(frame->CommandBuffer, &beginInfo));

    VkRenderPassBeginInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = windowData->RenderPass;
    renderPassInfo.framebuffer = frame->Framebuffer;
    renderPassInfo.renderArea.extent.width = windowData->Width;
    renderPassInfo.renderArea.extent.height = windowData->Height;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &windowData->ClearValue;
    vkCmdBeginRenderPass(frame->CommandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(drawData, frame->CommandBuffer);
    vkCmdEndRenderPass(frame->CommandBuffer);

    VkBuffer captureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory captureMemory = VK_NULL_HANDLE;
    VkDeviceSize captureSize = 0;
    if (g_CaptureRequested) {
        captureSize = static_cast<VkDeviceSize>(windowData->Width) * windowData->Height * 4;
        VkBufferCreateInfo bufferInfo = {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = captureSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVkResult(vkCreateBuffer(g_Device, &bufferInfo, g_Allocator, &captureBuffer));

        VkMemoryRequirements requirements = {};
        vkGetBufferMemoryRequirements(g_Device, captureBuffer, &requirements);
        VkMemoryAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(
            requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        CheckVkResult(vkAllocateMemory(g_Device, &allocateInfo, g_Allocator, &captureMemory));
        CheckVkResult(vkBindBufferMemory(g_Device, captureBuffer, captureMemory, 0));

        VkImageMemoryBarrier barrier = {};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = frame->Backbuffer;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(frame->CommandBuffer, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);

        VkBufferImageCopy copy = {};
        copy.bufferRowLength = windowData->Width;
        copy.bufferImageHeight = windowData->Height;
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {static_cast<uint32_t>(windowData->Width),
                            static_cast<uint32_t>(windowData->Height), 1};
        vkCmdCopyImageToBuffer(frame->CommandBuffer, frame->Backbuffer,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, captureBuffer, 1, &copy);

        barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = 0;
        vkCmdPipelineBarrier(frame->CommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAcquired;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &frame->CommandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderComplete;
    CheckVkResult(vkEndCommandBuffer(frame->CommandBuffer));
    CheckVkResult(vkQueueSubmit(g_Queue, 1, &submitInfo, frame->Fence));

    if (g_CaptureRequested) {
        CheckVkResult(vkWaitForFences(g_Device, 1, &frame->Fence, VK_TRUE, UINT64_MAX));
        void* mapped = nullptr;
        CheckVkResult(vkMapMemory(g_Device, captureMemory, 0, captureSize, 0, &mapped));
        std::vector<unsigned char> pixels(static_cast<size_t>(captureSize));
        std::memcpy(pixels.data(), mapped, static_cast<size_t>(captureSize));
        vkUnmapMemory(g_Device, captureMemory);
        SavePpm(g_CapturePath, pixels, windowData->Width, windowData->Height,
                windowData->SurfaceFormat.format);
        vkDestroyBuffer(g_Device, captureBuffer, g_Allocator);
        vkFreeMemory(g_Device, captureMemory, g_Allocator);
        g_CaptureRequested = false;
    }
}

void FramePresent(ImGui_ImplVulkanH_Window* windowData) {
    if (g_SwapChainRebuild) {
        return;
    }
    VkSemaphore renderComplete = windowData->FrameSemaphores[windowData->SemaphoreIndex].RenderCompleteSemaphore;
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderComplete;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &windowData->Swapchain;
    presentInfo.pImageIndices = &windowData->FrameIndex;
    VkResult result = vkQueuePresentKHR(g_Queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        g_SwapChainRebuild = true;
    }
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    }
    if (result != VK_SUBOPTIMAL_KHR) {
        CheckVkResult(result);
    }
    windowData->SemaphoreIndex = (windowData->SemaphoreIndex + 1) % windowData->SemaphoreCount;
}

} // namespace

int main(int, char**) {
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit()) {
        return 1;
    }
    if (!glfwVulkanSupported()) {
        std::fprintf(stderr, "GLFW: Vulkan is not supported on this display.\n");
        glfwTerminate();
        return 1;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
    GLFWwindow* window = glfwCreateWindow(1440, 900, "细线抗锯齿 · 来源检查", nullptr, nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return 1;
    }

    ImVector<const char*> extensions;
    uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    for (uint32_t i = 0; i < extensionCount; ++i) {
        extensions.push_back(glfwExtensions[i]);
    }
    SetupVulkan(extensions);

    VkSurfaceKHR surface = VK_NULL_HANDLE;
    CheckVkResult(glfwCreateWindowSurface(g_Instance, window, g_Allocator, &surface));

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    ImGui_ImplVulkanH_Window* windowData = &g_MainWindowData;
    SetupVulkanWindow(windowData, surface, width, height);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;

    ImGui_ImplGlfw_InitForVulkan(window, true);
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.Instance = g_Instance;
    initInfo.PhysicalDevice = g_PhysicalDevice;
    initInfo.Device = g_Device;
    initInfo.QueueFamily = g_QueueFamily;
    initInfo.Queue = g_Queue;
    initInfo.PipelineCache = g_PipelineCache;
    initInfo.DescriptorPool = g_DescriptorPool;
    initInfo.RenderPass = windowData->RenderPass;
    initInfo.Subpass = 0;
    initInfo.MinImageCount = g_MinImageCount;
    initInfo.ImageCount = windowData->ImageCount;
    initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.Allocator = g_Allocator;
    initInfo.CheckVkResultFn = CheckVkResult;
    ImGui_ImplVulkan_Init(&initInfo);

    aa::UiApp app;
    app.Initialize(io);

    const ImVec4 clearColor(0.055f, 0.063f, 0.075f, 1.0f);
    g_CapturePath = std::getenv("AA_CAPTURE");
    int frameNumber = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        int fbWidth = 0;
        int fbHeight = 0;
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        if (fbWidth > 0 && fbHeight > 0 &&
            (g_SwapChainRebuild || windowData->Width != fbWidth || windowData->Height != fbHeight)) {
            ImGui_ImplVulkan_SetMinImageCount(g_MinImageCount);
            ImGui_ImplVulkanH_CreateOrResizeWindow(
                g_Instance, g_PhysicalDevice, g_Device, windowData, g_QueueFamily,
                g_Allocator, fbWidth, fbHeight, g_MinImageCount);
            windowData->FrameIndex = 0;
            g_SwapChainRebuild = false;
        }
        if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) != 0) {
            ImGui_ImplGlfw_Sleep(10);
            continue;
        }
        if (g_CapturePath != nullptr && frameNumber == 20) {
            g_CaptureRequested = true;
        }
        if (glfwGetKey(window, GLFW_KEY_F12) == GLFW_PRESS) {
            g_CapturePath = "frame.ppm";
            g_CaptureRequested = true;
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        app.Draw();
        ImGui::Render();

        ImDrawData* drawData = ImGui::GetDrawData();
        const bool minimized = drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f;
        if (!minimized) {
            windowData->ClearValue.color.float32[0] = clearColor.x * clearColor.w;
            windowData->ClearValue.color.float32[1] = clearColor.y * clearColor.w;
            windowData->ClearValue.color.float32[2] = clearColor.z * clearColor.w;
            windowData->ClearValue.color.float32[3] = clearColor.w;
            FrameRender(windowData, drawData);
            FramePresent(windowData);
        }
        ++frameNumber;

        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    }

    CheckVkResult(vkDeviceWaitIdle(g_Device));
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    CleanupVulkanWindow();
    vkDestroySurfaceKHR(g_Instance, surface, g_Allocator);
    CleanupVulkan();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
