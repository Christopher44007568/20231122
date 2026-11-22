#define GLFW_INCLUDE_NONE
#include "VulkanApp.hpp"

#include "Layout.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace tpd {
namespace {

void checkVk(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " +
                                 std::to_string(static_cast<int>(result)));
    }
}

const std::vector<const char*> kDeviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

}  // namespace

VulkanApp::VulkanApp()
    : model_(PagingConfig{}, makeDefaultPages()), scenarios_(makeScenarios()) {}

VulkanApp::~VulkanApp() {
    cleanup();
}

int VulkanApp::run() {
    if (!initWindow()) {
        return 1;
    }
    if (!initVulkan()) {
        return 1;
    }
    resetScenario(0);

    std::cout << "Texture page debugger: SPACE play/pause, LEFT/RIGHT step, "
                 "1/2/3 scenarios, R reset, +/- speed, ESC quit\n";
    mainLoop();
    return 0;
}

bool VulkanApp::initWindow() {
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "GLFW initialization failed\n";
        return false;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(layout::kWidth, layout::kHeight,
                               "Texture Array Paging Debugger", nullptr, nullptr);
    if (window_ == nullptr) {
        std::cerr << "GLFW window creation failed\n";
        glfwTerminate();
        return false;
    }
    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);
    glfwSetKeyCallback(window_, keyCallback);
    return true;
}

void VulkanApp::resetScenario(size_t index) {
    if (index >= scenarios_.size()) {
        return;
    }
    scenarioIndex_ = index;
    model_.reset();
    replayStep_ = 0;
    tickRemainder_ = 0.0;
    playing_ = true;
    nextTraceToPrint_ = 0;
    requestReplayPage(0);
    nextReplayTick_ = model_.currentTick() +
                      std::max<uint32_t>(1, scenarios_[index].stepTicks);
}

void VulkanApp::requestReplayPage(size_t step) {
    const Scenario& scenario = scenarios_[scenarioIndex_];
    if (scenario.pages.empty()) {
        return;
    }
    replayStep_ = step % scenario.pages.size();
    model_.requestPage(scenario.pages[replayStep_], model_.currentTick());
    processStartedUploads();
    pumpSwitchOutput();
}

void VulkanApp::advanceReplay() {
    const Scenario& scenario = scenarios_[scenarioIndex_];
    if (scenario.pages.empty()) {
        return;
    }
    const size_t nextStep = (replayStep_ + 1) % scenario.pages.size();
    requestReplayPage(nextStep);
    nextReplayTick_ = model_.currentTick() +
                      std::max<uint32_t>(1, scenario.stepTicks);
}

void VulkanApp::processStartedUploads() {
    for (const UploadItem& item : model_.takeStartedUploads()) {
        uploadLayerContent(item.layer, item.contentId);
    }
}

void VulkanApp::pumpSwitchOutput() {
    const auto& traces = model_.traces();
    while (nextTraceToPrint_ < traces.size() &&
           traces[nextTraceToPrint_].resolved) {
        const SwitchTrace& trace = traces[nextTraceToPrint_];
        std::cout << formatSwitchLine(trace, model_.config()) << "\n";
        ++nextTraceToPrint_;
    }
}

void VulkanApp::handleInput() {
    if (resetRequested_) {
        resetRequested_ = false;
        resetScenario(requestedScenario_);
    }
    if (stepDirection_ != 0) {
        const int direction = stepDirection_;
        stepDirection_ = 0;
        playing_ = false;
        const Scenario& scenario = scenarios_[scenarioIndex_];
        const size_t count = scenario.pages.size();
        if (count > 0) {
            const size_t next =
                (replayStep_ + count + (direction > 0 ? 1 : count - 1)) % count;
            requestReplayPage(next);
            nextReplayTick_ = model_.currentTick() +
                              std::max<uint32_t>(1, scenario.stepTicks);
        }
    }
}

void VulkanApp::mainLoop() {
    double previous = glfwGetTime();
    while (glfwWindowShouldClose(window_) == GLFW_FALSE) {
        glfwPollEvents();
        handleInput();

        const double now = glfwGetTime();
        const double delta = std::min(0.1, now - previous);
        previous = now;
        if (delta > 0.0) {
            const float instant = static_cast<float>(1.0 / delta);
            smoothedFps_ += (instant - smoothedFps_) * 0.08f;
        }

        tickRemainder_ += delta * 60.0 * speed_;
        int ticks = static_cast<int>(tickRemainder_);
        tickRemainder_ -= ticks;
        ticks = std::min(ticks, 8);
        for (int i = 0; i < ticks; ++i) {
            model_.advanceTo(model_.currentTick() + 1);
            processStartedUploads();
            pumpSwitchOutput();
            if (playing_ && model_.currentTick() >= nextReplayTick_) {
                advanceReplay();
            }
        }

        drawFrame();
    }
    vkDeviceWaitIdle(device_);
}

