#include "vulkan_app.h"

#include "app_ui.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stb/stb_image_write.h>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace surfprov {
namespace {

constexpr int kWindowWidth = 1440;
constexpr int kWindowHeight = 900;

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " + std::to_string(result));
    }
}

struct Texture {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    VkDescriptorSet descriptor = VK_NULL_HANDLE;
};

class VulkanApp {
public:
    ~VulkanApp() { cleanup(); }
    int run(const AppOptions& options);

private:
    void initWindow(const AppOptions& options);
    void initVulkan();
    void createInstance();
    void createSurface();
    void pickPhysicalDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createFramebuffers();
    void createCommandObjects();
    void createSyncObjects();
    void initImGui();
    void createPreviewTextures();
    void recreateSwapchain();
    void destroySwapchain();
    void mainLoop();
    void renderFrame();
    void captureScreenshot(VkBuffer buffer, VkDeviceMemory memory);
    void cleanup();

    std::uint32_t findMemoryType(std::uint32_t typeFilter,
                                 VkMemoryPropertyFlags properties) const;
    void createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer,
                      VkDeviceMemory& memory,
                      void** mapped = nullptr) const;
    void destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) const;
    void oneShot(const std::function<void(VkCommandBuffer)>& function);
    Texture uploadTexture(const Preview& preview);
    void transitionImage(VkCommandBuffer command,
                         VkImage image,
                         VkImageLayout oldLayout,
                         VkImageLayout newLayout);

    GLFWwindow* window_ = nullptr;
    bool glfwReady_ = false;
    bool imguiReady_ = false;
    bool framebufferResized_ = false;
    int frameIndex_ = 0;
    int captureFrame_ = -1;
    int exitFrame_ = -1;
    std::filesystem::path screenshotPath_;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily_ = 0;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent_{};
    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainImageViews_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    std::vector<Texture> textures_;
    UiContext ui_;
};

int VulkanApp::run(const AppOptions& options) {
    ui_.root = options.root;
    ui_.batch = processBatch({options.root, true});
    for (std::size_t index = 0; index < ui_.batch.records.size(); ++index) {
        if (ui_.batch.records[index].boundary.present) {
            ui_.selected = static_cast<int>(index);
            ui_.selectedStep = -1;
            ui_.rawMode = false;
            break;
        }
    }
    if (!options.initialRecord.empty()) {
        for (std::size_t index = 0; index < ui_.batch.records.size(); ++index) {
            if (ui_.batch.records[index].id == options.initialRecord ||
                ui_.batch.records[index].name == options.initialRecord) {
                ui_.selected = static_cast<int>(index);
                ui_.rawMode =
                    ui_.batch.records[index].kind == RecordKind::UnknownExtension;
            }
        }
    }
    if (!options.screenshot.empty()) {
        screenshotPath_ = options.screenshot;
        captureFrame_ = std::max(2, options.screenshotFrame);
        exitFrame_ = std::max(captureFrame_ + 2, options.autoExitFrames > 0
                                                       ? options.autoExitFrames
                                                       : captureFrame_ + 3);
    } else if (options.autoExitFrames > 0) {
        exitFrame_ = options.autoExitFrames;
    }

    initWindow(options);
    initVulkan();
    initImGui();
    createPreviewTextures();
    mainLoop();
    cleanup();
    return 0;
}

void VulkanApp::initWindow(const AppOptions&) {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
    });
    if (std::getenv("SURFPROV_FORCE_X11") != nullptr) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("GLFW initialization failed");
    }
    glfwReady_ = true;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(
        kWindowWidth, kWindowHeight,
        "曲面参数化展开 · 来源检查", nullptr, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW window creation failed");
    }
    glfwSetWindowSizeLimits(window_, 1120, 720, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, [](GLFWwindow* window, int, int) {
        auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
        app->framebufferResized_ = true;
    });
}

void VulkanApp::initVulkan() {
    createInstance();
    createSurface();
    pickPhysicalDevice();
    createDevice();
    createSwapchain();
    createImageViews();
    createRenderPass();
    createFramebuffers();
    createCommandObjects();
    createSyncObjects();
}

