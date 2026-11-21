#define GLFW_INCLUDE_VULKAN
#define GLFW_INCLUDE_NONE
#include "app/vulkan_app.h"

#include "app/app_state.h"
#include "app/scene_builder.h"
#include "app/shaders_spv.h"
#include "core/acceptance.h"
#include "core/time_util.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stb/stb_image_write.h>

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace clipfix {
namespace {

constexpr int kViewportLeft = 468;

struct Mat4 {
    std::array<float, 16> value{};
};

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0.0f;
            for (int inner = 0; inner < 4; ++inner) {
                sum += a.value[inner * 4 + row] * b.value[column * 4 + inner];
            }
            result.value[column * 4 + row] = sum;
        }
    }
    return result;
}

Mat4 perspective(float fieldOfView, float aspect, float nearPlane, float farPlane) {
    Mat4 result;
    const float f = 1.0f / std::tan(fieldOfView * 0.5f);
    result.value[0] = f / aspect;
    result.value[5] = -f;
    result.value[10] = farPlane / (nearPlane - farPlane);
    result.value[11] = -1.0f;
    result.value[14] = (nearPlane * farPlane) / (nearPlane - farPlane);
    return result;
}

Mat4 lookAt(const Vec3& eye, const Vec3& center, const Vec3& up) {
    const Vec3 forward = (center - eye).normalized();
    const Vec3 side = forward.cross(up).normalized();
    const Vec3 realUp = side.cross(forward);
    Mat4 result;
    result.value[0] = static_cast<float>(side.x);
    result.value[1] = static_cast<float>(realUp.x);
    result.value[2] = static_cast<float>(-forward.x);
    result.value[4] = static_cast<float>(side.y);
    result.value[5] = static_cast<float>(realUp.y);
    result.value[6] = static_cast<float>(-forward.y);
    result.value[8] = static_cast<float>(side.z);
    result.value[9] = static_cast<float>(realUp.z);
    result.value[10] = static_cast<float>(-forward.z);
    result.value[12] = static_cast<float>(-side.dot(eye));
    result.value[13] = static_cast<float>(-realUp.dot(eye));
    result.value[14] = static_cast<float>(forward.dot(eye));
    result.value[15] = 1.0f;
    return result;
}

void checkVk(VkResult result) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Vulkan call failed: " + std::to_string(result));
    }
}

void imguiCheckVk(VkResult result) {
    if (result != VK_SUCCESS) {
        std::fprintf(stderr, "ImGui Vulkan result: %d\n", static_cast<int>(result));
    }
}

struct Buffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

class VulkanRenderer {
public:
    ~VulkanRenderer() { cleanup(); }
    int run(AppState& app, const GuiOptions& options);

private:
    void initWindow(const GuiOptions& options);
    void initVulkan();
    void createInstance();
    void createSurface();
    void pickDevice();
    void createDevice();
    void createSwapchain();
    void createImageViews();
    void createRenderPass();
    void createDepthResources();
    void createFramebuffers();
    void createCommandPool();
    void createPipeline();
    void createSyncObjects();
    void initImGui();
    void recreateSwapchain();
    void destroySwapchainObjects();
    void cleanup();

    void processActions(AppState& app);
    void updateGeometry(const RenderData& data);
    void drawFrame(AppState& app);
    void updateCamera(AppState& app);
    Mat4 viewProjection(const AppState& app) const;

    VkShaderModule createShaderModule(const unsigned char* data, std::size_t size);
    Buffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage);
    void destroyBuffer(Buffer& buffer);
    VkFormat findDepthFormat() const;
    void createBufferInternal(
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        VkMemoryPropertyFlags properties,
        VkBuffer& buffer,
        VkDeviceMemory& memory,
        void** mapped);
    std::uint32_t findMemoryType(std::uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

    GLFWwindow* window_ = nullptr;
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
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline linePipeline_ = VK_NULL_HANDLE;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers_;

    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence inFlight_ = VK_NULL_HANDLE;
    Buffer triangleBuffer_;
    Buffer triangleIndexBuffer_;
    Buffer lineBuffer_;
    std::uint32_t triangleCount_ = 0;
    std::uint32_t lineCount_ = 0;
    bool geometryDirty_ = true;

    bool imguiInitialized_ = false;
    bool glfwInitialized_ = false;
    bool framebufferResized_ = false;
    bool suspended_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    bool dragging_ = false;
    std::string acceptanceDirectory_;
    std::filesystem::path screenshotPath_;
    int screenshotFrame_ = -1;
    int frameIndex_ = 0;
};