void VulkanApp::framebufferResizeCallback(GLFWwindow* window, int, int) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (app != nullptr) {
        app->framebufferResized_ = true;
    }
}

void VulkanApp::keyCallback(GLFWwindow* window, int key, int, int action, int) {
    auto* app = static_cast<VulkanApp*>(glfwGetWindowUserPointer(window));
    if (app == nullptr || action == GLFW_RELEASE) {
        return;
    }
    if (key == GLFW_KEY_ESCAPE) {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    } else if (key == GLFW_KEY_SPACE) {
        app->playing_ = !app->playing_;
    } else if (key == GLFW_KEY_RIGHT) {
        app->stepDirection_ = 1;
    } else if (key == GLFW_KEY_LEFT) {
        app->stepDirection_ = -1;
    } else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_3) {
        app->requestedScenario_ = static_cast<size_t>(key - GLFW_KEY_1);
        app->resetRequested_ = true;
    } else if (key == GLFW_KEY_R) {
        app->requestedScenario_ = app->scenarioIndex_;
        app->resetRequested_ = true;
    } else if (key == GLFW_KEY_EQUAL || key == GLFW_KEY_KP_ADD) {
        app->speed_ = std::min(4.0, app->speed_ * 1.25);
    } else if (key == GLFW_KEY_MINUS || key == GLFW_KEY_KP_SUBTRACT) {
        app->speed_ = std::max(0.25, app->speed_ / 1.25);
    }
}

bool VulkanApp::initVulkan() {
    try {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createDescriptorSetLayout();
        createPipelines();
        createCommandResources();
        createTextureArray();
        createUiTexture();
        createDescriptors();
        createSwapchainResources();
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Vulkan initialization error: " << error.what() << "\n";
        return false;
    }
}

void VulkanApp::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Texture Array Paging Debugger";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "TPD";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_1;

    uint32_t extensionCount = 0;
    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (glfwExtensions == nullptr) {
        throw std::runtime_error("GLFW did not provide Vulkan instance extensions");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;
    checkVk(vkCreateInstance(&createInfo, nullptr, &instance_),
            "vkCreateInstance");
}

void VulkanApp::createSurface() {
    checkVk(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
            "glfwCreateWindowSurface");
}

VulkanApp::QueueFamilies VulkanApp::findQueueFamilies(
    VkPhysicalDevice device) const {
    QueueFamilies result;
    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());
    for (uint32_t i = 0; i < count; ++i) {
        if ((families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0) {
            result.graphics = i;
        }
        VkBool32 presentSupport = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface_, &presentSupport);
        if (presentSupport == VK_TRUE) {
            result.present = i;
        }
    }
    return result;
}

VulkanApp::SwapchainSupport VulkanApp::querySwapchainSupport(
    VkPhysicalDevice device) const {
    SwapchainSupport support;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface_,
                                              &support.capabilities);
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount, nullptr);
    support.formats.resize(formatCount);
    if (formatCount > 0) {
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface_, &formatCount,
                                             support.formats.data());
    }
    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface_, &modeCount,
                                              nullptr);
    support.presentModes.resize(modeCount);
    if (modeCount > 0) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(
            device, surface_, &modeCount, support.presentModes.data());
    }
    return support;
}