void VulkanApp::createInstance() {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Surface Unfold Provenance";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "surface_unfold";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    std::uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0) {
        throw std::runtime_error("GLFW did not expose Vulkan surface extensions");
    }
    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
}

void VulkanApp::createSurface() {
    checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
            "glfwCreateWindowSurface");
}

void VulkanApp::pickPhysicalDevice() {
    std::uint32_t count = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, nullptr),
            "vkEnumeratePhysicalDevices");
    if (count == 0) throw std::runtime_error("no Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(count);
    checkVk(vkEnumeratePhysicalDevices(instance_, &count, devices.data()),
            "vkEnumeratePhysicalDevices");

    for (const auto candidate : devices) {
        std::uint32_t extensionCount = 0;
        checkVk(vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, nullptr),
                "vkEnumerateDeviceExtensionProperties");
        std::vector<VkExtensionProperties> extensions(extensionCount);
        checkVk(vkEnumerateDeviceExtensionProperties(
                    candidate, nullptr, &extensionCount, extensions.data()),
                "vkEnumerateDeviceExtensionProperties");
        bool hasSwapchain = false;
        for (const auto& extension : extensions) {
            if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
            }
        }
        if (!hasSwapchain) continue;

        std::uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &familyCount, families.data());
        for (std::uint32_t family = 0; family < familyCount; ++family) {
            VkBool32 present = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                        candidate, family, surface_, &present),
                    "vkGetPhysicalDeviceSurfaceSupportKHR");
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) &&
                present == VK_TRUE) {
                physicalDevice_ = candidate;
                graphicsFamily_ = family;
                return;
            }
        }
    }
    throw std::runtime_error("no Vulkan device with graphics and presentation support");
}

void VulkanApp::createDevice() {
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queue{};
    queue.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue.queueFamilyIndex = graphicsFamily_;
    queue.queueCount = 1;
    queue.pQueuePriorities = &priority;
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = 1;
    info.pQueueCreateInfos = &queue;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateDevice(physicalDevice_, &info, nullptr, &device_),
            "vkCreateDevice");
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

void VulkanApp::destroySwapchain() {
    for (const auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    for (const auto view : swapchainImageViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainImageViews_.clear();
    swapchainImages_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
}

void VulkanApp::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width == 0 || height == 0) return;
    checkVk(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
    destroySwapchain();
    if (renderPass_ != VK_NULL_HANDLE) {
        vkDestroyRenderPass(device_, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }
    createSwapchain();
    createImageViews();
    createRenderPass();
    createFramebuffers();
    if (imguiReady_) {
        ImGui_ImplVulkan_SetMinImageCount(2);
    }
}

void VulkanApp::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (imguiReady_) {
        for (auto& texture : textures_) {
            if (texture.descriptor != VK_NULL_HANDLE) {
                ImGui_ImplVulkan_RemoveTexture(texture.descriptor);
            }
        }
    }
    textures_.clear();
    if (imguiReady_) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiReady_ = false;
    }
    if (device_ != VK_NULL_HANDLE) {
        if (inFlight_ != VK_NULL_HANDLE) vkDestroyFence(device_, inFlight_, nullptr);
        if (renderFinished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinished_, nullptr);
        }
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        inFlight_ = VK_NULL_HANDLE;
        renderFinished_ = VK_NULL_HANDLE;
        imageAvailable_ = VK_NULL_HANDLE;
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
            commandBuffer_ = VK_NULL_HANDLE;
        }
        destroySwapchain();
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
            renderPass_ = VK_NULL_HANDLE;
        }
        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }
    if (surface_ != VK_NULL_HANDLE && instance_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
        surface_ = VK_NULL_HANDLE;
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
        instance_ = VK_NULL_HANDLE;
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfwReady_) {
        glfwTerminate();
        glfwReady_ = false;
    }
}

}  // namespace

int runVulkanApp(const AppOptions& options) {
    VulkanApp app;
    return app.run(options);
}

}  // namespace surfprov