int VulkanRenderer::run(AppState& app, const GuiOptions& options) {
    acceptanceDirectory_ =
        std::filesystem::path(CLIPFIX_SOURCE_DIR) / "artifacts" / "acceptance";
    screenshotPath_ = options.screenshot;
    screenshotFrame_ = screenshotPath_.empty() ? -1 : std::max(1, options.screenshotAfterFrames);
    initWindow(options);
    initVulkan();
    initImGui();
    app.log("Loaded workspace: " + app.workspace.root().string());
    app.log("Raw material SHA-256: " + app.workspace.aggregateSha256());

    int frameCount = 0;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
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

        if (glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS &&
            !ImGui::GetIO().WantCaptureKeyboard) {
            app.actions |= UiActionSaveExit;
        }
        updateCamera(app);
        drawUi(app, glfwGetTime());
        if (options.autoExitFrames > 0 && frameCount >= options.autoExitFrames) {
            app.actions |= UiActionSaveExit;
        }
        processActions(app);

        ImGui::Render();
        drawFrame(app);
        ++frameIndex_;
        ++frameCount;
        if (app.requestClose) {
            break;
        }
    }

    if (!suspended_) {
        app.workspace.suspend("gui_window_close");
        suspended_ = true;
    }
    cleanup();
    return 0;
}

void VulkanRenderer::initWindow(const GuiOptions&) {
    glfwSetErrorCallback([](int code, const char* description) {
        std::fprintf(stderr, "GLFW error %d: %s\n", code, description);
    });
    if (std::getenv("CLIPFIX_FORCE_X11") != nullptr) {
        glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
    }
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("GLFW initialization failed");
    }
    glfwInitialized_ = true;
    if (std::getenv("CLIPFIX_DEBUG_PLATFORM") != nullptr) {
        const int platform = glfwGetPlatform();
        std::fprintf(
            stderr,
            "GLFW platform: %s\n",
            platform == GLFW_PLATFORM_X11 ? "X11"
            : platform == GLFW_PLATFORM_WAYLAND ? "Wayland"
                                                : "other");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(1500, 900, "Curved Trim Plane | 3-Anchors", nullptr, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW window creation failed");
    }
    glfwSetWindowSizeLimits(window_, 1050, 680, GLFW_DONT_CARE, GLFW_DONT_CARE);
}

void VulkanRenderer::initVulkan() {
    createInstance();
    createSurface();
    pickDevice();
    createDevice();
    createSwapchain();
    createRenderPass();
    createDepthResources();
    createFramebuffers();
    createCommandPool();
    createPipeline();
    createSyncObjects();
}

void VulkanRenderer::createInstance() {
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "Curved Trim Plane";
    application.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    application.pEngineName = "clipfix";
    application.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    application.apiVersion = VK_API_VERSION_1_1;

    std::uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0) {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &application;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = extensions;
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance_));
}

void VulkanRenderer::createSurface() {
    checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_));
}

void VulkanRenderer::pickDevice() {
    std::uint32_t deviceCount = 0;
    checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, nullptr));
    if (deviceCount == 0) {
        throw std::runtime_error("no Vulkan physical device found");
    }
    std::vector<VkPhysicalDevice> devices(deviceCount);
    checkVk(vkEnumeratePhysicalDevices(instance_, &deviceCount, devices.data()));

    for (const auto candidate : devices) {
        std::uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(queueCount);
        vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueCount, families.data());

        std::uint32_t extensionCount = 0;
        checkVk(vkEnumerateDeviceExtensionProperties(
            candidate, nullptr, &extensionCount, nullptr));
        std::vector<VkExtensionProperties> extensions(extensionCount);
        checkVk(vkEnumerateDeviceExtensionProperties(
            candidate, nullptr, &extensionCount, extensions.data()));
        bool hasSwapchain = false;
        for (const auto& extension : extensions) {
            if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
                hasSwapchain = true;
            }
        }
        if (!hasSwapchain) {
            continue;
        }

        for (std::uint32_t family = 0; family < queueCount; ++family) {
            if ((families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0) {
                continue;
            }
            VkBool32 presentSupport = VK_FALSE;
            checkVk(vkGetPhysicalDeviceSurfaceSupportKHR(
                candidate, family, surface_, &presentSupport));
            if (presentSupport == VK_TRUE) {
                physicalDevice_ = candidate;
                graphicsFamily_ = family;
                return;
            }
        }
    }
    throw std::runtime_error("no Vulkan device with graphics and presentation queues");
}

