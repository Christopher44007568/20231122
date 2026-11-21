#include "renderer.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <fstream>

static const VkFormat kGbufFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
static const VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;
static const VkFormat kMetricsFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
static const VkFormat kDisplayFormat = VK_FORMAT_R8G8B8A8_UNORM;

void Renderer::init(GLFWwindow* window, uint32_t width, uint32_t height) {
    m_window = window;
    m_headless = (window == nullptr);
    m_width = width;
    m_height = height;
    createInstance(window);
    pickDevice();

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = m_gpu.queueFamily;
    VK_CHECK(vkCreateCommandPool(m_gpu.device, &pi, nullptr, &m_cmdPool));

    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateSemaphore(m_gpu.device, &sci, nullptr, &m_semAcquire));
    VK_CHECK(vkCreateSemaphore(m_gpu.device, &sci, nullptr, &m_semDone));
    VK_CHECK(vkCreateFence(m_gpu.device, &fci, nullptr, &m_fence));

    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(m_gpu.device, &si, nullptr, &m_sampler));

    if (!m_headless)
        createSwapchain();
    createTargets();
    createPipelines();

    VkDeviceSize diagSize = 16 + (VkDeviceSize)kMaxDiagRecords * sizeof(DiagRecord);
    m_diagBuf = createBuffer(m_gpu, diagSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                 VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // Descriptor pool + sets.
    VkDescriptorPoolSize sizes[2]{};
    sizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    sizes[0].descriptorCount = 8;
    sizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    sizes[1].descriptorCount = 2;
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 4;
    dpi.poolSizeCount = 2;
    dpi.pPoolSizes = sizes;
    VK_CHECK(vkCreateDescriptorPool(m_gpu.device, &dpi, nullptr, &m_descPool));

    VkDescriptorSetLayout layouts[3] = {m_edgeDSL, m_diagDSL, m_displayDSL};
    VkDescriptorSet sets[3];
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = m_descPool;
    dai.descriptorSetCount = 3;
    dai.pSetLayouts = layouts;
    VK_CHECK(vkAllocateDescriptorSets(m_gpu.device, &dai, sets));
    m_edgeDS = sets[0];
    m_diagDS = sets[1];
    m_displayDS = sets[2];

    VkDescriptorImageInfo gbufInfo{m_sampler, m_gbuf.view,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorImageInfo metricsInfo{m_sampler, m_metrics.view,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkDescriptorBufferInfo diagInfo{m_diagBuf.buf, 0, diagSize};
    VkWriteDescriptorSet writes[4]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_edgeDS;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &gbufInfo;
    writes[1] = writes[0];
    writes[1].dstSet = m_diagDS;
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_diagDS;
    writes[2].dstBinding = 1;
    writes[2].descriptorCount = 1;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].pBufferInfo = &diagInfo;
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_displayDS;
    writes[3].dstBinding = 0;
    writes[3].descriptorCount = 1;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].pImageInfo = &metricsInfo;
    vkUpdateDescriptorSets(m_gpu.device, 4, writes, 0, nullptr);
    // displayDS binding 1 = gbuf.
    VkWriteDescriptorSet w4{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w4.dstSet = m_displayDS;
    w4.dstBinding = 1;
    w4.descriptorCount = 1;
    w4.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    w4.pImageInfo = &gbufInfo;
    vkUpdateDescriptorSets(m_gpu.device, 1, &w4, 0, nullptr);
}

void Renderer::createInstance(GLFWwindow* window) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "outline_diag";
    app.apiVersion = VK_API_VERSION_1_2;
    std::vector<const char*> exts;
    if (window) {
        uint32_t n = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&n);
        for (uint32_t i = 0; i < n; ++i) exts.push_back(glfwExts[i]);
    }
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = (uint32_t)exts.size();
    ci.ppEnabledExtensionNames = exts.data();
    VK_CHECK(vkCreateInstance(&ci, nullptr, &m_instance));

    if (window)
        VK_CHECK(glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface));
}

