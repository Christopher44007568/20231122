#include "renderer.hpp"

#include "evidence.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace inst {
namespace {

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

struct MeshVertex {
    float position[3];
    float normal[3];
};

struct GpuInstance {
    float matrix[16];
    float color[4];
};

static_assert(sizeof(MeshVertex) == 24, "mesh vertex layout changed");
static_assert(sizeof(GpuInstance) == 80, "instance layout changed");

void appendVertex(std::vector<MeshVertex>& vertices, float x, float y,
                  float z, float nx, float ny, float nz) {
    vertices.push_back({{x, y, z}, {nx, ny, nz}});
}

void appendQuad(std::vector<MeshVertex>& vertices,
                std::vector<uint32_t>& indices,
                const std::array<float, 3>& a,
                const std::array<float, 3>& b,
                const std::array<float, 3>& c,
                const std::array<float, 3>& d,
                const std::array<float, 3>& normal) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    appendVertex(vertices, a[0], a[1], a[2],
                 normal[0], normal[1], normal[2]);
    appendVertex(vertices, b[0], b[1], b[2],
                 normal[0], normal[1], normal[2]);
    appendVertex(vertices, c[0], c[1], c[2],
                 normal[0], normal[1], normal[2]);
    appendVertex(vertices, d[0], d[1], d[2],
                 normal[0], normal[1], normal[2]);
    indices.insert(indices.end(), {base, base + 1, base + 2,
                                   base, base + 2, base + 3});
}

void buildCube(std::vector<MeshVertex>& vertices,
               std::vector<uint32_t>& indices) {
    appendQuad(vertices, indices, {-0.5f, -0.5f, 0.5f},
               {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f},
               {-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f});
    appendQuad(vertices, indices, {0.5f, -0.5f, -0.5f},
               {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f},
               {0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f});
    appendQuad(vertices, indices, {-0.5f, -0.5f, -0.5f},
               {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
               {-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f});
    appendQuad(vertices, indices, {0.5f, -0.5f, 0.5f},
               {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f},
               {0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f});
    appendQuad(vertices, indices, {-0.5f, 0.5f, 0.5f},
               {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f},
               {-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f});
    appendQuad(vertices, indices, {-0.5f, -0.5f, -0.5f},
               {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f},
               {-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f});
}

void appendTriangle(std::vector<MeshVertex>& vertices,
                    std::vector<uint32_t>& indices,
                    const std::array<float, 3>& a,
                    const std::array<float, 3>& b,
                    const std::array<float, 3>& c,
                    const std::array<float, 3>& normal) {
    const uint32_t base = static_cast<uint32_t>(vertices.size());
    appendVertex(vertices, a[0], a[1], a[2],
                 normal[0], normal[1], normal[2]);
    appendVertex(vertices, b[0], b[1], b[2],
                 normal[0], normal[1], normal[2]);
    appendVertex(vertices, c[0], c[1], c[2],
                 normal[0], normal[1], normal[2]);
    indices.insert(indices.end(), {base, base + 1, base + 2});
}

void buildPyramid(std::vector<MeshVertex>& vertices,
                  std::vector<uint32_t>& indices) {
    const std::array<float, 3> apex{0.0f, 0.6f, 0.0f};
    const std::array<std::array<float, 3>, 4> base{{
        {-0.55f, -0.45f, 0.55f}, {0.55f, -0.45f, 0.55f},
        {0.55f, -0.45f, -0.55f}, {-0.55f, -0.45f, -0.55f}}};
    for (size_t i = 0; i < base.size(); ++i) {
        const auto& p0 = base[i];
        const auto& p1 = base[(i + 1) % base.size()];
        const float ax = p1[0] - p0[0];
        const float az = p1[2] - p0[2];
        const float len = std::sqrt(ax * ax + az * az + 0.0001f);
        appendTriangle(vertices, indices, p0, p1, apex,
                       {az / len, 0.45f, -ax / len});
    }
    appendQuad(vertices, indices, base[3], base[2], base[1], base[0],
               {0.0f, -1.0f, 0.0f});
}

void buildPlane(std::vector<MeshVertex>& vertices,
                std::vector<uint32_t>& indices) {
    appendQuad(vertices, indices, {-0.65f, -0.35f, 0.0f},
               {0.65f, -0.35f, 0.0f}, {0.65f, 0.35f, 0.0f},
               {-0.65f, 0.35f, 0.0f}, {0.0f, 0.0f, 1.0f});
}

std::vector<uint32_t> readSpv(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw RenderError("missing shader " + path.string());
    const auto bytes = static_cast<size_t>(in.tellg());
    if (bytes == 0 || bytes % 4 != 0) {
        throw RenderError("invalid SPIR-V size for " + path.string());
    }
    std::vector<uint32_t> words(bytes / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(words.data()),
            static_cast<std::streamsize>(bytes));
    return words;
}

VkDeviceMemory allocateImageMemory(const VulkanContext& context,
                                   VkImage image) {
    VkMemoryRequirements requirements{};
    vkGetImageMemoryRequirements(context.device(), image, &requirements);
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = context.findMemoryType(
        requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VkDeviceMemory memory = VK_NULL_HANDLE;
    INST_VK_CHECK(vkAllocateMemory(context.device(), &allocation, nullptr,
                                  &memory));
    INST_VK_CHECK(vkBindImageMemory(context.device(), image, memory, 0));
    return memory;
}

VkImage createImage2D(const VulkanContext& context, VkFormat format,
                      VkImageUsageFlags usage) {
    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {context.width(), context.height(), 1};
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage image = VK_NULL_HANDLE;
    INST_VK_CHECK(vkCreateImage(context.device(), &info, nullptr, &image));
    return image;
}

VkImageView createImageView(const VulkanContext& context, VkImage image,
                            VkFormat format, VkImageAspectFlags aspect) {
    VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    info.image = image;
    info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    info.format = format;
    info.subresourceRange = {aspect, 0, 1, 0, 1};
    VkImageView view = VK_NULL_HANDLE;
    INST_VK_CHECK(vkCreateImageView(context.device(), &info, nullptr, &view));
    return view;
}

const char* layoutName(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
            return "TRANSFER_SRC_OPTIMAL";
        default: return "OTHER";
    }
}

} // namespace