void VulkanRenderer::createDevice() {
    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pQueueCreateInfos = &queueInfo;
    createInfo.enabledExtensionCount = 1;
    createInfo.ppEnabledExtensionNames = &extension;
    checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_));
    vkGetDeviceQueue(device_, graphicsFamily_, 0, &graphicsQueue_);
}

void VulkanRenderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR capabilities{};
    checkVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        physicalDevice_, surface_, &capabilities));

    std::uint32_t formatCount = 0;
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice_, surface_, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    checkVk(vkGetPhysicalDeviceSurfaceFormatsKHR(
        physicalDevice_, surface_, &formatCount, formats.data()));
    VkSurfaceFormatKHR selectedFormat = formats.front();
    for (const auto& candidate : formats) {
        const bool srgb = candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        if (srgb && (candidate.format == VK_FORMAT_B8G8R8A8_SRGB ||
                     candidate.format == VK_FORMAT_R8G8B8A8_SRGB)) {
            selectedFormat = candidate;
            break;
        }
    }

    std::uint32_t modeCount = 0;
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice_, surface_, &modeCount, nullptr));
    std::vector<VkPresentModeKHR> modes(modeCount);
    checkVk(vkGetPhysicalDeviceSurfacePresentModesKHR(
        physicalDevice_, surface_, &modeCount, modes.data()));
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (const auto candidate : modes) {
        if (candidate == VK_PRESENT_MODE_MAILBOX_KHR) {
            presentMode = candidate;
            break;
        }
    }

    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        swapchainExtent_ = capabilities.currentExtent;
    } else {
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(window_, &width, &height);
        swapchainExtent_.width = std::clamp(
            static_cast<std::uint32_t>(width),
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        swapchainExtent_.height = std::clamp(
            static_cast<std::uint32_t>(height),
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = selectedFormat.format;
    createInfo.imageColorSpace = selectedFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent_;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_));
    swapchainFormat_ = selectedFormat.format;

    checkVk(vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr));
    swapchainImages_.resize(imageCount);
    checkVk(vkGetSwapchainImagesKHR(
        device_, swapchain_, &imageCount, swapchainImages_.data()));
    createImageViews();
}

void VulkanRenderer::createImageViews() {
    swapchainImageViews_.resize(swapchainImages_.size());
    for (std::size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainFormat_;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &createInfo, nullptr, &swapchainImageViews_[i]));
    }
}

void VulkanRenderer::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depthAttachment{};
    depthAttachment.format = findDepthFormat();
    depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference depthReference{};
    depthReference.attachment = 1;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    const std::array<VkAttachmentDescription, 2> attachments{
        colorAttachment,
        depthAttachment,
    };
    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    createInfo.pAttachments = attachments.data();
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    checkVk(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_));
}

VkFormat VulkanRenderer::findDepthFormat() const {
    const std::array<VkFormat, 3> candidates{
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };
    for (const auto format : candidates) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice_, format, &properties);
        if ((properties.optimalTilingFeatures &
             VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0) {
            return format;
        }
    }
    throw std::runtime_error("no suitable depth format");
}

void VulkanRenderer::createDepthResources() {
    depthFormat_ = findDepthFormat();
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {swapchainExtent_.width, swapchainExtent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateImage(device_, &imageInfo, nullptr, &depthImage_));

    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, depthImage_, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &depthMemory_));
    checkVk(vkBindImageMemory(device_, depthImage_, depthMemory_, 0));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &depthView_));
}

