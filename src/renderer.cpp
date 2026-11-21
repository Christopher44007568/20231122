#include "renderer.hpp"

#include <cstring>
#include <sstream>

namespace skr {
namespace {

constexpr VkFormat kColorFormat = VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

std::string toHex(VkFlags v) {
    std::ostringstream os;
    os << "0x" << std::hex << v;
    return os.str();
}

const char* layoutName(VkImageLayout layout) {
    switch (layout) {
        case VK_IMAGE_LAYOUT_UNDEFINED: return "UNDEFINED";
        case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
            return "COLOR_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
            return "DEPTH_STENCIL_ATTACHMENT_OPTIMAL";
        case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL: return "TRANSFER_SRC_OPTIMAL";
        case VK_IMAGE_LAYOUT_PRESENT_SRC_KHR: return "PRESENT_SRC_KHR";
        default: return "OTHER";
    }
}

} // namespace

SkinningRenderer::GpuBuffer SkinningRenderer::createBuffer(
        VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props) {
    GpuBuffer b;
    b.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(ctx_.device(), &bi, nullptr, &b.buffer));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(ctx_.device(), b.buffer, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = ctx_.findMemoryType(req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(ctx_.device(), &ai, nullptr, &b.memory));
    VK_CHECK(vkBindBufferMemory(ctx_.device(), b.buffer, b.memory, 0));
    return b;
}

void SkinningRenderer::destroyBuffer(GpuBuffer& b) {
    if (b.buffer) vkDestroyBuffer(ctx_.device(), b.buffer, nullptr);
    if (b.memory) vkFreeMemory(ctx_.device(), b.memory, nullptr);
    b = {};
}

void SkinningRenderer::transitionImage(VkImage image, VkImageLayout oldLayout,
                                       VkImageLayout newLayout,
                                       VkCommandBuffer cmd,
                                       const std::string& resourceName,
                                       VkImageAspectFlags aspect) {
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};

    VkPipelineStageFlags srcStage = 0, dstStage = 0;
    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && aspect == VK_IMAGE_ASPECT_DEPTH_BIT &&
        newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL &&
               newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
        barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        barrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr,
                         1, &barrier);

    ResourceStateEvent event{resourceName, toHex(srcStage), toHex(dstStage),
                             layoutName(oldLayout), layoutName(newLayout)};
    pendingTransitions_.push_back(event);
}

SkinningRenderer::SkinningRenderer(VulkanContext& ctx, const uint32_t* vertSpv,
                                   size_t vertSpvWords, const uint32_t* fragSpv,
                                   size_t fragSpvWords)
        : ctx_(ctx), width_(ctx.width()), height_(ctx.height()) {
    createRenderTarget();
    createPipeline(vertSpv, vertSpvWords, fragSpv, fragSpvWords);

    const VkDeviceSize readbackSize =
        static_cast<VkDeviceSize>(width_) * height_ * 4;
    stagingReadback_ = createBuffer(
        readbackSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
}

SkinningRenderer::~SkinningRenderer() {
    VkDevice dev = ctx_.device();
    vkDeviceWaitIdle(dev);
    destroyBuffer(stagingReadback_);
    if (pipeline_) vkDestroyPipeline(dev, pipeline_, nullptr);
    if (pipelineLayout_) vkDestroyPipelineLayout(dev, pipelineLayout_, nullptr);
    if (framebuffer_) vkDestroyFramebuffer(dev, framebuffer_, nullptr);
    if (renderPass_) vkDestroyRenderPass(dev, renderPass_, nullptr);
    if (colorView_) vkDestroyImageView(dev, colorView_, nullptr);
    if (depthView_) vkDestroyImageView(dev, depthView_, nullptr);
    if (colorImage_) vkDestroyImage(dev, colorImage_, nullptr);
    if (colorMemory_) vkFreeMemory(dev, colorMemory_, nullptr);
    if (depthImage_) vkDestroyImage(dev, depthImage_, nullptr);
    if (depthMemory_) vkFreeMemory(dev, depthMemory_, nullptr);
    if (descriptorPool_) vkDestroyDescriptorPool(dev, descriptorPool_, nullptr);
    if (setLayout_) vkDestroyDescriptorSetLayout(dev, setLayout_, nullptr);
}

static VkDeviceMemory allocForImage(VkDevice dev, VkPhysicalDevice phys,
                                    VkImage image, uint32_t& outTypeIndex) {
    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(dev, image, &req);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    uint32_t typeIndex = 0;
    bool found = false;
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
            typeIndex = i;
            found = true;
            break;
        }
    }
    if (!found) throw std::runtime_error("no device-local memory for image");
    outTypeIndex = typeIndex;
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = typeIndex;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &memory));
    VK_CHECK(vkBindImageMemory(dev, image, memory, 0));
    return memory;
}