bool VulkanApp::isDeviceSuitable(VkPhysicalDevice device) const {
    if (!findQueueFamilies(device).complete()) {
        return false;
    }
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                         extensions.data());
    for (const char* required : kDeviceExtensions) {
        bool found = false;
        for (const VkExtensionProperties& extension : extensions) {
            if (std::strcmp(required, extension.extensionName) == 0) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    const SwapchainSupport support = querySwapchainSupport(device);
    return !support.formats.empty() && !support.presentModes.empty();
}

void VulkanApp::pickPhysicalDevice() {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance_, &count, nullptr);
    if (count == 0) {
        throw std::runtime_error("no Vulkan physical device found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance_, &count, devices.data());
    for (VkPhysicalDevice device : devices) {
        if (isDeviceSuitable(device)) {
            physicalDevice_ = device;
            queueFamilies_ = findQueueFamilies(device);
            break;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("no suitable Vulkan device");
    }
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    std::cout << "Vulkan device: " << properties.deviceName << "\n";
}

void VulkanApp::createLogicalDevice() {
    std::vector<uint32_t> uniqueFamilies;
    uniqueFamilies.push_back(queueFamilies_.graphics);
    if (queueFamilies_.present != queueFamilies_.graphics) {
        uniqueFamilies.push_back(queueFamilies_.present);
    }
    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }

    VkPhysicalDeviceFeatures features{};
    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.pEnabledFeatures = &features;
    createInfo.enabledExtensionCount =
        static_cast<uint32_t>(kDeviceExtensions.size());
    createInfo.ppEnabledExtensionNames = kDeviceExtensions.data();
    checkVk(vkCreateDevice(physicalDevice_, &createInfo, nullptr, &device_),
            "vkCreateDevice");
    vkGetDeviceQueue(device_, queueFamilies_.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, queueFamilies_.present, 0, &presentQueue_);
}

VkSurfaceFormatKHR VulkanApp::chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats) const {
    if (formats.size() == 1 && formats[0].format == VK_FORMAT_UNDEFINED) {
        return {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    }
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_B8G8R8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    for (const VkSurfaceFormatKHR& format : formats) {
        if (format.format == VK_FORMAT_R8G8B8A8_UNORM &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            return format;
        }
    }
    return formats[0];
}

VkPresentModeKHR VulkanApp::choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes) const {
    for (VkPresentModeKHR mode : modes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            return mode;
        }
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanApp::chooseExtent(const VkSurfaceCapabilitiesKHR& caps) const {
    if (caps.currentExtent.width != UINT32_MAX) {
        return caps.currentExtent;
    }
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    VkExtent2D extent{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width,
                              caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height,
                               caps.maxImageExtent.height);
    return extent;
}

void VulkanApp::createSwapchain() {
    const SwapchainSupport support = querySwapchainSupport(physicalDevice_);
    const VkSurfaceFormatKHR surfaceFormat =
        chooseSurfaceFormat(support.formats);
    const VkPresentModeKHR presentMode =
        choosePresentMode(support.presentModes);
    const VkExtent2D extent = chooseExtent(support.capabilities);

    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0 &&
        imageCount > support.capabilities.maxImageCount) {
        imageCount = support.capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    createInfo.surface = surface_;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = surfaceFormat.format;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = extent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const uint32_t indices[] = {queueFamilies_.graphics, queueFamilies_.present};
    if (queueFamilies_.graphics != queueFamilies_.present) {
        createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = indices;
    } else {
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    createInfo.oldSwapchain = VK_NULL_HANDLE;

    checkVk(vkCreateSwapchainKHR(device_, &createInfo, nullptr, &swapchain_),
            "vkCreateSwapchainKHR");
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount, nullptr);
    swapchainImages_.resize(imageCount);
    vkGetSwapchainImagesKHR(device_, swapchain_, &imageCount,
                            swapchainImages_.data());
    swapchainFormat_ = surfaceFormat.format;
    swapchainExtent_ = extent;
}

void VulkanApp::createImageViews() {
    swapchainViews_.resize(swapchainImages_.size());
    for (size_t i = 0; i < swapchainImages_.size(); ++i) {
        VkImageViewCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = swapchainImages_[i];
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = swapchainFormat_;
        createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        createInfo.subresourceRange.baseMipLevel = 0;
        createInfo.subresourceRange.levelCount = 1;
        createInfo.subresourceRange.baseArrayLayer = 0;
        createInfo.subresourceRange.layerCount = 1;
        checkVk(vkCreateImageView(device_, &createInfo, nullptr,
                                  &swapchainViews_[i]),
                "vkCreateImageView(swapchain)");
    }
}

void VulkanApp::createRenderPass() {
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat_;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    createInfo.attachmentCount = 1;
    createInfo.pAttachments = &colorAttachment;
    createInfo.subpassCount = 1;
    createInfo.pSubpasses = &subpass;
    createInfo.dependencyCount = 1;
    createInfo.pDependencies = &dependency;
    checkVk(vkCreateRenderPass(device_, &createInfo, nullptr, &renderPass_),
            "vkCreateRenderPass");
}

void VulkanApp::createDescriptorSetLayout() {
    VkDescriptorSetLayoutBinding samplerBinding{};
    samplerBinding.binding = 0;
    samplerBinding.descriptorCount = 1;
    samplerBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerBinding.pImmutableSamplers = nullptr;
    samplerBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    createInfo.bindingCount = 1;
    createInfo.pBindings = &samplerBinding;
    checkVk(vkCreateDescriptorSetLayout(device_, &createInfo, nullptr,
                                        &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout");
}

void VulkanApp::createPipelines() {
    const std::string shaderDir = TPD_SHADER_DIR;
    VkShaderModule sceneVert = loadShader(shaderDir + "/scene.vert.spv");
    VkShaderModule sceneFrag = loadShader(shaderDir + "/scene.frag.spv");
    VkShaderModule uiVert = loadShader(shaderDir + "/ui.vert.spv");
    VkShaderModule uiFrag = loadShader(shaderDir + "/ui.frag.spv");

    VkPipelineShaderStageCreateInfo sceneStages[2]{};
    sceneStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sceneStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    sceneStages[0].module = sceneVert;
    sceneStages[0].pName = "main";
    sceneStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    sceneStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    sceneStages[1].module = sceneFrag;
    sceneStages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType =
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
    multisample.sType =
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorAttachment.blendEnable = VK_FALSE;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT,
                                            VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ScenePushConstants);
    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &descriptorSetLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushRange;
    checkVk(vkCreatePipelineLayout(device_, &layoutInfo, nullptr,
                                   &scenePipelineLayout_),
            "vkCreatePipelineLayout(scene)");

    VkPipelineLayoutCreateInfo uiLayoutInfo{};
    uiLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    uiLayoutInfo.setLayoutCount = 1;
    uiLayoutInfo.pSetLayouts = &descriptorSetLayout_;
    checkVk(vkCreatePipelineLayout(device_, &uiLayoutInfo, nullptr,
                                   &uiPipelineLayout_),
            "vkCreatePipelineLayout(ui)");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = sceneStages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = scenePipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                      nullptr, &scenePipeline_),
            "vkCreateGraphicsPipelines(scene)");

    sceneStages[0].module = uiVert;
    sceneStages[1].module = uiFrag;
    colorAttachment.blendEnable = VK_TRUE;
    colorAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
    pipelineInfo.layout = uiPipelineLayout_;
    checkVk(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineInfo,
                                      nullptr, &uiPipeline_),
            "vkCreateGraphicsPipelines(ui)");

    vkDestroyShaderModule(device_, sceneVert, nullptr);
    vkDestroyShaderModule(device_, sceneFrag, nullptr);
    vkDestroyShaderModule(device_, uiVert, nullptr);
    vkDestroyShaderModule(device_, uiFrag, nullptr);
}