void VulkanRenderer::createFramebuffers() {
    framebuffers_.resize(swapchainImageViews_.size());
    for (std::size_t i = 0; i < swapchainImageViews_.size(); ++i) {
        const std::array<VkImageView, 2> attachments{
            swapchainImageViews_[i],
            depthView_,
        };
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
        createInfo.pAttachments = attachments.data();
        createInfo.width = swapchainExtent_.width;
        createInfo.height = swapchainExtent_.height;
        createInfo.layers = 1;
        checkVk(vkCreateFramebuffer(device_, &createInfo, nullptr, &framebuffers_[i]));
    }
}

void VulkanRenderer::createCommandPool() {
    VkCommandPoolCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    createInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    createInfo.queueFamilyIndex = graphicsFamily_;
    checkVk(vkCreateCommandPool(device_, &createInfo, nullptr, &commandPool_));

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool_;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    checkVk(vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_));
}

VkShaderModule VulkanRenderer::createShaderModule(
    const unsigned char* data,
    std::size_t size) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size;
    createInfo.pCode = reinterpret_cast<const std::uint32_t*>(data);
    VkShaderModule module = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module));
    return module;
}

void VulkanRenderer::createPipeline() {
    VkShaderModule vertexModule = createShaderModule(kMeshVertSpv, kMeshVertSpv_size);
    VkShaderModule fragmentModule = createShaderModule(kMeshFragSpv, kMeshFragSpv_size);

    VkPipelineShaderStageCreateInfo vertexStage{};
    vertexStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertexStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertexStage.module = vertexModule;
    vertexStage.pName = "main";
    VkPipelineShaderStageCreateInfo fragmentStage{};
    fragmentStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragmentStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragmentStage.module = fragmentModule;
    fragmentStage.pName = "main";
    const std::array<VkPipelineShaderStageCreateInfo, 2> stages{
        vertexStage,
        fragmentStage,
    };

    VkVertexInputBindingDescription binding{};
    binding.binding = 0;
    binding.stride = sizeof(RenderVertex);
    binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    const std::array<VkVertexInputAttributeDescription, 3> attributes{{
        {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, position)},
        {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, normal)},
        {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(RenderVertex, color)},
    }};
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount =
        static_cast<std::uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo triangleAssembly{};
    triangleAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    triangleAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineInputAssemblyStateCreateInfo lineAssembly = triangleAssembly;
    lineAssembly.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;

    const std::array<VkDynamicState, 2> dynamicStates{
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
    };
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(float) * 16;
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr, &pipelineLayout_));

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &triangleAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    VkGraphicsPipelineCreateInfo linePipelineInfo = pipelineInfo;
    linePipelineInfo.pInputAssemblyState = &lineAssembly;
    const std::array<VkGraphicsPipelineCreateInfo, 2> pipelineInfos{
        pipelineInfo,
        linePipelineInfo,
    };
    std::array<VkPipeline, 2> pipelines{VK_NULL_HANDLE, VK_NULL_HANDLE};
    checkVk(vkCreateGraphicsPipelines(
        device_,
        VK_NULL_HANDLE,
        static_cast<std::uint32_t>(pipelineInfos.size()),
        pipelineInfos.data(),
        nullptr,
        pipelines.data()));
    pipeline_ = pipelines[0];
    linePipeline_ = pipelines[1];

    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
}

void VulkanRenderer::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_));
    checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinished_));
    checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &inFlight_));
}