void SkinningRenderer::createRenderTarget() {
    VkDevice dev = ctx_.device();

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = kColorFormat;
    imageInfo.extent = {width_, height_, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(dev, &imageInfo, nullptr, &colorImage_));
    uint32_t unusedType = 0;
    colorMemory_ = allocForImage(dev, ctx_.physicalDevice(), colorImage_, unusedType);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = colorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = kColorFormat;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(dev, &viewInfo, nullptr, &colorView_));

    VkImageCreateInfo depthInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    depthInfo.imageType = VK_IMAGE_TYPE_2D;
    depthInfo.format = kDepthFormat;
    depthInfo.extent = {width_, height_, 1};
    depthInfo.mipLevels = 1;
    depthInfo.arrayLayers = 1;
    depthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(dev, &depthInfo, nullptr, &depthImage_));
    depthMemory_ = allocForImage(dev, ctx_.physicalDevice(), depthImage_, unusedType);

    VkImageViewCreateInfo depthView{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    depthView.image = depthImage_;
    depthView.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthView.format = kDepthFormat;
    depthView.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(dev, &depthView, nullptr, &depthView_));

    VkAttachmentDescription attachments[2]{};
    attachments[0].format = kColorFormat;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachments[1].format = kDepthFormat;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    attachments[1].finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorRef{0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depthRef{1,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    subpass.pDepthStencilAttachment = &depthRef;

    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rpInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpInfo.attachmentCount = 2;
    rpInfo.pAttachments = attachments;
    rpInfo.subpassCount = 1;
    rpInfo.pSubpasses = &subpass;
    rpInfo.dependencyCount = 1;
    rpInfo.pDependencies = &dep;
    VK_CHECK(vkCreateRenderPass(dev, &rpInfo, nullptr, &renderPass_));

    VkImageView views[2] = {colorView_, depthView_};
    VkFramebufferCreateInfo fbInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fbInfo.renderPass = renderPass_;
    fbInfo.attachmentCount = 2;
    fbInfo.pAttachments = views;
    fbInfo.width = width_;
    fbInfo.height = height_;
    fbInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(dev, &fbInfo, nullptr, &framebuffer_));
}

void SkinningRenderer::createPipeline(const uint32_t* vertSpv,
                                      size_t vertSpvWords,
                                      const uint32_t* fragSpv,
                                      size_t fragSpvWords) {
    VkDevice dev = ctx_.device();

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    VkDescriptorSetLayoutCreateInfo setInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    setInfo.bindingCount = 1;
    setInfo.pBindings = &binding;
    VK_CHECK(vkCreateDescriptorSetLayout(dev, &setInfo, nullptr, &setLayout_));

    VkPushConstantRange push{};
    push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    push.offset = 0;
    push.size = sizeof(float) * 16; // one mat4 view-projection

    VkPipelineLayoutCreateInfo layoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout_;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &push;
    VK_CHECK(vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &pipelineLayout_));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4};
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 4;
    VK_CHECK(vkCreateDescriptorPool(dev, &poolInfo, nullptr, &descriptorPool_));

    VkShaderModule vert = ctx_.loadShader(vertSpv, vertSpvWords);
    VkShaderModule frag = ctx_.loadShader(fragSpv, fragSpvWords);
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkVertexInputBindingDescription bindDesc{};
    bindDesc.binding = 0;
    bindDesc.stride = sizeof(Vertex);
    bindDesc.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    VkVertexInputAttributeDescription attrs[5]{};
    attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)};
    attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)};
    attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)};
    attrs[3] = {3, 0, VK_FORMAT_R32G32B32A32_UINT, offsetof(Vertex, boneIndex)};
    attrs[4] = {4, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Vertex, boneWeight)};

    VkPipelineVertexInputStateCreateInfo vertexInput{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindDesc;
    vertexInput.vertexAttributeDescriptionCount = 5;
    vertexInput.pVertexAttributeDescriptions = attrs;

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

    VkPipelineMultisampleStateCreateInfo msaa{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    depth.depthTestEnable = VK_TRUE;
    depth.depthWriteEnable = VK_TRUE;
    depth.depthCompareOp = VK_COMPARE_OP_LESS;

    VkPipelineColorBlendAttachmentState blend{};
    blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &blend;

    VkGraphicsPipelineCreateInfo pipeInfo{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipeInfo.stageCount = 2;
    pipeInfo.pStages = stages;
    pipeInfo.pVertexInputState = &vertexInput;
    pipeInfo.pInputAssemblyState = &assembly;
    pipeInfo.pViewportState = &viewportState;
    pipeInfo.pRasterizationState = &raster;
    pipeInfo.pMultisampleState = &msaa;
    pipeInfo.pDepthStencilState = &depth;
    pipeInfo.pColorBlendState = &colorBlend;
    pipeInfo.layout = pipelineLayout_;
    pipeInfo.renderPass = renderPass_;
    pipeInfo.subpass = 0;
    VK_CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &pipeInfo,
                                       nullptr, &pipeline_));
    vkDestroyShaderModule(dev, vert, nullptr);
    vkDestroyShaderModule(dev, frag, nullptr);
}