void VulkanApp::createCommandResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilies_.graphics;
    checkVk(vkCreateCommandPool(device_, &poolInfo, nullptr, &commandPool_),
            "vkCreateCommandPool");

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &imageAvailable_),
            "vkCreateSemaphore(imageAvailable)");
    checkVk(vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &renderFinished_),
            "vkCreateSemaphore(renderFinished)");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    checkVk(vkCreateFence(device_, &fenceInfo, nullptr, &frameFence_),
            "vkCreateFence");
}

void VulkanApp::createSwapchainResources() {
    framebuffers_.resize(swapchainViews_.size());
    for (size_t i = 0; i < swapchainViews_.size(); ++i) {
        VkImageView attachments[] = {swapchainViews_[i]};
        VkFramebufferCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        createInfo.renderPass = renderPass_;
        createInfo.attachmentCount = 1;
        createInfo.pAttachments = attachments;
        createInfo.width = swapchainExtent_.width;
        createInfo.height = swapchainExtent_.height;
        createInfo.layers = 1;
        checkVk(vkCreateFramebuffer(device_, &createInfo, nullptr,
                                    &framebuffers_[i]),
                "vkCreateFramebuffer");
    }

    commandBuffers_.resize(swapchainImages_.size());
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers_.size());
    checkVk(vkAllocateCommandBuffers(device_, &allocInfo, commandBuffers_.data()),
            "vkAllocateCommandBuffers");
}

void VulkanApp::cleanupSwapchain() {
    if (!commandBuffers_.empty()) {
        vkFreeCommandBuffers(device_, commandPool_,
                             static_cast<uint32_t>(commandBuffers_.size()),
                             commandBuffers_.data());
        commandBuffers_.clear();
    }
    for (VkFramebuffer framebuffer : framebuffers_) {
        vkDestroyFramebuffer(device_, framebuffer, nullptr);
    }
    framebuffers_.clear();
    for (VkImageView view : swapchainViews_) {
        vkDestroyImageView(device_, view, nullptr);
    }
    swapchainViews_.clear();
    if (swapchain_ != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device_, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    swapchainImages_.clear();
}

void VulkanApp::recreateSwapchain() {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    while (width == 0 || height == 0) {
        glfwGetFramebufferSize(window_, &width, &height);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(device_);
    cleanupSwapchain();
    createSwapchain();
    createImageViews();
    createSwapchainResources();
}

uint32_t VulkanApp::findMemoryType(uint32_t typeFilter,
                                   VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &memoryProperties);
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((typeFilter & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & properties) ==
                properties) {
            return i;
        }
    }
    throw std::runtime_error("no suitable Vulkan memory type");
}

void VulkanApp::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                             VkMemoryPropertyFlags properties, VkBuffer& buffer,
                             VkDeviceMemory& memory) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateBuffer(device_, &bufferInfo, nullptr, &buffer),
            "vkCreateBuffer");
    VkMemoryRequirements requirements{};
    vkGetBufferMemoryRequirements(device_, buffer, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, properties);
    checkVk(vkAllocateMemory(device_, &allocInfo, nullptr, &memory),
            "vkAllocateMemory(buffer)");
    checkVk(vkBindBufferMemory(device_, buffer, memory, 0),
            "vkBindBufferMemory");
}