void VulkanRenderer::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontScaleMain = 1.08f;
    style.WindowRounding = 8.0f;
    style.FrameRounding = 5.0f;
    style.GrabRounding = 4.0f;
    style.ChildRounding = 6.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.055f, 0.065f, 0.085f, 0.96f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.13f, 0.15f, 0.20f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.17f, 0.28f, 0.42f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.24f, 0.42f, 0.62f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.52f, 0.76f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.19f, 0.32f, 0.48f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.26f, 0.30f, 0.38f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.99f, 0.71f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.99f, 0.71f, 0.26f, 1.0f);

    ImGui_ImplGlfw_InitForVulkan(window_, true);
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_1;
    info.Instance = instance_;
    info.PhysicalDevice = physicalDevice_;
    info.Device = device_;
    info.QueueFamily = graphicsFamily_;
    info.Queue = graphicsQueue_;
    info.RenderPass = renderPass_;
    info.MinImageCount = 2;
    info.ImageCount = static_cast<std::uint32_t>(swapchainImageViews_.size());
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.DescriptorPoolSize = 64;
    info.CheckVkResultFn = imguiCheckVk;
    if (!ImGui_ImplVulkan_Init(&info)) {
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("ImGui Vulkan backend initialization failed");
    }
    imguiInitialized_ = true;
}

void VulkanRenderer::destroySwapchainObjects() {
    for (const auto framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    if (depthView_ != VK_NULL_HANDLE) {
        vkDestroyImageView(device_, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE) {
        vkDestroyImage(device_, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE) {
        vkFreeMemory(device_, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }
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

void VulkanRenderer::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width == 0 || height == 0) {
        return;
    }
    vkDeviceWaitIdle(device_);
    destroySwapchainObjects();
    createSwapchain();
    createDepthResources();
    createFramebuffers();
    if (imguiInitialized_) {
        ImGui_ImplVulkan_SetMinImageCount(2);
    }
    framebufferResized_ = false;
}

std::uint32_t VulkanRenderer::findMemoryType(
    std::uint32_t typeFilter,
    VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1U << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("no suitable Vulkan memory type");
}

void VulkanRenderer::createBufferInternal(
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    VkBuffer& buffer,
    VkDeviceMemory& memory,
    void** mapped) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = requirements.size;
    allocateInfo.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, properties);
    checkVk(vkAllocateMemory(device_, &allocateInfo, nullptr, &memory));
    checkVk(vkBindBufferMemory(device_, buffer, memory, 0));
    if (mapped != nullptr) {
        checkVk(vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, mapped));
    }
}

Buffer VulkanRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
    Buffer result;
    result.size = size;
    createBufferInternal(
        size,
        usage,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        result.buffer,
        result.memory,
        &result.mapped);
    return result;
}

void VulkanRenderer::destroyBuffer(Buffer& buffer) {
    if (buffer.mapped != nullptr) {
        vkUnmapMemory(device_, buffer.memory);
        buffer.mapped = nullptr;
    }
    if (buffer.buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(device_, buffer.buffer, nullptr);
        buffer.buffer = VK_NULL_HANDLE;
    }
    if (buffer.memory != VK_NULL_HANDLE) {
        vkFreeMemory(device_, buffer.memory, nullptr);
        buffer.memory = VK_NULL_HANDLE;
    }
    buffer.size = 0;
}