static void uploadViaStaging(VkDevice dev, VkPhysicalDevice phys,
                             VkCommandPool pool, VkQueue queue,
                             VkBuffer dst, VkDeviceSize size, const void* src) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer staging = VK_NULL_HANDLE;
    VK_CHECK(vkCreateBuffer(dev, &bi, nullptr, &staging));
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(dev, staging, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(phys, &mem);
    for (uint32_t i = 0; i < mem.memoryTypeCount; ++i) {
        if ((req.memoryTypeBits & (1u << i)) &&
            (mem.memoryTypes[i].propertyFlags &
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            ai.memoryTypeIndex = i;
            break;
        }
    }
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateMemory(dev, &ai, nullptr, &stagingMem));
    VK_CHECK(vkBindBufferMemory(dev, staging, stagingMem, 0));
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(dev, stagingMem, 0, size, 0, &mapped));
    std::memcpy(mapped, src, static_cast<size_t>(size));
    vkUnmapMemory(dev, stagingMem);

    VkCommandBufferAllocateInfo cai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(dev, &cai, &cmd));
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &begin));
    VkBufferCopy region{0, 0, size};
    vkCmdCopyBuffer(cmd, staging, dst, 1, &region);
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(dev, pool, 1, &cmd);
    vkDestroyBuffer(dev, staging, nullptr);
    vkFreeMemory(dev, stagingMem, nullptr);
}