void VulkanApp::createImage(uint32_t width, uint32_t height, uint32_t layers,
                            VkFormat format, VkImageUsageFlags usage,
                            VkImage& image, VkDeviceMemory& memory) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = layers;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    checkVk(vkCreateImage(device_, &imageInfo, nullptr, &image),
            "vkCreateImage");
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(device_, image, &requirements);
    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = requirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    checkVk(vkAllocateMemory(device_, &allocInfo, nullptr, &memory),
            "vkAllocateMemory(image)");
    checkVk(vkBindImageMemory(device_, image, memory, 0), "vkBindImageMemory");
}

VkCommandBuffer VulkanApp::beginSingleTimeCommands() {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool_;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    checkVk(vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer),
            "vkAllocateCommandBuffers(single)");
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer(single)");
    return commandBuffer;
}

void VulkanApp::endSingleTimeCommands(VkCommandBuffer commandBuffer) {
    checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(single)");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, VK_NULL_HANDLE),
            "vkQueueSubmit(single)");
    checkVk(vkQueueWaitIdle(graphicsQueue_), "vkQueueWaitIdle(single)");
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
}

void VulkanApp::transitionImageLayout(VkImage image, VkFormat, VkImageLayout oldLayout,
                                      VkImageLayout newLayout, uint32_t baseLayer,
                                      uint32_t layerCount) {
    const VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = baseLayer;
    barrier.subresourceRange.layerCount = layerCount;

    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage =
        VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0, 0,
                         nullptr, 0, nullptr, 1, &barrier);
    endSingleTimeCommands(commandBuffer);
}

void VulkanApp::copyBufferToImage(VkBuffer buffer, VkImage image, uint32_t width,
                                  uint32_t height, uint32_t baseLayer) {
    const VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = baseLayer;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};
    vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(commandBuffer);
}

VkShaderModule VulkanApp::loadShader(const std::string& path) const {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("cannot open shader: " + path);
    }
    const size_t size = static_cast<size_t>(file.tellg());
    std::vector<char> code(size);
    file.seekg(0);
    file.read(code.data(), static_cast<std::streamsize>(code.size()));
    return createShaderModule(code);
}

VkShaderModule VulkanApp::createShaderModule(
    const std::vector<char>& code) const {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule module = VK_NULL_HANDLE;
    checkVk(vkCreateShaderModule(device_, &createInfo, nullptr, &module),
            "vkCreateShaderModule");
    return module;
}

std::vector<uint8_t> VulkanApp::generatePagePixels(int contentId) const {
    static const std::array<std::array<float, 3>, 12> palette = {{
        {{0.25f, 0.62f, 0.95f}},
        {{0.18f, 0.82f, 0.64f}},
        {{0.95f, 0.64f, 0.23f}},
        {{0.88f, 0.31f, 0.48f}},
        {{0.61f, 0.47f, 0.96f}},
        {{0.30f, 0.78f, 0.91f}},
        {{0.92f, 0.82f, 0.31f}},
        {{0.42f, 0.87f, 0.43f}},
        {{0.96f, 0.42f, 0.27f}},
        {{0.35f, 0.55f, 0.90f}},
        {{0.80f, 0.38f, 0.92f}},
        {{0.28f, 0.83f, 0.82f}},
    }};

    std::vector<uint8_t> pixels(kPageSize * kPageSize * 4);
    const int index = contentId < 0 ? 0 : contentId;
    const float baseR = contentId < 0 ? 0.10f : palette[index % 12][0];
    const float baseG = contentId < 0 ? 0.11f : palette[index % 12][1];
    const float baseB = contentId < 0 ? 0.13f : palette[index % 12][2];

    for (uint32_t y = 0; y < kPageSize; ++y) {
        for (uint32_t x = 0; x < kPageSize; ++x) {
            const float u = static_cast<float>(x) / (kPageSize - 1);
            const float v = static_cast<float>(y) / (kPageSize - 1);
            const int cell = static_cast<int>(x / 32 + y / 32 + index);
            const float checker = (cell & 1) ? 0.09f : -0.045f;
            const float band =
                0.5f + 0.5f * std::sin((x * 0.071f + y * 0.043f) + index * 1.7f);
            const float ring = 0.5f + 0.5f * std::sin(
                std::sqrt((u - 0.5f) * (u - 0.5f) +
                          (v - 0.5f) * (v - 0.5f)) *
                95.0f);
            const float line = (x % 32 < 2 || y % 32 < 2) ? 0.12f : 0.0f;
            const float quadrant =
                ((static_cast<int>(x / 64) + static_cast<int>(y / 64) + index) & 1)
                    ? 0.10f
                    : -0.02f;
            const float vignette =
                1.0f - 0.30f * std::pow(std::max(std::abs(u - 0.5f),
                                                 std::abs(v - 0.5f)) *
                                        2.0f,
                                        2.0f);

            float r = baseR * (0.52f + 0.38f * band + checker + line + quadrant);
            float g = baseG * (0.50f + 0.38f * ring + checker + line + quadrant);
            float b = baseB * (0.58f + 0.32f * band + checker + line + quadrant);
            r = std::clamp(r * vignette + 0.025f, 0.0f, 1.0f);
            g = std::clamp(g * vignette + 0.025f, 0.0f, 1.0f);
            b = std::clamp(b * vignette + 0.030f, 0.0f, 1.0f);

            const uint32_t border = 5;
            if (x < border || y < border || x >= kPageSize - border ||
                y >= kPageSize - border) {
                r = std::min(1.0f, baseR + 0.22f);
                g = std::min(1.0f, baseG + 0.22f);
                b = std::min(1.0f, baseB + 0.22f);
            }

            const size_t offset =
                (static_cast<size_t>(y) * kPageSize + x) * 4;
            pixels[offset + 0] = static_cast<uint8_t>(r * 255.0f);
            pixels[offset + 1] = static_cast<uint8_t>(g * 255.0f);
            pixels[offset + 2] = static_cast<uint8_t>(b * 255.0f);
            pixels[offset + 3] = 255;
        }
    }
    return pixels;
}