void Renderer::pickDevice() {
    uint32_t n = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &n, nullptr));
    if (n == 0) {
        std::fprintf(stderr, "no Vulkan devices\n");
        std::exit(1);
    }
    std::vector<VkPhysicalDevice> devs(n);
    VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &n, devs.data()));
    for (VkPhysicalDevice d : devs) {
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qn, qs.data());
        for (uint32_t i = 0; i < qn; ++i) {
            if (!(qs[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            if (m_surface) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(d, i, m_surface, &present);
                if (!present) continue;
            }
            m_gpu.phys = d;
            m_gpu.queueFamily = i;
            break;
        }
        if (m_gpu.phys) break;
    }
    if (!m_gpu.phys) {
        std::fprintf(stderr, "no suitable queue family\n");
        std::exit(1);
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_gpu.phys, &props);
    std::fprintf(stderr, "GPU: %s\n", props.deviceName);

    const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    float prio = 1.0f;
    qi.queueFamilyIndex = m_gpu.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.enabledExtensionCount = m_surface ? 1u : 0u;
    di.ppEnabledExtensionNames = devExts;
    VK_CHECK(vkCreateDevice(m_gpu.phys, &di, nullptr, &m_gpu.device));
    m_gpu.queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(m_gpu.device, m_gpu.queueFamily, 0, &m_gpu.queue);
    vkGetPhysicalDeviceMemoryProperties(m_gpu.phys, &m_gpu.memProps);
}

void Renderer::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_gpu.phys, m_surface, &caps));
    uint32_t fn = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu.phys, m_surface, &fn, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(fn);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_gpu.phys, m_surface, &fn, formats.data());
    VkSurfaceFormatKHR chosen = formats[0];
    for (const auto& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM ||
             f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    m_swapFormat = chosen.format;
    VkExtent2D extent = caps.currentExtent;
    if (extent.width == UINT32_MAX)
        extent = {m_width, m_height};
    m_width = extent.width;
    m_height = extent.height;

    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    ci.surface = m_surface;
    ci.minImageCount = caps.minImageCount + 1 > caps.maxImageCount && caps.maxImageCount > 0
                           ? caps.maxImageCount
                           : caps.minImageCount + 1;
    ci.imageFormat = m_swapFormat;
    ci.imageColorSpace = chosen.colorSpace;
    ci.imageExtent = extent;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    ci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    ci.clipped = VK_TRUE;
    VK_CHECK(vkCreateSwapchainKHR(m_gpu.device, &ci, nullptr, &m_swapchain));

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(m_gpu.device, m_swapchain, &n, nullptr);
    m_swapImages.resize(n);
    vkGetSwapchainImagesKHR(m_gpu.device, m_swapchain, &n, m_swapImages.data());
    m_swapViews.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = m_swapImages[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_swapFormat;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(m_gpu.device, &vi, nullptr, &m_swapViews[i]));
    }
}

void Renderer::destroySwapchain() {
    for (VkFramebuffer fb : m_swapFbs) vkDestroyFramebuffer(m_gpu.device, fb, nullptr);
    m_swapFbs.clear();
    for (VkImageView v : m_swapViews) vkDestroyImageView(m_gpu.device, v, nullptr);
    m_swapViews.clear();
    if (m_swapchain) vkDestroySwapchainKHR(m_gpu.device, m_swapchain, nullptr);
    m_swapchain = VK_NULL_HANDLE;
}