InstanceRenderer::InstanceRenderer(VulkanContext& context,
                                   const fs::path& vertexShader,
                                   const fs::path& fragmentShader)
    : context_(context), width_(context.width()), height_(context.height()) {
    createRenderTarget();
    createMeshCatalog();
    createPipeline(vertexShader, fragmentShader);
    readbackBuffer_ = createBufferResource(
        context_, static_cast<VkDeviceSize>(width_) * height_ * 4,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

InstanceRenderer::~InstanceRenderer() {
    vkDeviceWaitIdle(context_.device());
    destroyBufferResource(context_, readbackBuffer_);
    destroyBufferResource(context_, indexBuffer_);
    destroyBufferResource(context_, meshBuffer_);
    if (pipeline_) vkDestroyPipeline(context_.device(), pipeline_, nullptr);
    if (pipelineLayout_) {
        vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
    }
    if (framebuffer_) {
        vkDestroyFramebuffer(context_.device(), framebuffer_, nullptr);
    }
    if (renderPass_) {
        vkDestroyRenderPass(context_.device(), renderPass_, nullptr);
    }
    if (colorView_) vkDestroyImageView(context_.device(), colorView_, nullptr);
    if (depthView_) vkDestroyImageView(context_.device(), depthView_, nullptr);
    if (colorImage_) vkDestroyImage(context_.device(), colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(context_.device(), colorMemory_, nullptr);
    if (depthImage_) vkDestroyImage(context_.device(), depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(context_.device(), depthMemory_, nullptr);
}

void InstanceRenderer::createRenderTarget() {
    colorImage_ = createImage2D(
        context_, kColorFormat,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
            VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    colorMemory_ = allocateImageMemory(context_, colorImage_);
    colorView_ = createImageView(context_, colorImage_, kColorFormat,
                                 VK_IMAGE_ASPECT_COLOR_BIT);

    depthImage_ = createImage2D(
        context_, kDepthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT);
    depthMemory_ = allocateImageMemory(context_, depthImage_);
    depthView_ = createImageView(context_, depthImage_, kDepthFormat,
                                 VK_IMAGE_ASPECT_DEPTH_BIT);

    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kColorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = kDepthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorReference{
        0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthReference{
        1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorReference;
    subpass.pDepthStencilAttachment = &depthReference;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                              VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 2;
    renderPassInfo.pAttachments = attachments;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    INST_VK_CHECK(vkCreateRenderPass(context_.device(), &renderPassInfo,
                                    nullptr, &renderPass_));

    const VkImageView views[2] = {colorView_, depthView_};
    VkFramebufferCreateInfo framebufferInfo{
        VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    framebufferInfo.renderPass = renderPass_;
    framebufferInfo.attachmentCount = 2;
    framebufferInfo.pAttachments = views;
    framebufferInfo.width = width_;
    framebufferInfo.height = height_;
    framebufferInfo.layers = 1;
    INST_VK_CHECK(vkCreateFramebuffer(context_.device(), &framebufferInfo,
                                     nullptr, &framebuffer_));
}

void InstanceRenderer::createMeshCatalog() {
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    auto appendMesh = [&](void (*builder)(std::vector<MeshVertex>&,
                                          std::vector<uint32_t>&),
                          uint32_t model) {
        MeshRange range;
        range.firstVertex = static_cast<uint32_t>(vertices.size());
        range.firstIndex = static_cast<uint32_t>(indices.size());
        builder(vertices, indices);
        range.vertexCount =
            static_cast<uint32_t>(vertices.size()) - range.firstVertex;
        range.indexCount =
            static_cast<uint32_t>(indices.size()) - range.firstIndex;
        meshes_[model] = range;
    };
    appendMesh(buildCube, 0);
    appendMesh(buildPyramid, 1);
    appendMesh(buildPlane, 2);

    meshBuffer_ = createBufferResource(
        context_, vertices.size() * sizeof(MeshVertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    indexBuffer_ = createBufferResource(
        context_, indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    uploadBuffer(context_, meshBuffer_, vertices.data(),
                 static_cast<VkDeviceSize>(vertices.size() *
                                           sizeof(MeshVertex)));
    uploadBuffer(context_, indexBuffer_, indices.data(),
                 static_cast<VkDeviceSize>(indices.size() *
                                           sizeof(uint32_t)));
}

void InstanceRenderer::createPipeline(const fs::path& vertexShader,
                                      const fs::path& fragmentShader) {
    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    push.size = sizeof(float) * 16;
    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    INST_VK_CHECK(vkCreatePipelineLayout(context_.device(), &layoutInfo,
                                        nullptr, &pipelineLayout_));

    const std::vector<uint32_t> vertexWords = readSpv(vertexShader);
    const std::vector<uint32_t> fragmentWords = readSpv(fragmentShader);
    VkShaderModule vertexModule =
        context_.loadShader(vertexWords.data(), vertexWords.size());
    VkShaderModule fragmentModule =
        context_.loadShader(fragmentWords.data(), fragmentWords.size());

    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertexModule;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragmentModule;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].stride = sizeof(MeshVertex);
    bindings[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    bindings[1].binding = 1;
    bindings[1].stride = sizeof(GpuInstance);
    bindings[1].inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
    VkVertexInputAttributeDescription attributes[7]{};
    attributes[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     offsetof(MeshVertex, position)};
    attributes[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                     offsetof(MeshVertex, normal)};
    for (uint32_t column = 0; column < 4; ++column) {
        attributes[2 + column] = {
            2 + column, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
            static_cast<uint32_t>(offsetof(GpuInstance, matrix) +
                                  column * sizeof(float) * 4)};
    }
    attributes[6] = {6, 1, VK_FORMAT_R32G32B32A32_SFLOAT,
                     offsetof(GpuInstance, color)};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 2;
    vertexInput.pVertexBindingDescriptions = bindings;
    vertexInput.vertexAttributeDescriptionCount = 7;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0.0f, 0.0f, static_cast<float>(width_),
                        static_cast<float>(height_), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, {width_, height_}};
    VkPipelineViewportStateCreateInfo viewportState{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo raster{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;
    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                           VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    VkGraphicsPipelineCreateInfo pipelineInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = stages;
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &assembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pDepthStencilState = &depth;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.layout = pipelineLayout_;
    pipelineInfo.renderPass = renderPass_;
    pipelineInfo.subpass = 0;
    INST_VK_CHECK(vkCreateGraphicsPipelines(
        context_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
        &pipeline_));
    vkDestroyShaderModule(context_.device(), vertexModule, nullptr);
    vkDestroyShaderModule(context_.device(), fragmentModule, nullptr);
}

void InstanceRenderer::transitionColor(VkCommandBuffer commandBuffer,
                                       VkImageLayout newLayout,
                                       const std::string& resource) {
    if (colorLayout_ == newLayout) return;
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = colorLayout_;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = colorImage_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkPipelineStageFlags sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags destinationStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    if (colorLayout_ == VK_IMAGE_LAYOUT_UNDEFINED) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    } else if (newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        sourceStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    vkCmdPipelineBarrier(commandBuffer, sourceStage, destinationStage, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    transitions_.push_back({"colorImage/" + resource,
                            layoutName(colorLayout_),
                            layoutName(newLayout),
                            sourceStage == VK_PIPELINE_STAGE_TRANSFER_BIT
                                ? "TRANSFER" : "COLOR_ATTACHMENT_OUTPUT",
                            newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL
                                ? "TRANSFER_READ" : "COLOR_ATTACHMENT_WRITE"});
    colorLayout_ = newLayout;
}

void InstanceRenderer::transitionDepth(VkCommandBuffer commandBuffer) {
    if (depthLayout_ == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        return;
    }
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = depthLayout_;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage_;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
                         0, nullptr, 0, nullptr, 1, &barrier);
    transitions_.push_back({"depthImage", layoutName(depthLayout_),
                            layoutName(barrier.newLayout), "TOP_OF_PIPE",
                            "DEPTH_STENCIL_ATTACHMENT_WRITE"});
    depthLayout_ = barrier.newLayout;
}

RenderSnapshot InstanceRenderer::render(const SelectionResult& selection,
                                        const std::string& label) {
    transitions_.clear();
    RenderSnapshot snapshot;
    snapshot.label = label;
    snapshot.policy = selection.policyName;
    snapshot.order = selection.orderName;
    snapshot.selectionHash = selection.selectionHash;
    snapshot.drawOrderHash = selection.drawOrderHash;
    snapshot.deviceName = context_.deviceName();
    snapshot.width = width_;
    snapshot.height = height_;
    snapshot.instances = selection.selected;

    std::vector<GpuInstance> gpuInstances;
    std::vector<uint32_t> groupStart(3, 0);
    std::vector<uint32_t> groupCount(3, 0);
    for (uint32_t model = 0; model < 3; ++model) {
        groupStart[model] = static_cast<uint32_t>(gpuInstances.size());
        for (const SelectedInstance& item : selection.selected) {
            if (item.model % 3u != model) continue;
            GpuInstance gpu{};
            const float cosine = std::cos(item.yaw);
            const float sine = std::sin(item.yaw);
            gpu.matrix[0] = item.scale * cosine;
            gpu.matrix[2] = -item.scale * sine;
            gpu.matrix[5] = item.scale;
            gpu.matrix[8] = item.scale * sine;
            gpu.matrix[10] = item.scale * cosine;
            gpu.matrix[12] = item.x;
            gpu.matrix[13] = item.y;
            gpu.matrix[14] = item.z;
            gpu.matrix[15] = 1.0f;
            gpu.color[0] = item.r;
            gpu.color[1] = item.g;
            gpu.color[2] = item.b;
            gpu.color[3] = 1.0f;
            gpuInstances.push_back(gpu);
        }
        groupCount[model] =
            static_cast<uint32_t>(gpuInstances.size()) - groupStart[model];
    }

    VkBufferResource instanceBuffer = createBufferResource(
        context_,
        static_cast<VkDeviceSize>(std::max<size_t>(1, gpuInstances.size()) *
                                  sizeof(GpuInstance)),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
            VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (!gpuInstances.empty()) {
        uploadBuffer(context_, instanceBuffer, gpuInstances.data(),
                     static_cast<VkDeviceSize>(gpuInstances.size() *
                                               sizeof(GpuInstance)));
    }

    VkCommandBuffer commandBuffer = context_.beginOneShot();
    transitionColor(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    label);
    transitionDepth(commandBuffer);

    VkClearValue clears[2]{};
    clears[0].color = {{0.035f, 0.045f, 0.07f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    begin.renderPass = renderPass_;
    begin.framebuffer = framebuffer_;
    begin.renderArea.extent = {width_, height_};
    begin.clearValueCount = 2;
    begin.pClearValues = clears;
    vkCmdBeginRenderPass(commandBuffer, &begin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      pipeline_);

    const float viewProjection[16] = {
        0.625f, 0.0f, 0.0f, 0.0f,
        0.0f, -1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f};
    vkCmdPushConstants(commandBuffer, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(viewProjection),
                       viewProjection);

    uint32_t drawIndex = 0;
    const VkDeviceSize instanceStride = sizeof(GpuInstance);
    for (uint32_t model = 0; model < 3; ++model) {
        if (groupCount[model] == 0) continue;
        const MeshRange& mesh = meshes_[model];
        const VkDeviceSize vertexOffset = 0;
        const VkDeviceSize indexOffset =
            static_cast<VkDeviceSize>(mesh.firstIndex) * sizeof(uint32_t);
        const VkDeviceSize instanceOffset =
            static_cast<VkDeviceSize>(groupStart[model]) * instanceStride;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &meshBuffer_.buffer,
                               &vertexOffset);
        vkCmdBindVertexBuffers(commandBuffer, 1, 1, &instanceBuffer.buffer,
                               &instanceOffset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer_.buffer, indexOffset,
                             VK_INDEX_TYPE_UINT32);
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, groupCount[model],
                         0, 0, 0);
        snapshot.draws.push_back({
            drawIndex++, model, modelName(model), mesh.indexCount,
            groupCount[model], mesh.firstIndex, 0,
            "meshCatalog:" + std::to_string(mesh.firstVertex) + ":" +
                std::to_string(mesh.vertexCount),
            "indexCatalog:" + std::to_string(mesh.firstIndex),
            "instanceBuffer:offset=" +
                std::to_string(instanceOffset)});
    }
    vkCmdEndRenderPass(commandBuffer);

    transitionColor(commandBuffer, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    label);
    VkBufferImageCopy copy{};
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copy.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(commandBuffer,
                           colorImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readbackBuffer_.buffer, 1, &copy);
    transitionColor(commandBuffer, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    label);
    context_.endAndWait(commandBuffer);
    destroyBufferResource(context_, instanceBuffer);

    snapshot.transitions = transitions_;
    snapshot.rgba.resize(static_cast<size_t>(width_) * height_ * 4);
    void* mapped = nullptr;
    INST_VK_CHECK(vkMapMemory(context_.device(), readbackBuffer_.memory, 0,
                             readbackBuffer_.size, 0, &mapped));
    std::memcpy(snapshot.rgba.data(), mapped, snapshot.rgba.size());
    vkUnmapMemory(context_.device(), readbackBuffer_.memory);
    snapshot.imageHash = imageHash(snapshot.rgba);
    return snapshot;
}

} // namespace inst