namespace surfprov {
namespace {

void VulkanApp::mainLoop() {
    while (!glfwWindowShouldClose(window_)) {
        glfwWaitEventsTimeout(1.0 / 30.0);
        if (framebufferResized_) {
            recreateSwapchain();
            framebufferResized_ = false;
        }
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        if (width > 0 && height > 0 &&
            (static_cast<std::uint32_t>(width) != swapchainExtent_.width ||
             static_cast<std::uint32_t>(height) != swapchainExtent_.height)) {
            recreateSwapchain();
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        const double now = glfwGetTime();
        const UiAction action = drawUi(ui_, now);
        if (action == UiAction::Revalidate) {
            try {
                ui_.batch = processBatch({ui_.root, true});
                ui_.notice = "重新验证完成，临时状态已清理";
                ui_.noticeUntil = now + 3.5;
            } catch (const std::exception& error) {
                ui_.notice = std::string("重新验证失败：") + error.what();
                ui_.noticeUntil = now + 5.0;
            }
        } else if (action == UiAction::Quit) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            !ImGui::GetIO().WantCaptureKeyboard) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
        ImGui::Render();
        renderFrame();
        ++frameIndex_;
        if (exitFrame_ >= 0 && frameIndex_ >= exitFrame_) {
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
        }
    }
}

void VulkanApp::renderFrame() {
    checkVk(vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");
    std::uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    const bool capture = frameIndex_ == captureFrame_ && !screenshotPath_.empty();
    VkBuffer captureBuffer = VK_NULL_HANDLE;
    VkDeviceMemory captureMemory = VK_NULL_HANDLE;
    if (capture) {
        createBuffer(
            static_cast<VkDeviceSize>(swapchainExtent_.width) *
                static_cast<VkDeviceSize>(swapchainExtent_.height) * 4,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            captureBuffer, captureMemory);
    }

    checkVk(vkResetFences(device_, 1, &inFlight_), "vkResetFences");
    checkVk(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    checkVk(vkBeginCommandBuffer(commandBuffer_, &begin), "vkBeginCommandBuffer");

    VkClearValue clear{};
    clear.color = {{0.025f, 0.038f, 0.052f, 1.0f}};
    VkRenderPassBeginInfo renderPass{};
    renderPass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPass.renderPass = renderPass_;
    renderPass.framebuffer = framebuffers_[imageIndex];
    renderPass.renderArea.extent = swapchainExtent_;
    renderPass.clearValueCount = 1;
    renderPass.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer_, &renderPass, VK_SUBPASS_CONTENTS_INLINE);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer_);
    vkCmdEndRenderPass(commandBuffer_);

    if (capture) {
        VkImageMemoryBarrier toCopy{};
        toCopy.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toCopy.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toCopy.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toCopy.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toCopy.image = swapchainImages_[imageIndex];
        toCopy.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toCopy.subresourceRange.levelCount = 1;
        toCopy.subresourceRange.layerCount = 1;
        toCopy.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(commandBuffer_,
                             VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toCopy);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        vkCmdCopyImageToBuffer(commandBuffer_, swapchainImages_[imageIndex],
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               captureBuffer, 1, &region);
        VkImageMemoryBarrier toPresent = toCopy;
        toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toPresent.dstAccessMask = 0;
        vkCmdPipelineBarrier(commandBuffer_,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0,
                             0, nullptr, 0, nullptr, 1, &toPresent);
    }
    checkVk(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

    const VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &imageAvailable_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &commandBuffer_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &renderFinished_;
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submit, inFlight_), "vkQueueSubmit");

    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &renderFinished_;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    const VkResult result = vkQueuePresentKHR(graphicsQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        recreateSwapchain();
    } else if (result != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }

    if (capture) {
        checkVk(vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
        captureScreenshot(captureBuffer, captureMemory);
        destroyBuffer(captureBuffer, captureMemory);
        captureFrame_ = -1;
    }
}

void VulkanApp::captureScreenshot(VkBuffer, VkDeviceMemory memory) {
    void* mapped = nullptr;
    checkVk(vkMapMemory(device_, memory, 0,
                        static_cast<VkDeviceSize>(swapchainExtent_.width) *
                            static_cast<VkDeviceSize>(swapchainExtent_.height) * 4,
                        0, &mapped),
            "vkMapMemory(screenshot)");
    const std::size_t byteCount =
        static_cast<std::size_t>(swapchainExtent_.width) *
        static_cast<std::size_t>(swapchainExtent_.height) * 4;
    std::vector<std::uint8_t> rgba(byteCount);
    std::memcpy(rgba.data(), mapped, byteCount);
    vkUnmapMemory(device_, memory);
    if (swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB ||
        swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM) {
        for (std::size_t index = 0; index + 3 < rgba.size(); index += 4) {
            std::swap(rgba[index], rgba[index + 2]);
        }
    }
    if (!screenshotPath_.parent_path().empty()) {
        std::filesystem::create_directories(screenshotPath_.parent_path());
    }
    const int saved = stbi_write_png(
        screenshotPath_.string().c_str(),
        static_cast<int>(swapchainExtent_.width),
        static_cast<int>(swapchainExtent_.height),
        4, rgba.data(), static_cast<int>(swapchainExtent_.width) * 4);
    if (saved == 0) {
        throw std::runtime_error("failed to write screenshot: " +
                                 screenshotPath_.string());
    }
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

void VulkanApp::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImFontConfig config;
    config.OversampleH = 2;
    config.OversampleV = 2;
    config.PixelSnapH = true;
    const std::filesystem::path fontDirectory = ui_.root / "assets" / "fonts";
    const std::string cjk = (fontDirectory / "NotoSansSC-Regular.otf").string();
    const std::string mono = (fontDirectory / "DejaVuSansMono.ttf").string();
    ImFont* body = io.Fonts->AddFontFromFileTTF(
        cjk.c_str(), 17.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    if (body == nullptr) body = io.Fonts->AddFontDefault();
    io.FontDefault = body;
    io.Fonts->AddFontFromFileTTF(
        cjk.c_str(), 25.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontFromFileTTF(
        cjk.c_str(), 13.0f, &config, io.Fonts->GetGlyphRangesChineseFull());
    io.Fonts->AddFontFromFileTTF(
        mono.c_str(), 13.5f, &config, io.Fonts->GetGlyphRangesDefault());

    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowPadding = ImVec2(0.0f, 0.0f);
    style.FramePadding = ImVec2(11.0f, 7.0f);
    style.CellPadding = ImVec2(8.0f, 6.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ScrollbarSize = 10.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 10.0f;
    style.FrameRounding = 8.0f;
    style.PopupRounding = 10.0f;
    style.GrabRounding = 6.0f;
    style.WindowBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.Colors[ImGuiCol_Text] = ImVec4(0.88f, 0.91f, 0.94f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.43f, 0.49f, 0.56f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.035f, 0.050f, 0.067f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.055f, 0.074f, 0.094f, 1.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.065f, 0.084f, 0.105f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.14f, 0.18f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.08f, 0.11f, 0.14f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.10f, 0.16f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.10f, 0.16f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.13f, 0.22f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.17f, 0.30f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.09f, 0.15f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.12f, 0.21f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.15f, 0.26f, 0.30f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.03f, 0.04f, 0.06f, 0.5f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.19f, 0.26f, 0.30f, 1.0f);

    ImGui_ImplGlfw_InitForVulkan(window_, true);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = graphicsFamily_;
    info.Queue = graphicsQueue_;
    info.RenderPass = renderPass_;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<std::uint32_t>(swapchainImages_.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.DescriptorPoolSize = 64;
    info.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) {
            std::fprintf(stderr, "ImGui Vulkan result: %d\n", static_cast<int>(result));
        }
    };
    if (!ImGui_ImplVulkan_Init(&info)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui Vulkan backend initialization failed");
    }
    imguiReady_ = true;
}

std::uint32_t VulkanApp::findMemoryType(std::uint32_t typeFilter,
                                        VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memory);
    for (std::uint32_t index = 0; index < memory.memoryTypeCount; ++index) {
        if ((typeFilter & (1u << index)) &&
            (memory.memoryTypes[index].propertyFlags & properties) == properties) {
            return index;
        }
    }
    throw std::runtime_error("required Vulkan memory type not found");
}

void VulkanApp::createBuffer(VkDeviceSize size,
                             VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties,
                             VkBuffer& buffer,
                             VkDeviceMemory& memory,
                             void** mapped) const {
    VkBufferCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    info.size = size;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &info, nullptr, &buffer), "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, properties);
    checkVk(vkAllocateMemory(device_, &allocation, nullptr, &memory),
            "vkAllocateMemory");
    checkVk(vkBindBufferMemory(device_, buffer, memory, 0), "vkBindBufferMemory");
    if (mapped != nullptr) {
        checkVk(vkMapMemory(device_, memory, 0, size, 0, mapped), "vkMapMemory");
    }
}

void VulkanApp::destroyBuffer(VkBuffer& buffer, VkDeviceMemory& memory) const {
    if (buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer, nullptr);
        buffer = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void VulkanApp::transitionImage(VkCommandBuffer command,
                                VkImage image,
                                VkImageLayout oldLayout,
                                VkImageLayout newLayout) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    }
    vkCmdPipelineBarrier(command, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
}

void VulkanApp::oneShot(const std::function<void(VkCommandBuffer)>& function) {
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    VkCommandBuffer command = VK_NULL_HANDLE;
    checkVk(vkAllocateCommandBuffers(device_, &allocation, &command),
            "vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(command, &begin), "vkBeginCommandBuffer");
    function(command);
    checkVk(vkEndCommandBuffer(command), "vkEndCommandBuffer");
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command;
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submit, VK_NULL_HANDLE),
            "vkQueueSubmit");
    checkVk(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle");
    vkFreeCommandBuffers(device_, commandPool_, 1, &command);
}

Texture VulkanApp::uploadTexture(const Preview& preview) {
    Texture texture;
    const VkDeviceSize byteCount =
        static_cast<VkDeviceSize>(preview.rgba.size());
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    createBuffer(byteCount, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory, &mapped);
    std::memcpy(mapped, preview.rgba.data(), preview.rgba.size());
    vkUnmapMemory(device_, stagingMemory);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageInfo.extent = {static_cast<std::uint32_t>(preview.width),
                        static_cast<std::uint32_t>(preview.height), 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    checkVk(vkCreateImage(device_, &imageInfo, nullptr, &texture.image),
            "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, texture.image, &requirements);
    VkMemoryAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVk(vkAllocateMemory(device_, &allocation, nullptr, &texture.memory),
            "vkAllocateMemory");
    checkVk(vkBindImageMemory(device_, texture.image, texture.memory, 0),
            "vkBindImageMemory");

    oneShot([&](VkCommandBuffer command) {
        transitionImage(command, texture.image, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = {static_cast<std::uint32_t>(preview.width),
                              static_cast<std::uint32_t>(preview.height), 1};
        vkCmdCopyBufferToImage(command, stagingBuffer, texture.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
        transitionImage(command, texture.image,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    });
    destroyBuffer(stagingBuffer, stagingMemory);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = texture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &texture.view),
            "vkCreateImageView");
    VkSamplerCreateInfo sampler{};
    sampler.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sampler.magFilter = VK_FILTER_LINEAR;
    sampler.minFilter = VK_FILTER_LINEAR;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    checkVk(vkCreateSampler(device_, &sampler, nullptr, &texture.sampler),
            "vkCreateSampler");
    texture.descriptor = ImGui_ImplVulkan_AddTexture(
        texture.sampler, texture.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return texture;
}

void VulkanApp::createPreviewTextures() {
    textures_.reserve(ui_.batch.records.size());
    ui_.previews.assign(ui_.batch.records.size(), 0);
    for (std::size_t index = 0; index < ui_.batch.records.size(); ++index) {
        if (ui_.batch.records[index].preview.rgba.empty()) {
            textures_.push_back({});
            continue;
        }
        textures_.push_back(uploadTexture(ui_.batch.records[index].preview));
        ui_.previews[index] =
            reinterpret_cast<std::uint64_t>(textures_.back().descriptor);
    }
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

void VulkanApp::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
                physicalDevice_, surface_, &capabilities),
            "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    std::uint32_t formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice_, surface_, &formatCount, nullptr),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
                physicalDevice_, surface_, &formatCount, formats.data()),
            "vkGetPhysicalDeviceSurfaceFormatsKHR");
    VkSurfaceFormatKHR surfaceFormat = formats.front();
    for (const auto& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_SRGB &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = format;
            break;
        }
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM) surfaceFormat = format;
    }
    swapchainFormat_ = surfaceFormat.format;

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        extent.width = static_cast<std::uint32_t>(std::max(1, width));
        extent.height = static_cast<std::uint32_t>(std::max(1, height));
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width,
                                  capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height,
                                   capabilities.maxImageExtent.height);
    }
    swapchainExtent_ = extent;

    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }
    VkCompositeAlphaFlagBitsKHR composite = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(capabilities.supportedCompositeAlpha & composite)) {
        composite = static_cast<VkCompositeAlphaFlagBitsKHR>(
            capabilities.supportedCompositeAlpha &
            (~capabilities.supportedCompositeAlpha + 1));
    }
    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface_;
    info.minImageCount = imageCount;
    info.imageFormat = surfaceFormat.format;
    info.imageColorSpace = surfaceFormat.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = capabilities.currentTransform;
    info.compositeAlpha = composite;
    info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    info.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_),
            "vkCreateSwapchainKHR");

    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr),
            "vkGetSwapchainImagesKHR");
    swapchainImages_.resize(imageCount);
    checkVk(vkGetSwapchainImagesKHR(
                device_, swapchain_, &imageCount, swapchainImages_.data()),
            "vkGetSwapchainImagesKHR");
}