void Renderer::createTargets() {
    m_gbuf = createImage2D(m_gpu, m_width, m_height, kGbufFormat,
                           VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                           VK_IMAGE_ASPECT_COLOR_BIT);
    m_depth = createImage2D(m_gpu, m_width, m_height, kDepthFormat,
                            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
                            VK_IMAGE_ASPECT_DEPTH_BIT);
    m_metrics = createImage2D(m_gpu, m_width, m_height, kMetricsFormat,
                              VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                              VK_IMAGE_ASPECT_COLOR_BIT);
    if (m_headless) {
        m_display = createImage2D(m_gpu, m_width, m_height, kDisplayFormat,
                                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                  VK_IMAGE_ASPECT_COLOR_BIT);
        m_readback = createBuffer(m_gpu, (VkDeviceSize)m_width * m_height * 4,
                                  VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
}

void Renderer::destroyTargets() {
    destroyImage(m_gpu, m_gbuf);
    destroyImage(m_gpu, m_depth);
    destroyImage(m_gpu, m_metrics);
    if (m_headless) {
        destroyImage(m_gpu, m_display);
        destroyBuffer(m_gpu, m_readback);
    }
}

static VkRenderPass makeColorRP(VkDevice dev, VkFormat format, VkImageLayout finalLayout,
                                bool depth) {
    VkAttachmentDescription att[2]{};
    att[0].format = format;
    att[0].samples = VK_SAMPLE_COUNT_1_BIT;
    att[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att[0].finalLayout = finalLayout;
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &colorRef;
    uint32_t attCount = 1;
    if (depth) {
        att[1].format = kDepthFormat;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        sub.pDepthStencilAttachment = &depthRef;
        attCount = 2;
    }
    VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ci.attachmentCount = attCount;
    ci.pAttachments = att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    VkRenderPass rp;
    VK_CHECK(vkCreateRenderPass(dev, &ci, nullptr, &rp));
    return rp;
}

static VkPipeline makeFullscreenPipe(VkDevice dev, VkRenderPass rp, VkPipelineLayout layout,
                                     VkShaderModule frag) {
    VkShaderModule vert = loadShaderModule(dev, std::string(SHADER_DIR) + "/fullscreen.vert.spv");
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1] = stages[0];
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dynStates;
    VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pColorBlendState = &cb;
    ci.pDynamicState = &dyn;
    ci.layout = layout;
    ci.renderPass = rp;
    VkPipeline pipe;
    VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &pipe));
    vkDestroyShaderModule(dev, vert, nullptr);
    vkDestroyShaderModule(dev, frag, nullptr);
    return pipe;
}

static VkDescriptorSetLayout makeDSL(VkDevice dev,
                                     std::initializer_list<VkDescriptorType> types) {
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    uint32_t i = 0;
    for (VkDescriptorType t : types) {
        VkDescriptorSetLayoutBinding b{};
        b.binding = i++;
        b.descriptorType = t;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        bindings.push_back(b);
    }
    VkDescriptorSetLayoutCreateInfo ci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    ci.bindingCount = (uint32_t)bindings.size();
    ci.pBindings = bindings.data();
    VkDescriptorSetLayout l;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &ci, nullptr, &l));
    return l;
}