void VulkanRenderer::updateGeometry(const RenderData& data) {
    destroyBuffer(triangleBuffer_);
    destroyBuffer(triangleIndexBuffer_);
    destroyBuffer(lineBuffer_);
    triangleCount_ = static_cast<std::uint32_t>(data.triangleIndices.size());
    lineCount_ = static_cast<std::uint32_t>(data.lineIndices.size());
    if (!data.triangleVertices.empty()) {
        triangleBuffer_ = createBuffer(
            data.triangleVertices.size() * sizeof(RenderVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::memcpy(
            triangleBuffer_.mapped,
            data.triangleVertices.data(),
            data.triangleVertices.size() * sizeof(RenderVertex));
    }
    if (!data.triangleIndices.empty()) {
        triangleIndexBuffer_ = createBuffer(
            data.triangleIndices.size() * sizeof(std::uint32_t),
            VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        std::memcpy(
            triangleIndexBuffer_.mapped,
            data.triangleIndices.data(),
            data.triangleIndices.size() * sizeof(std::uint32_t));
    }
    if (!data.lineVertices.empty()) {
        lineBuffer_ = createBuffer(
            data.lineVertices.size() * sizeof(RenderVertex),
            VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        std::memcpy(
            lineBuffer_.mapped,
            data.lineVertices.data(),
            data.lineVertices.size() * sizeof(RenderVertex));
    }
    geometryDirty_ = false;
}

void VulkanRenderer::updateCamera(AppState& app) {
    ImGuiIO& io = ImGui::GetIO();
    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);
    const bool leftDown = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
    if (!io.WantCaptureMouse) {
        if (io.MouseWheel != 0.0f) {
            app.orbitDistance = std::clamp(
                app.orbitDistance - io.MouseWheel * 0.45f,
                2.2f,
                14.0f);
        }
        if (leftDown && dragging_) {
            app.orbitYawDeg += static_cast<float>(mouseX - lastMouseX_) * 0.35f;
            app.orbitPitchDeg += static_cast<float>(mouseY - lastMouseY_) * 0.30f;
            app.orbitPitchDeg = std::clamp(app.orbitPitchDeg, -82.0f, 82.0f);
        }
    }
    dragging_ = leftDown && !io.WantCaptureMouse;
    lastMouseX_ = mouseX;
    lastMouseY_ = mouseY;
}

Mat4 VulkanRenderer::viewProjection(const AppState& app) const {
    const float aspect =
        static_cast<float>(std::max(1, static_cast<int>(swapchainExtent_.width) - kViewportLeft)) /
        static_cast<float>(std::max(1U, swapchainExtent_.height));
    const float yaw = app.orbitYawDeg * 3.14159265358979323846f / 180.0f;
    const float pitch = app.orbitPitchDeg * 3.14159265358979323846f / 180.0f;
    const float cp = std::cos(pitch);
    const Vec3 target{0.0, 0.05, 0.0};
    const Vec3 eye{
        target.x + app.orbitDistance * cp * std::sin(yaw),
        target.y + app.orbitDistance * std::sin(pitch),
        target.z + app.orbitDistance * cp * std::cos(yaw),
    };
    const Mat4 projection = perspective(0.82f, aspect, 0.05f, 80.0f);
    const Mat4 view = lookAt(eye, target, {0.0, 1.0, 0.0});
    return multiply(projection, view);
}

void VulkanRenderer::processActions(AppState& app) {
    const std::uint32_t actions = app.actions;
    app.actions = UiActionNone;
    if (actions == UiActionNone) {
        return;
    }
    const std::string selected = app.selectedObjectId();
    const double now = glfwGetTime();
    try {
        if ((actions & UiActionRunSelected) != 0) {
            const auto run = app.workspace.runObject(selected);
            app.setNotice(
                "Re-ran " + selected + " at " + run.finishedUtc +
                    " | affected " + std::to_string(run.stats.affectedCount),
                false,
                now);
            app.renderDirty = true;
        }
        if ((actions & UiActionRunAll) != 0) {
            const auto runs = app.workspace.runAllObjects();
            app.setNotice(
                "Re-ran all " + std::to_string(runs.size()) +
                    " render objects independently",
                false,
                now);
            app.renderDirty = true;
        }
        if ((actions & UiActionCompareSelected) != 0) {
            const auto compare = app.workspace.compareObject(selected, false);
            app.setNotice(
                "Compared " + selected + " | changed " +
                    std::to_string(compare.changedVertices) + " | max " +
                    formatMm(compare.maxDeltaMm),
                false,
                now);
            app.renderDirty = true;
        }
        if ((actions & UiActionExportSelected) != 0) {
            const auto exported = app.workspace.exportObject(selected);
            app.setNotice(
                "Exported " + selected + " | " + exported.objSha256.substr(0, 16),
                false,
                now);
        }
        if ((actions & UiActionExportAll) != 0) {
            for (const auto& object : app.workspace.material().scene.objects) {
                app.workspace.exportObject(object.id);
            }
            app.setNotice("Exported all render objects", false, now);
        }
        if ((actions & UiActionAcceptance) != 0) {
            AcceptanceOptions options;
            options.reportDirectory = acceptanceDirectory_;
            options.fillMissingEvidence = true;
            const AcceptanceReport report = runAcceptance(app.workspace, options);
            app.setNotice(
                std::string("Acceptance ") + (report.pass ? "PASS" : "FAIL") +
                    " | " + report.markdownPath.string(),
                !report.pass,
                now);
            app.renderDirty = true;
        }
        if ((actions & UiActionSave) != 0) {
            app.workspace.save();
            app.log("Saved session revision " +
                    std::to_string(app.workspace.state().revision));
        }
        if ((actions & UiActionSaveExit) != 0) {
            app.workspace.suspend("gui_save_exit");
            suspended_ = true;
            app.requestClose = true;
        }
    } catch (const std::exception& ex) {
        app.setNotice(ex.what(), true, now);
    }
}

void VulkanRenderer::drawFrame(AppState& app) {
    RenderData renderData;
    if (geometryDirty_ || app.renderDirty) {
        try {
            renderData = buildRenderData(app);
        } catch (const std::exception& ex) {
            app.setNotice(std::string("Render data error: ") + ex.what(), true, glfwGetTime());
            app.renderDirty = false;
            return;
        }
    }

    checkVk(vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX));
    std::uint32_t imageIndex = 0;
    const VkResult acquire = vkAcquireNextImageKHR(
        device_, swapchain_, UINT64_MAX, imageAvailable_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("failed to acquire swapchain image");
    }

    if (app.renderDirty || geometryDirty_) {
        updateGeometry(renderData);
        app.renderDirty = false;
    }

    const bool captureThisFrame =
        screenshotFrame_ >= 0 && frameIndex_ == screenshotFrame_;
    Buffer captureBuffer;
    if (captureThisFrame) {
        captureBuffer = createBuffer(
            static_cast<VkDeviceSize>(swapchainExtent_.width) *
                static_cast<VkDeviceSize>(swapchainExtent_.height) * 4,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    checkVk(vkResetFences(device_, 1, &inFlight_));
    checkVk(vkResetCommandBuffer(commandBuffer_, 0));
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    checkVk(vkBeginCommandBuffer(commandBuffer_, &beginInfo));

    std::array<VkClearValue, 2> clearValues{};
    clearValues[0].color = {{0.018f, 0.024f, 0.038f, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass_;
    renderPassInfo.framebuffer = framebuffers_[imageIndex];
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = swapchainExtent_;
    renderPassInfo.clearValueCount = static_cast<std::uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();
    vkCmdBeginRenderPass(commandBuffer_, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    const int viewWidth =
        std::max(1, static_cast<int>(swapchainExtent_.width) - kViewportLeft);
    VkViewport viewport{};
    viewport.x = static_cast<float>(kViewportLeft);
    viewport.width = static_cast<float>(viewWidth);
    viewport.height = static_cast<float>(swapchainExtent_.height);
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{
        {kViewportLeft, 0},
        {static_cast<std::uint32_t>(viewWidth), swapchainExtent_.height}};
    vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);

    const Mat4 matrix = viewProjection(app);
    vkCmdPushConstants(
        commandBuffer_,
        pipelineLayout_,
        VK_SHADER_STAGE_VERTEX_BIT,
        0,
        sizeof(matrix.value),
        matrix.value.data());

    const VkDeviceSize offset = 0;
    if (triangleCount_ > 0 && triangleBuffer_.buffer != VK_NULL_HANDLE) {
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &triangleBuffer_.buffer, &offset);
        vkCmdBindIndexBuffer(
            commandBuffer_,
            triangleIndexBuffer_.buffer,
            0,
            VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer_, triangleCount_, 1, 0, 0, 0);
    }
    if (lineCount_ > 0 && lineBuffer_.buffer != VK_NULL_HANDLE) {
        vkCmdBindPipeline(commandBuffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, linePipeline_);
        vkCmdBindVertexBuffers(commandBuffer_, 0, 1, &lineBuffer_.buffer, &offset);
        vkCmdDraw(commandBuffer_, lineCount_, 1, 0, 0);
    }

    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer_);
    vkCmdEndRenderPass(commandBuffer_);

    if (captureThisFrame) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = swapchainImages_[imageIndex];
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.baseArrayLayer = 0;
        toTransfer.subresourceRange.layerCount = 1;
        toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(
            commandBuffer_,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &toTransfer);

        VkBufferImageCopy copy{};
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.mipLevel = 0;
        copy.imageSubresource.baseArrayLayer = 0;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent = {swapchainExtent_.width, swapchainExtent_.height, 1};
        vkCmdCopyImageToBuffer(
            commandBuffer_,
            swapchainImages_[imageIndex],
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            captureBuffer.buffer,
            1,
            &copy);

        VkImageMemoryBarrier backToPresent = toTransfer;
        backToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        backToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        backToPresent.dstAccessMask = 0;
        vkCmdPipelineBarrier(
            commandBuffer_,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
            0,
            0,
            nullptr,
            0,
            nullptr,
            1,
            &backToPresent);
    }
    checkVk(vkEndCommandBuffer(commandBuffer_));

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailable_;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer_;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinished_;
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, inFlight_));

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinished_;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult present = vkQueuePresentKHR(graphicsQueue_, &presentInfo);
    if (present == VK_ERROR_OUT_OF_DATE_KHR || present == VK_SUBOPTIMAL_KHR ||
        framebufferResized_) {
        recreateSwapchain();
    } else if (present != VK_SUCCESS) {
        destroyBuffer(captureBuffer);
        throw std::runtime_error("failed to present swapchain image");
    }

    if (captureThisFrame) {
        checkVk(vkWaitForFences(device_, 1, &inFlight_, VK_TRUE, UINT64_MAX));
        std::vector<std::uint8_t> rgba(
            static_cast<std::size_t>(swapchainExtent_.width) *
            static_cast<std::size_t>(swapchainExtent_.height) * 4);
        std::memcpy(rgba.data(), captureBuffer.mapped, rgba.size());
        if (swapchainFormat_ == VK_FORMAT_B8G8R8A8_SRGB ||
            swapchainFormat_ == VK_FORMAT_B8G8R8A8_UNORM) {
            for (std::size_t offset = 0; offset + 3 < rgba.size(); offset += 4) {
                std::swap(rgba[offset], rgba[offset + 2]);
            }
        }
        std::filesystem::create_directories(screenshotPath_.parent_path());
        const int saved = stbi_write_png(
            screenshotPath_.string().c_str(),
            static_cast<int>(swapchainExtent_.width),
            static_cast<int>(swapchainExtent_.height),
            4,
            rgba.data(),
            static_cast<int>(swapchainExtent_.width) * 4);
        if (saved == 0) {
            app.setNotice("Failed to write screenshot", true, glfwGetTime());
        } else {
            app.setNotice(
                "Screenshot saved: " + screenshotPath_.string(),
                false,
                glfwGetTime());
        }
        destroyBuffer(captureBuffer);
        screenshotFrame_ = -1;
    }
}

void VulkanRenderer::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (imguiInitialized_) {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (device_ != VK_NULL_HANDLE) {
        destroyBuffer(triangleBuffer_);
        destroyBuffer(triangleIndexBuffer_);
        destroyBuffer(lineBuffer_);
        if (inFlight_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, inFlight_, nullptr);
            inFlight_ = VK_NULL_HANDLE;
        }
        if (renderFinished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinished_, nullptr);
            renderFinished_ = VK_NULL_HANDLE;
        }
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
            imageAvailable_ = VK_NULL_HANDLE;
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
            commandPool_ = VK_NULL_HANDLE;
            commandBuffer_ = VK_NULL_HANDLE;
        }
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (linePipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, linePipeline_, nullptr);
            linePipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        destroySwapchainObjects();
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
    if (glfwInitialized_) {
        glfwTerminate();
        glfwInitialized_ = false;
    }
}

}  // namespace

int runGui(const GuiOptions& options) {
    try {
        Workspace workspace = Workspace::open({options.workspace, options.material});
        AppState app(std::move(workspace));
        if (options.view == "original") {
            app.viewMode = ViewMode::Original;
        } else if (options.view == "diff") {
            app.viewMode = ViewMode::Diff;
        } else {
            app.viewMode = ViewMode::Corrected;
        }
        app.showClipSurface = options.showClipSurface;
        app.showWireframe = options.showWireframe;
        VulkanRenderer renderer;
        return renderer.run(app, options);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "GUI error: %s\n", ex.what());
        return 1;
    }
}

}  // namespace clipfix