Snapshot SkinningRenderer::render(const SkinnedMesh& mesh,
                                  const std::string& label,
                                  uint64_t frameId) {
    pendingTransitions_.clear();
    VkDevice dev = ctx_.device();
    const VkDeviceSize vertexBytes =
        static_cast<VkDeviceSize>(mesh.vertices.size() * sizeof(Vertex));
    const VkDeviceSize indexBytes =
        static_cast<VkDeviceSize>(mesh.indices.size() * sizeof(uint32_t));
    const std::vector<Mat4> skin = computeSkinningMatrices(mesh);
    const VkDeviceSize uboBytes =
        static_cast<VkDeviceSize>(skin.size() * sizeof(Mat4));

    GpuBuffer vbo = createBuffer(
        vertexBytes,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    GpuBuffer ibo = createBuffer(
        indexBytes,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    GpuBuffer ubo = createBuffer(
        uboBytes,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    uploadViaStaging(dev, ctx_.physicalDevice(), ctx_.commandPool(),
                     ctx_.queue(), vbo.buffer, vertexBytes,
                     mesh.vertices.data());
    uploadViaStaging(dev, ctx_.physicalDevice(), ctx_.commandPool(),
                     ctx_.queue(), ibo.buffer, indexBytes,
                     mesh.indices.data());
    uploadViaStaging(dev, ctx_.physicalDevice(), ctx_.commandPool(),
                     ctx_.queue(), ubo.buffer, uboBytes,
                     skin.data());

    VkDescriptorSetAllocateInfo dsai{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dsai.descriptorPool = descriptorPool_;
    dsai.descriptorSetCount = 1;
    dsai.pSetLayouts = &setLayout_;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateDescriptorSets(dev, &dsai, &set));
    VkDescriptorBufferInfo uboInfo{ubo.buffer, 0, uboBytes};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &uboInfo;
    vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    Snapshot snapshot;
    snapshot.label = label;
    snapshot.revision = mesh.revision;
    snapshot.objectName = mesh.objectName;
    snapshot.deviceName = ctx_.deviceName();
    snapshot.frameId = frameId;
    snapshot.width = width_;
    snapshot.height = height_;
    snapshot.framebuffer = {
        "offscreen/" + label, width_, height_, kColorFormat, kDepthFormat,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, {}};

    VkCommandBuffer cmd = ctx_.beginOneShot();

    // Explicit resource-state transitions with recorded evidence.
    transitionImage(colorImage_, VK_IMAGE_LAYOUT_UNDEFINED,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, cmd,
                    "colorImage/" + label, VK_IMAGE_ASPECT_COLOR_BIT);
    {
        VkImageMemoryBarrier depthBarrier{
            VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        depthBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthBarrier.newLayout =
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        depthBarrier.image = depthImage_;
        depthBarrier.subresourceRange = {VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};
        depthBarrier.dstAccessMask =
            VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        vkCmdPipelineBarrier(
            cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0, 0, nullptr,
            0, nullptr, 1, &depthBarrier);
        pendingTransitions_.push_back(
            {"depthImage/" + label, "TOP_OF_PIPE", "EARLY_FRAGMENT_TESTS",
             "UNDEFINED", "DEPTH_STENCIL_ATTACHMENT_OPTIMAL"});
    }

    VkClearValue clears[2]{};
    clears[0].color = {{0.06f, 0.07f, 0.09f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo rpBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rpBegin.renderPass = renderPass_;
    rpBegin.framebuffer = framebuffer_;
    rpBegin.renderArea.extent = {width_, height_};
    rpBegin.clearValueCount = 2;
    rpBegin.pClearValues = clears;
    vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkDeviceSize vertexOffset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vbo.buffer, &vertexOffset);
    vkCmdBindIndexBuffer(cmd, ibo.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout_, 0, 1, &set, 0, nullptr);

    // Orthographic camera looking at the skinned mesh (object roughly in
    // [-1, 1]^3). Column-major, matching the push-constant mat4 convention.
    const float viewProj[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 0.5f, 0.0f,
        0.0f, 0.0f, 0.5f, 1.0f};
    vkCmdPushConstants(cmd, pipelineLayout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(viewProj), viewProj);

    const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size());
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
    vkCmdEndRenderPass(cmd);

    transitionImage(colorImage_, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, cmd,
                    "colorImage/" + label, VK_IMAGE_ASPECT_COLOR_BIT);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width_, height_, 1};
    vkCmdCopyImageToBuffer(cmd, colorImage_,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           stagingReadback_.buffer, 1, &region);

    transitionImage(colorImage_, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, cmd,
                    "colorImage/" + label, VK_IMAGE_ASPECT_COLOR_BIT);

    ctx_.endAndWait(cmd);

    snapshot.allTransitions = pendingTransitions_;
    snapshot.framebuffer.transitions = pendingTransitions_;

    snapshot.drawCall.index = 0;
    snapshot.drawCall.vertexCount =
        static_cast<uint32_t>(mesh.vertices.size());
    snapshot.drawCall.instanceCount = 1;
    snapshot.drawCall.firstVertex = 0;
    snapshot.drawCall.firstInstance = 0;
    snapshot.drawCall.pipelineName = "gpu_skinning_pipeline";
    snapshot.drawCall.framebufferName = snapshot.framebuffer.name;
    snapshot.drawCall.vertexBindings = {
        "vertexBuffer:0:stride=" + std::to_string(sizeof(Vertex))};
    snapshot.drawCall.indexBuffer = "indexBuffer:0:UINT32";
    snapshot.drawCall.skinMatrixCount = static_cast<uint32_t>(skin.size());

    const VkDeviceSize pixelBytes =
        static_cast<VkDeviceSize>(width_) * height_ * 4;
    snapshot.pixels.resize(static_cast<size_t>(pixelBytes));
    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(dev, stagingReadback_.memory, 0, pixelBytes, 0,
                         &mapped));
    std::memcpy(snapshot.pixels.data(), mapped,
                static_cast<size_t>(pixelBytes));
    vkUnmapMemory(dev, stagingReadback_.memory);

    vkResetDescriptorPool(dev, descriptorPool_, 0);
    destroyBuffer(ubo);
    destroyBuffer(ibo);
    destroyBuffer(vbo);
    return snapshot;
}

} // namespace skr