void Renderer::createPipelines() {
    VkDevice dev = m_gpu.device;
    m_geomRP = makeColorRP(dev, kGbufFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, true);
    m_edgeRP = makeColorRP(dev, kMetricsFormat, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, false);
    VkFormat displayFmt = m_headless ? kDisplayFormat : m_swapFormat;
    m_displayRP = makeColorRP(dev, displayFmt,
                              m_headless ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                         : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                              false);
    // Diag pass: no attachments, fragment shader only writes to SSBO.
    {
        VkSubpassDescription sub{};
        sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ci.subpassCount = 1;
        ci.pSubpasses = &sub;
        VK_CHECK(vkCreateRenderPass(dev, &ci, nullptr, &m_diagRP));
    }

    // Framebuffers.
    auto makeFB = [&](VkRenderPass rp, std::initializer_list<VkImageView> views,
                      VkFramebuffer* out) {
        std::vector<VkImageView> v(views);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = rp;
        fi.attachmentCount = (uint32_t)v.size();
        fi.pAttachments = v.data();
        fi.width = m_width;
        fi.height = m_height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(dev, &fi, nullptr, out));
    };
    makeFB(m_geomRP, {m_gbuf.view, m_depth.view}, &m_geomFB);
    makeFB(m_edgeRP, {m_metrics.view}, &m_edgeFB);
    {
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = m_diagRP;
        fi.attachmentCount = 0;
        fi.width = m_width;
        fi.height = m_height;
        fi.layers = 1;
        VK_CHECK(vkCreateFramebuffer(dev, &fi, nullptr, &m_diagFB));
    }
    if (m_headless) {
        makeFB(m_displayRP, {m_display.view}, &m_displayFB);
    } else {
        m_swapFbs.resize(m_swapViews.size());
        for (size_t i = 0; i < m_swapViews.size(); ++i)
            makeFB(m_displayRP, {m_swapViews[i]}, &m_swapFbs[i]);
    }

    // Descriptor set layouts.
    m_edgeDSL = makeDSL(dev, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});
    m_diagDSL = makeDSL(dev, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER});
    m_displayDSL = makeDSL(dev, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                 VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER});

    // Pipeline layouts with push constants.
    auto makePL = [&](VkDescriptorSetLayout dsl, VkShaderStageFlags stage, uint32_t size) {
        VkPushConstantRange pcr{stage, 0, size};
        VkPipelineLayoutCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        ci.setLayoutCount = dsl ? 1u : 0u;
        ci.pSetLayouts = &dsl;
        ci.pushConstantRangeCount = 1;
        ci.pPushConstantRanges = &pcr;
        VkPipelineLayout pl;
        VK_CHECK(vkCreatePipelineLayout(dev, &ci, nullptr, &pl));
        return pl;
    };
    m_geomPL = makePL(VK_NULL_HANDLE, VK_SHADER_STAGE_VERTEX_BIT, 128);
    m_edgePL = makePL(m_edgeDSL, VK_SHADER_STAGE_FRAGMENT_BIT, 32);
    m_diagPL = makePL(m_diagDSL, VK_SHADER_STAGE_FRAGMENT_BIT, 36);
    m_displayPL = makePL(m_displayDSL, VK_SHADER_STAGE_FRAGMENT_BIT, 16);

    // Geometry pipeline.
    {
        VkShaderModule vs = loadShaderModule(dev, std::string(SHADER_DIR) + "/geom.vert.spv");
        VkShaderModule fs = loadShaderModule(dev, std::string(SHADER_DIR) + "/geom.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vs;
        stages[0].pName = "main";
        stages[1] = stages[0];
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fs;
        VkVertexInputBindingDescription bind{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attrs[2]{};
        attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
        attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, 12};
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = 1;
        vi.pVertexBindingDescriptions = &bind;
        vi.vertexAttributeDescriptionCount = 2;
        vi.pVertexAttributeDescriptions = attrs;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_BACK_BIT;
        rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        ds.depthTestEnable = VK_TRUE;
        ds.depthWriteEnable = VK_TRUE;
        ds.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState cba{};
        cba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &cba;
        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;
        VkGraphicsPipelineCreateInfo ci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        ci.stageCount = 2;
        ci.pStages = stages;
        ci.pVertexInputState = &vi;
        ci.pInputAssemblyState = &ia;
        ci.pViewportState = &vp;
        ci.pRasterizationState = &rs;
        ci.pMultisampleState = &ms;
        ci.pDepthStencilState = &ds;
        ci.pColorBlendState = &cb;
        ci.pDynamicState = &dyn;
        ci.layout = m_geomPL;
        ci.renderPass = m_geomRP;
        VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &ci, nullptr, &m_geomPipe));
        vkDestroyShaderModule(dev, vs, nullptr);
        vkDestroyShaderModule(dev, fs, nullptr);
    }

    m_edgePipe = makeFullscreenPipe(dev, m_edgeRP, m_edgePL,
        loadShaderModule(dev, std::string(SHADER_DIR) + "/edge.frag.spv"));
    m_diagPipe = makeFullscreenPipe(dev, m_diagRP, m_diagPL,
        loadShaderModule(dev, std::string(SHADER_DIR) + "/diag.frag.spv"));
    m_displayPipe = makeFullscreenPipe(dev, m_displayRP, m_displayPL,
        loadShaderModule(dev, std::string(SHADER_DIR) + "/display.frag.spv"));
}