void VulkanApp::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    for (std::size_t index = 0; index < swapchainImages_.size(); ++index) {
        VkImageViewCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        info.image = swapchainImages_[index];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = swapchainFormat_;
        info.components = {VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY,
                           VK_COMPONENT_SWIZZLE_IDENTITY};
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &info, nullptr, &swapchainImageViews_[index]),
                "vkCreateImageView");
    }
}

void VulkanApp::createRenderPass() {
    VkAttachmentDescription color{};
    color.format = swapchainFormat_;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorRef{};
    colorRef.attachment = 0;
    colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    std::array<VkSubpassDependency, 2> dependencies{};
    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = 0;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = 0;

    VkRenderPassCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    info.attachmentCount = 1;
    info.pAttachments = &color;
    info.subpassCount = 1;
    info.pSubpasses = &subpass;
    info.dependencyCount = static_cast<std::uint32_t>(dependencies.size());
    info.pDependencies = dependencies.data();
    checkVk(vkCreateRenderPass(device_, &info, nullptr, &renderPass_),
            "vkCreateRenderPass");
}

void VulkanApp::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (std::size_t index = 0; index < swapchainImageViews_.size(); ++index) {
        VkFramebufferCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        info.renderPass = renderPass_;
        info.attachmentCount = 1;
        info.pAttachments = &swapchainImageViews_[index];
        info.width = swapchainExtent_.width;
        info.height = swapchainExtent_.height;
        info.layers = 1;
        checkVk(vkCreateFramebuffer(device_, &info, nullptr, &framebuffers_[index]),
                "vkCreateFramebuffer");
    }
}

void VulkanApp::createCommandObjects() {
    VkCommandPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = graphicsFamily_;
    checkVk(vkCreateCommandPool(device_, &info, nullptr, &commandPool_),
            "vkCreateCommandPool");
    VkCommandBufferAllocateInfo allocation{};
    allocation.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(device_, &allocation, &commandBuffer_),
            "vkAllocateCommandBuffers");
}

void VulkanApp::createSyncObjects() {
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateSemaphore(device_, &semaphore, nullptr, &imageAvailable_),
            "vkCreateSemaphore");
    checkVk(vkCreateSemaphore(device_, &semaphore, nullptr, &renderFinished_),
            "vkCreateSemaphore");
    checkVk(vkCreateFence(device_, &fence, nullptr, &inFlight_), "vkCreateFence");
}

}  // namespace
}  // namespace surfprov