void VulkanApp::createTextureArray() {
    const uint32_t layers = static_cast<uint32_t>(model_.config().layerCount);
    createImage(kPageSize, kPageSize, layers, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                arrayImage_, arrayMemory_);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = arrayImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = layers;
    checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &arrayView_),
            "vkCreateImageView(array)");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = 0.0f;
    checkVk(vkCreateSampler(device_, &samplerInfo, nullptr, &arraySampler_),
            "vkCreateSampler(array)");

    const VkDeviceSize layerBytes = kPageSize * kPageSize * 4;
    const VkDeviceSize totalBytes = layerBytes * layers;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(totalBytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);
    void* mapped = nullptr;
    checkVk(vkMapMemory(device_, stagingMemory, 0, totalBytes, 0, &mapped),
            "vkMapMemory(array staging)");
    const std::vector<uint8_t> placeholder = generatePagePixels(-1);
    for (uint32_t layer = 0; layer < layers; ++layer) {
        std::memcpy(static_cast<uint8_t*>(mapped) + layer * layerBytes,
                    placeholder.data(), static_cast<size_t>(layerBytes));
    }
    vkUnmapMemory(device_, stagingMemory);

    transitionImageLayout(arrayImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, layers);
    const VkCommandBuffer commandBuffer = beginSingleTimeCommands();
    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.layerCount = layers;
    region.imageExtent = {kPageSize, kPageSize, 1};
    vkCmdCopyBufferToImage(commandBuffer, stagingBuffer, arrayImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    endSingleTimeCommands(commandBuffer);
    transitionImageLayout(arrayImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, layers);

    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
}

void VulkanApp::createUiTexture() {
    createImage(layout::kWidth, layout::kHeight, 1, VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                uiImage_, uiMemory_);
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = uiImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    checkVk(vkCreateImageView(device_, &viewInfo, nullptr, &uiView_),
            "vkCreateImageView(ui)");

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    checkVk(vkCreateSampler(device_, &samplerInfo, nullptr, &uiSampler_),
            "vkCreateSampler(ui)");

    const VkDeviceSize bytes =
        static_cast<VkDeviceSize>(layout::kWidth) * layout::kHeight * 4;
    createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 uiStagingBuffer_, uiStagingMemory_);
    checkVk(vkMapMemory(device_, uiStagingMemory_, 0, bytes, 0,
                        &uiStagingMapped_),
            "vkMapMemory(ui staging)");
    transitionImageLayout(uiImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
}

void VulkanApp::createDescriptors() {
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 2;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 2;
    checkVk(vkCreateDescriptorPool(device_, &poolInfo, nullptr, &descriptorPool_),
            "vkCreateDescriptorPool");

    VkDescriptorSetLayout layouts[2] = {descriptorSetLayout_, descriptorSetLayout_};
    VkDescriptorSet sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool_;
    allocInfo.descriptorSetCount = 2;
    allocInfo.pSetLayouts = layouts;
    checkVk(vkAllocateDescriptorSets(device_, &allocInfo, sets),
            "vkAllocateDescriptorSets");
    arrayDescriptorSet_ = sets[0];
    uiDescriptorSet_ = sets[1];

    VkDescriptorImageInfo arrayInfo{};
    arrayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    arrayInfo.imageView = arrayView_;
    arrayInfo.sampler = arraySampler_;
    VkDescriptorImageInfo uiInfo{};
    uiInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    uiInfo.imageView = uiView_;
    uiInfo.sampler = uiSampler_;

    VkWriteDescriptorSet writes[2]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = arrayDescriptorSet_;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &arrayInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = uiDescriptorSet_;
    writes[1].dstBinding = 0;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].pImageInfo = &uiInfo;
    vkUpdateDescriptorSets(device_, 2, writes, 0, nullptr);
}