static void uploadMesh(const Gpu& gpu, VkCommandPool pool, const Mesh& mesh,
                       Buffer& vb, Buffer& ib) {
    VkDeviceSize vsize = mesh.vertices.size() * sizeof(Vertex);
    VkDeviceSize isize = mesh.indices.size() * sizeof(uint32_t);
    vb = createBuffer(gpu, vsize,
                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    ib = createBuffer(gpu, isize,
                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    Buffer staging = createBuffer(gpu, vsize + isize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    void* p = nullptr;
    VK_CHECK(vkMapMemory(gpu.device, staging.mem, 0, vsize + isize, 0, &p));
    std::memcpy(p, mesh.vertices.data(), vsize);
    std::memcpy((char*)p + vsize, mesh.indices.data(), isize);
    vkUnmapMemory(gpu.device, staging.mem);
    VkCommandBuffer cmd = beginOneShot(gpu.device, pool);
    VkBufferCopy cpy{vsize, 0, vsize};
    cpy.srcOffset = 0;
    vkCmdCopyBuffer(cmd, staging.buf, vb.buf, 1, &cpy);
    cpy.srcOffset = vsize;
    cpy.size = isize;
    vkCmdCopyBuffer(cmd, staging.buf, ib.buf, 1, &cpy);
    endOneShot(gpu.device, pool, gpu.queue, cmd);
    destroyBuffer(gpu, staging);
}

void Renderer::uploadScene(const SceneSet& scenes) {
    uploadMesh(m_gpu, m_cmdPool, scenes.cube, m_cubeVB, m_cubeIB);
    uploadMesh(m_gpu, m_cmdPool, scenes.plane, m_planeVB, m_planeIB);
    m_cubeIndexCount = (uint32_t)scenes.cube.indices.size();
    m_planeIndexCount = (uint32_t)scenes.plane.indices.size();
}

VkCommandBuffer Renderer::recordFrame(const SceneSet& scenes, const FrameParams& params,
                                      VkFramebuffer displayFB) {
    const Scene& scene = scenes.scenes[params.sceneIndex];
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = m_cmdPool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(m_gpu.device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));

    VkViewport fullVp{0, 0, (float)m_width, (float)m_height, 0.0f, 1.0f};
    VkRect2D fullSc{{0, 0}, {m_width, m_height}};

    // 1) Geometry pass -> G-buffer.
    VkClearValue clears[2]{};
    clears[0].color = {{0.0f, 0.0f, 0.0f, 10000.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = m_geomRP;
    rbi.framebuffer = m_geomFB;
    rbi.renderArea = fullSc;
    rbi.clearValueCount = 2;
    rbi.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_geomPipe);
    vkCmdSetViewport(cmd, 0, 1, &fullVp);
    vkCmdSetScissor(cmd, 0, 1, &fullSc);

    float aspect = (float)m_width / (float)m_height;
    Mat4 view = mat4LookAt(scene.eye, scene.center, scene.up);
    Mat4 proj = mat4Perspective(scene.fovYDeg * 3.14159265f / 180.0f, aspect, 0.1f, 100.0f);
    proj.m[5] *= -1.0f; // Vulkan clip Y flip
    for (const Instance& inst : scene.instances) {
        Mat4 mv = mat4Mul(view, inst.model);
        struct {
            Mat4 mv, p;
        } pc{mv, proj};
        vkCmdPushConstants(cmd, m_geomPL, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);
        bool isCube = inst.mesh == &scenes.cube;
        VkBuffer vb = isCube ? m_cubeVB.buf : m_planeVB.buf;
        VkBuffer ib = isCube ? m_cubeIB.buf : m_planeIB.buf;
        uint32_t count = isCube ? m_cubeIndexCount : m_planeIndexCount;
        VkDeviceSize off = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &off);
        vkCmdBindIndexBuffer(cmd, ib, 0, VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(cmd, count, 1, 0, 0, 0);
    }
    vkCmdEndRenderPass(cmd);

    // 2) Edge metrics pass.
    struct EdgePC {
        float texel[2];
        float depthThreshold, normalThreshold, gtDepthEps, gtNormalEps;
    } epc{{1.0f / m_width, 1.0f / m_height},
          params.depthThreshold, params.normalThreshold,
          params.gtDepthEps, params.gtNormalEps};
    VkClearValue mclear{};
    rbi.renderPass = m_edgeRP;
    rbi.framebuffer = m_edgeFB;
    rbi.clearValueCount = 1;
    rbi.pClearValues = &mclear;
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_edgePipe);
    vkCmdSetViewport(cmd, 0, 1, &fullVp);
    vkCmdSetScissor(cmd, 0, 1, &fullSc);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_edgePL, 0, 1,
                            &m_edgeDS, 0, nullptr);
    vkCmdPushConstants(cmd, m_edgePL, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(epc), &epc);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd);

    // 3) Optional broken-line diagnostics (SSBO).
    if (params.runDiagnostics) {
        void* p = nullptr;
        VK_CHECK(vkMapMemory(m_gpu.device, m_diagBuf.mem, 0, 4, 0, &p));
        std::memset(p, 0, 4);
        vkUnmapMemory(m_gpu.device, m_diagBuf.mem);
        VkMemoryBarrier flush{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        flush.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        flush.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_HOST_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &flush, 0,
                             nullptr, 0, nullptr);
        struct DiagPC {
            float texel[2];
            float depthThreshold, normalThreshold, gtDepthEps, gtNormalEps;
            uint32_t maxRecords;
        } dpc{{1.0f / m_width, 1.0f / m_height},
              params.depthThreshold, params.normalThreshold,
              params.gtDepthEps, params.gtNormalEps, kMaxDiagRecords};
        rbi.renderPass = m_diagRP;
        rbi.framebuffer = m_diagFB;
        rbi.clearValueCount = 0;
        rbi.pClearValues = nullptr;
        vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_diagPipe);
        vkCmdSetViewport(cmd, 0, 1, &fullVp);
        vkCmdSetScissor(cmd, 0, 1, &fullSc);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_diagPL, 0, 1,
                                &m_diagDS, 0, nullptr);
        vkCmdPushConstants(cmd, m_diagPL, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(dpc),
                           &dpc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
        vkCmdEndRenderPass(cmd);
        VkMemoryBarrier rb{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        rb.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        rb.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                             VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &rb, 0, nullptr, 0,
                             nullptr);
    }

    // 4) Display pass: 2x2 quadrants.
    VkClearValue dclear{};
    rbi.renderPass = m_displayRP;
    rbi.framebuffer = displayFB;
    rbi.clearValueCount = 1;
    rbi.pClearValues = &dclear;
    // Framebuffer is patched in renderFrame via this lambda's capture.
    vkCmdBeginRenderPass(cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayPipe);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_displayPL, 0, 1,
                            &m_displayDS, 0, nullptr);
    float hw = (float)m_width * 0.5f, hh = (float)m_height * 0.5f;
    struct DisplayPC {
        uint32_t mode;
        float depthScale, normalScale;
    } dispPc{0, 0.5f, 1.0f};
    const float quadPos[4][2] = {{0, 0}, {hw, 0}, {0, hh}, {hw, hh}};
    for (uint32_t q = 0; q < 4; ++q) {
        VkViewport vp{quadPos[q][0], quadPos[q][1], hw, hh, 0.0f, 1.0f};
        VkRect2D sc{{(int32_t)quadPos[q][0], (int32_t)quadPos[q][1]},
                    {m_width / 2, m_height / 2}};
        vkCmdSetViewport(cmd, 0, 1, &vp);
        vkCmdSetScissor(cmd, 0, 1, &sc);
        dispPc.mode = q;
        vkCmdPushConstants(cmd, m_displayPL, VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                           sizeof(dispPc), &dispPc);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
    vkCmdEndRenderPass(cmd);

    // 5) Headless readback of the composited frame.
    if (m_headless) {
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_width, m_height, 1};
        vkCmdCopyImageToBuffer(cmd, m_display.img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               m_readback.buf, 1, &region);
    }
    VK_CHECK(vkEndCommandBuffer(cmd));
    return cmd;
}