void VulkanApp::uploadLayerContent(int layer, int contentId) {
    if (layer < 0 || layer >= model_.config().layerCount) {
        return;
    }
    const VkDeviceSize bytes = kPageSize * kPageSize * 4;
    std::vector<uint8_t> pixels = generatePagePixels(contentId);

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    createBuffer(bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);
    void* mapped = nullptr;
    checkVk(vkMapMemory(device_, stagingMemory, 0, bytes, 0, &mapped),
            "vkMapMemory(layer)");
    std::memcpy(mapped, pixels.data(), pixels.size());
    vkUnmapMemory(device_, stagingMemory);

    vkDeviceWaitIdle(device_);
    transitionImageLayout(arrayImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          static_cast<uint32_t>(layer), 1);
    copyBufferToImage(stagingBuffer, arrayImage_, kPageSize, kPageSize,
                      static_cast<uint32_t>(layer));
    transitionImageLayout(arrayImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          static_cast<uint32_t>(layer), 1);

    vkDestroyBuffer(device_, stagingBuffer, nullptr);
    vkFreeMemory(device_, stagingMemory, nullptr);
}

void VulkanApp::uploadUiPixels(const std::vector<uint8_t>& pixels) {
    const size_t expected =
        static_cast<size_t>(layout::kWidth) * layout::kHeight * 4;
    if (pixels.size() != expected || uiStagingMapped_ == nullptr) {
        return;
    }
    std::memcpy(uiStagingMapped_, pixels.data(), expected);

    vkDeviceWaitIdle(device_);
    transitionImageLayout(uiImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1);
    copyBufferToImage(uiStagingBuffer_, uiImage_, layout::kWidth,
                      layout::kHeight, 0);
    transitionImageLayout(uiImage_, VK_FORMAT_R8G8B8A8_UNORM,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1);
}

void VulkanApp::drawScene(VkCommandBuffer commandBuffer) {
    if (scenarios_.empty()) {
        return;
    }
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      scenePipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            scenePipelineLayout_, 0, 1, &arrayDescriptorSet_, 0,
                            nullptr);

    const float scaleX =
        static_cast<float>(swapchainExtent_.width) / layout::kWidth;
    const float scaleY =
        static_cast<float>(swapchainExtent_.height) / layout::kHeight;
    for (const PageEntry& page : model_.pages()) {
        const layout::Rect tile = layout::tileRect(page.id);
        VkViewport viewport{};
        viewport.x = static_cast<float>(tile.x) * scaleX;
        viewport.y = static_cast<float>(tile.y) * scaleY;
        viewport.width = static_cast<float>(tile.w) * scaleX;
        viewport.height = static_cast<float>(tile.h) * scaleY;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(viewport.x),
                          static_cast<int32_t>(viewport.y)};
        scissor.extent = {static_cast<uint32_t>(viewport.width),
                          static_cast<uint32_t>(viewport.height)};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        ScenePushConstants push{};
        push.layerIndex = model_.sampleLayer(page.id);
        if (page.empty) {
            push.mode = 1;
        } else if (push.layerIndex >= 0) {
            push.mode = 3;
        } else if (page.state == PageState::Queued ||
                   page.state == PageState::Uploading) {
            push.mode = 2;
        } else {
            push.mode = 0;
        }
        vkCmdPushConstants(commandBuffer, scenePipelineLayout_,
                           VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(ScenePushConstants), &push);
        vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    }
}

void VulkanApp::recordCommandBuffer(VkCommandBuffer commandBuffer,
                                    uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    checkVk(vkBeginCommandBuffer(commandBuffer, &beginInfo),
            "vkBeginCommandBuffer");

    VkClearValue clear{};
    clear.color = {{0.031f, 0.043f, 0.062f, 1.0f}};
    VkRenderPassBeginInfo renderInfo{};
    renderInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderInfo.renderPass = renderPass_;
    renderInfo.framebuffer = framebuffers_[imageIndex];
    renderInfo.renderArea.offset = {0, 0};
    renderInfo.renderArea.extent = swapchainExtent_;
    renderInfo.clearValueCount = 1;
    renderInfo.pClearValues = &clear;
    vkCmdBeginRenderPass(commandBuffer, &renderInfo, VK_SUBPASS_CONTENTS_INLINE);

    drawScene(commandBuffer);

    VkViewport fullViewport{};
    fullViewport.width = static_cast<float>(swapchainExtent_.width);
    fullViewport.height = static_cast<float>(swapchainExtent_.height);
    fullViewport.minDepth = 0.0f;
    fullViewport.maxDepth = 1.0f;
    vkCmdSetViewport(commandBuffer, 0, 1, &fullViewport);
    VkRect2D fullScissor{};
    fullScissor.extent = swapchainExtent_;
    vkCmdSetScissor(commandBuffer, 0, 1, &fullScissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      uiPipeline_);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            uiPipelineLayout_, 0, 1, &uiDescriptorSet_, 0,
                            nullptr);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);

    vkCmdEndRenderPass(commandBuffer);
    checkVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
}

void VulkanApp::drawFrame() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    if (framebufferResized_) {
        framebufferResized_ = false;
        recreateSwapchain();
    }

    checkVk(vkWaitForFences(device_, 1, &frameFence_, VK_TRUE, UINT64_MAX),
            "vkWaitForFences");

    uint32_t imageIndex = 0;
    const VkResult acquireResult =
        vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, imageAvailable_,
                              VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        recreateSwapchain();
        return;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        throw std::runtime_error("vkAcquireNextImageKHR failed");
    }

    DebugFrame frame;
    frame.model = &model_;
    frame.scenario = &scenarios_[scenarioIndex_];
    frame.replayStep = replayStep_;
    frame.replayTotal = scenarios_[scenarioIndex_].pages.size();
    frame.playing = playing_;
    frame.fixedCamera = true;
    frame.speed = speed_;
    frame.fps = smoothedFps_;
    ui_.build(frame, uiPixels_);
    uploadUiPixels(uiPixels_);

    checkVk(vkResetFences(device_, 1, &frameFence_), "vkResetFences");
    checkVk(vkResetCommandBuffer(commandBuffers_[imageIndex], 0),
            "vkResetCommandBuffer");
    recordCommandBuffer(commandBuffers_[imageIndex], imageIndex);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    const VkSemaphore waitSemaphores[] = {imageAvailable_};
    const VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
    };
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffers_[imageIndex];
    const VkSemaphore signalSemaphores[] = {renderFinished_};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;
    checkVk(vkQueueSubmit(graphicsQueue_, 1, &submitInfo, frameFence_),
            "vkQueueSubmit");

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = vkQueuePresentKHR(presentQueue_, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR ||
        presentResult == VK_SUBOPTIMAL_KHR) {
        framebufferResized_ = true;
    } else if (presentResult != VK_SUCCESS) {
        throw std::runtime_error("vkQueuePresentKHR failed");
    }
}

void VulkanApp::cleanup() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
    if (device_ != VK_NULL_HANDLE && uiStagingMapped_ != nullptr) {
        vkUnmapMemory(device_, uiStagingMemory_);
        uiStagingMapped_ = nullptr;
    }
    if (device_ != VK_NULL_HANDLE) {
        if (uiStagingBuffer_ != VK_NULL_HANDLE) {
            vkDestroyBuffer(device_, uiStagingBuffer_, nullptr);
        }
        if (uiStagingMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, uiStagingMemory_, nullptr);
        }
        if (uiView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, uiView_, nullptr);
        }
        if (uiSampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, uiSampler_, nullptr);
        }
        if (uiImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, uiImage_, nullptr);
        }
        if (uiMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, uiMemory_, nullptr);
        }
        if (arrayView_ != VK_NULL_HANDLE) {
            vkDestroyImageView(device_, arrayView_, nullptr);
        }
        if (arraySampler_ != VK_NULL_HANDLE) {
            vkDestroySampler(device_, arraySampler_, nullptr);
        }
        if (arrayImage_ != VK_NULL_HANDLE) {
            vkDestroyImage(device_, arrayImage_, nullptr);
        }
        if (arrayMemory_ != VK_NULL_HANDLE) {
            vkFreeMemory(device_, arrayMemory_, nullptr);
        }
        if (descriptorPool_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);
        }
        if (scenePipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, scenePipeline_, nullptr);
        }
        if (uiPipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, uiPipeline_, nullptr);
        }
        if (scenePipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, scenePipelineLayout_, nullptr);
        }
        if (uiPipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, uiPipelineLayout_, nullptr);
        }
        if (descriptorSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
        }
        if (renderPass_ != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device_, renderPass_, nullptr);
        }
        cleanupSwapchain();
        if (imageAvailable_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, imageAvailable_, nullptr);
        }
        if (renderFinished_ != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, renderFinished_, nullptr);
        }
        if (frameFence_ != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frameFence_, nullptr);
        }
        if (commandPool_ != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, commandPool_, nullptr);
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
    glfwTerminate();
}

}  // namespace tpd