void Renderer::renderFrame(const SceneSet& scenes, const FrameParams& params) {
    VK_CHECK(vkWaitForFences(m_gpu.device, 1, &m_fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(m_gpu.device, 1, &m_fence));

    VkFramebuffer fb = m_displayFB;
    uint32_t imageIndex = 0;
    if (!m_headless) {
        VkResult acq = vkAcquireNextImageKHR(m_gpu.device, m_swapchain, UINT64_MAX,
                                             m_semAcquire, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            vkDeviceWaitIdle(m_gpu.device);
            destroySwapchain();
            destroyTargets();
            int w = 0, h = 0;
            glfwGetFramebufferSize(m_window, &w, &h);
            m_width = (uint32_t)w;
            m_height = (uint32_t)h;
            createSwapchain();
            createTargets();
            // Recreate swapchain framebuffers against the existing display RP.
            m_swapFbs.resize(m_swapViews.size());
            for (size_t i = 0; i < m_swapViews.size(); ++i) {
                VkImageView v = m_swapViews[i];
                VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                fi.renderPass = m_displayRP;
                fi.attachmentCount = 1;
                fi.pAttachments = &v;
                fi.width = m_width;
                fi.height = m_height;
                fi.layers = 1;
                VK_CHECK(vkCreateFramebuffer(m_gpu.device, &fi, nullptr, &m_swapFbs[i]));
            }
            return; // skip this frame
        }
        VK_CHECK(acq);
        fb = m_swapFbs[imageIndex];
    }

    VkCommandBuffer cmd = recordFrame(scenes, params, fb);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = m_headless ? 0u : 1u;
    si.pWaitSemaphores = &m_semAcquire;
    si.pWaitDstStageMask = &waitStage;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    si.signalSemaphoreCount = m_headless ? 0u : 1u;
    si.pSignalSemaphores = &m_semDone;
    VK_CHECK(vkQueueSubmit(m_gpu.queue, 1, &si, m_fence));

    if (!m_headless) {
        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &m_semDone;
        pi.swapchainCount = 1;
        pi.pSwapchains = &m_swapchain;
        pi.pImageIndices = &imageIndex;
        VkResult pr = vkQueuePresentKHR(m_gpu.queue, &pi);
        if (pr != VK_SUCCESS && pr != VK_SUBOPTIMAL_KHR &&
            pr != VK_ERROR_OUT_OF_DATE_KHR)
            VK_CHECK(pr);
    }
    VK_CHECK(vkWaitForFences(m_gpu.device, 1, &m_fence, VK_TRUE, UINT64_MAX));
    vkFreeCommandBuffers(m_gpu.device, m_cmdPool, 1, &cmd);

    if (params.runDiagnostics)
        readbackDiagnostics();
}

void Renderer::readbackDiagnostics() {
    m_diagRecords.clear();
    void* p = nullptr;
    VK_CHECK(vkMapMemory(m_gpu.device, m_diagBuf.mem, 0, VK_WHOLE_SIZE, 0, &p));
    uint32_t count = 0;
    std::memcpy(&count, p, 4);
    count = std::min(count, kMaxDiagRecords);
    const DiagRecord* recs =
        reinterpret_cast<const DiagRecord*>((const char*)p + 16);
    m_diagRecords.assign(recs, recs + count);
    vkUnmapMemory(m_gpu.device, m_diagBuf.mem);
}

void Renderer::saveFramePPM(const std::string& path) {
    if (!m_headless) return;
    void* p = nullptr;
    VK_CHECK(vkMapMemory(m_gpu.device, m_readback.mem, 0, VK_WHOLE_SIZE, 0, &p));
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << m_width << " " << m_height << "\n255\n";
    const char* px = (const char*)p;
    for (uint32_t i = 0; i < m_width * m_height; ++i)
        f.write(px + i * 4, 3); // drop alpha (RGBA8)
    vkUnmapMemory(m_gpu.device, m_readback.mem);
}

bool Renderer::windowShouldClose() const {
    return m_window && glfwWindowShouldClose(m_window);
}

void Renderer::shutdown() {
    if (!m_gpu.device) return;
    vkDeviceWaitIdle(m_gpu.device);
    destroyBuffer(m_gpu, m_cubeVB);
    destroyBuffer(m_gpu, m_cubeIB);
    destroyBuffer(m_gpu, m_planeVB);
    destroyBuffer(m_gpu, m_planeIB);
    destroyBuffer(m_gpu, m_diagBuf);
    vkDestroyDescriptorPool(m_gpu.device, m_descPool, nullptr);
    vkDestroyPipeline(m_gpu.device, m_geomPipe, nullptr);
    vkDestroyPipeline(m_gpu.device, m_edgePipe, nullptr);
    vkDestroyPipeline(m_gpu.device, m_diagPipe, nullptr);
    vkDestroyPipeline(m_gpu.device, m_displayPipe, nullptr);
    vkDestroyPipelineLayout(m_gpu.device, m_geomPL, nullptr);
    vkDestroyPipelineLayout(m_gpu.device, m_edgePL, nullptr);
    vkDestroyPipelineLayout(m_gpu.device, m_diagPL, nullptr);
    vkDestroyPipelineLayout(m_gpu.device, m_displayPL, nullptr);
    vkDestroyDescriptorSetLayout(m_gpu.device, m_edgeDSL, nullptr);
    vkDestroyDescriptorSetLayout(m_gpu.device, m_diagDSL, nullptr);
    vkDestroyDescriptorSetLayout(m_gpu.device, m_displayDSL, nullptr);
    vkDestroyRenderPass(m_gpu.device, m_geomRP, nullptr);
    vkDestroyRenderPass(m_gpu.device, m_edgeRP, nullptr);
    vkDestroyRenderPass(m_gpu.device, m_diagRP, nullptr);
    vkDestroyRenderPass(m_gpu.device, m_displayRP, nullptr);
    vkDestroyFramebuffer(m_gpu.device, m_geomFB, nullptr);
    vkDestroyFramebuffer(m_gpu.device, m_edgeFB, nullptr);
    vkDestroyFramebuffer(m_gpu.device, m_diagFB, nullptr);
    if (m_headless) vkDestroyFramebuffer(m_gpu.device, m_displayFB, nullptr);
    vkDestroySampler(m_gpu.device, m_sampler, nullptr);
    destroyTargets();
    destroySwapchain();
    vkDestroySemaphore(m_gpu.device, m_semAcquire, nullptr);
    vkDestroySemaphore(m_gpu.device, m_semDone, nullptr);
    vkDestroyFence(m_gpu.device, m_fence, nullptr);
    vkDestroyCommandPool(m_gpu.device, m_cmdPool, nullptr);
    vkDestroyDevice(m_gpu.device, nullptr);
    if (m_surface) vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    vkDestroyInstance(m_instance, nullptr);
    m_gpu.device = VK_NULL_HANDLE;
}
