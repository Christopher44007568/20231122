#include "render/renderer.h"

#include "core/sha256.h"
#include "generated/shaders_spv.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <sstream>

namespace ssor {
namespace {

struct GbufferPushConstants {
    Mat4 modelView;
    Mat4 proj;
};
static_assert(sizeof(GbufferPushConstants) == 128, "push constant size");

struct EdgePushConstants {
    float texel[2];
    float depthThreshold;
    float normalThreshold;
    float gtDepthEps;
    float gtNormalEps;
    std::uint32_t width;
    std::uint32_t height;
};
static_assert(sizeof(EdgePushConstants) == 32, "edge push constant size");

TimestampPoint nowPoint() {
    return nowTimestamp();
}

std::string clearString(const VkClearValue& clear, bool depth) {
    std::ostringstream out;
    if (depth) {
        out << "depth=" << clear.depthStencil.depth
            << ",stencil=" << clear.depthStencil.stencil;
    } else {
        out << clear.color.float32[0] << "," << clear.color.float32[1] << ","
            << clear.color.float32[2] << "," << clear.color.float32[3];
    }
    return out.str();
}

std::string meshHash(const MeshData& mesh) {
    std::string blob;
    blob.resize(mesh.vertices.size() * sizeof(MeshVertex) +
                mesh.indices.size() * sizeof(std::uint32_t));
    std::size_t offset = 0;
    if (!mesh.vertices.empty()) {
        std::memcpy(&blob[offset], mesh.vertices.data(),
                    mesh.vertices.size() * sizeof(MeshVertex));
        offset += mesh.vertices.size() * sizeof(MeshVertex);
    }
    if (!mesh.indices.empty()) {
        std::memcpy(&blob[offset], mesh.indices.data(),
                    mesh.indices.size() * sizeof(std::uint32_t));
    }
    return sha256Hex(blob);
}

std::uint8_t byteFromFloat(float value) {
    const float scaled = std::clamp(value, 0.0f, 1.0f) * 255.0f;
    return static_cast<std::uint8_t>(scaled + 0.5f);
}

ImageRgba8 metricsImageFromFloats(const std::vector<float>& values,
                                  std::uint32_t width, std::uint32_t height,
                                  float depthThreshold, float normalThreshold) {
    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    const float depthScale = std::max(depthThreshold * 3.0f, 0.02f);
    const float normalScale = std::max(normalThreshold * 2.0f, 0.05f);
    for (std::size_t i = 0, p = 0; i < values.size(); i += 4, p += 4) {
        const float depth = values[i + 0];
        const float normal = values[i + 1];
        const float boundary = values[i + 2];
        const float mask = values[i + 3];
        image.pixels[p + 0] = byteFromFloat(depth / depthScale);
        image.pixels[p + 1] = byteFromFloat(normal / normalScale);
        image.pixels[p + 2] = boundary > 0.5f ? 255 : 0;
        image.pixels[p + 3] = 255;
        if (mask > 0.5f) {
            image.pixels[p + 0] = 255;
            image.pixels[p + 1] = std::max<std::uint8_t>(image.pixels[p + 1], 40);
        }
    }
    return image;
}

ImageRgba8 gbufferImageFromFloats(const std::vector<std::uint8_t>& bytes,
                                  std::uint32_t width, std::uint32_t height) {
    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    const auto* values = reinterpret_cast<const float*>(bytes.data());
    const std::size_t count = static_cast<std::size_t>(width) * height;
    for (std::size_t i = 0, p = 0; i < count; ++i, p += 4) {
        const float nx = values[i * 4 + 0];
        const float ny = values[i * 4 + 1];
        const float nz = values[i * 4 + 2];
        const float depth = values[i * 4 + 3];
        if (depth <= 1e-4f) {
            image.pixels[p + 0] = 10;
            image.pixels[p + 1] = 12;
            image.pixels[p + 2] = 18;
        } else {
            const float shade = std::clamp(0.35f + 0.65f / std::max(depth, 0.3f),
                                           0.0f, 1.0f);
            image.pixels[p + 0] = byteFromFloat(nx * 0.5f + 0.5f);
            image.pixels[p + 1] = byteFromFloat(ny * 0.5f + 0.5f);
            image.pixels[p + 2] = byteFromFloat(nz * 0.5f + 0.5f);
            image.pixels[p + 0] =
                static_cast<std::uint8_t>(image.pixels[p + 0] * shade);
            image.pixels[p + 1] =
                static_cast<std::uint8_t>(image.pixels[p + 1] * shade);
            image.pixels[p + 2] =
                static_cast<std::uint8_t>(image.pixels[p + 2] * shade);
        }
        image.pixels[p + 3] = 255;
    }
    return image;
}

ImageRgba8 maskImageFromBytes(const std::vector<std::uint8_t>& bytes,
                              std::uint32_t width, std::uint32_t height) {
    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels.resize(static_cast<std::size_t>(width) * height * 4);
    for (std::size_t i = 0, p = 0; i < bytes.size(); ++i, p += 4) {
        const std::uint8_t v = bytes[i] ? 255 : 0;
        image.pixels[p + 0] = v;
        image.pixels[p + 1] = v;
        image.pixels[p + 2] = v;
        image.pixels[p + 3] = 255;
    }
    return image;
}

ImageRgba8 rgbaImageFromBytes(const std::vector<std::uint8_t>& bytes,
                              std::uint32_t width, std::uint32_t height) {
    ImageRgba8 image;
    image.width = width;
    image.height = height;
    image.pixels = bytes;
    return image;
}

}  // namespace

bool Renderer::init(const RawMaterial& material, const SceneBuild& scenes,
                    std::string* error) {
    if (!initGpuContext(m_gpu, error)) return false;
    m_width = material.width;
    m_height = material.height;
    m_outlineDepth = material.outlineDepth;
    m_normalThreshold = material.normalThreshold;
    m_gtDepthEps = material.gtDepthEps;
    m_gtNormalEps = material.gtNormalEps;
    m_projection = scenes.cameraMatrices.proj;
    m_seedBytes.assign(static_cast<std::size_t>(m_width) * m_height, 0);
    for (const BoundaryProbe& probe : material.boundaryPixels) {
        if (probe.expected == ProbeKind::Boundary && probe.x < m_width &&
            probe.y < m_height) {
            m_seedBytes[static_cast<std::size_t>(probe.y) * m_width + probe.x] = 1;
        }
    }
    if (!createBuffer(m_gpu,
                      static_cast<VkDeviceSize>(m_width) * m_height * 4,
                      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      m_seedBuffer, error)) {
        return false;
    }
    auto* seeds = static_cast<std::uint32_t*>(m_seedBuffer.mapped);
    for (std::size_t i = 0; i < m_seedBytes.size(); ++i) {
        seeds[i] = m_seedBytes[i] ? 1u : 0u;
    }
    if (!uploadMeshes(scenes, error)) return false;
    if (!createPipelines(error)) return false;
    return true;
}

void Renderer::shutdown() {
    destroyTargets();
    if (m_gpu.device != VK_NULL_HANDLE) {
        if (m_gbufferPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_gpu.device, m_gbufferPipeline, nullptr);
        if (m_edgePipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_gpu.device, m_edgePipeline, nullptr);
        if (m_finalPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(m_gpu.device, m_finalPipeline, nullptr);
        if (m_gbufferLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_gpu.device, m_gbufferLayout, nullptr);
        if (m_edgeLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_gpu.device, m_edgeLayout, nullptr);
        if (m_finalLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(m_gpu.device, m_finalLayout, nullptr);
        if (m_edgeSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_gpu.device, m_edgeSetLayout, nullptr);
        if (m_finalSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(m_gpu.device, m_finalSetLayout, nullptr);
        if (m_descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(m_gpu.device, m_descriptorPool, nullptr);
        if (m_sampler != VK_NULL_HANDLE)
            vkDestroySampler(m_gpu.device, m_sampler, nullptr);
        if (m_gbufferPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(m_gpu.device, m_gbufferPass, nullptr);
        if (m_edgePass != VK_NULL_HANDLE)
            vkDestroyRenderPass(m_gpu.device, m_edgePass, nullptr);
        if (m_finalPass != VK_NULL_HANDLE)
            vkDestroyRenderPass(m_gpu.device, m_finalPass, nullptr);
        destroyBuffer(m_gpu, m_cubeVB);
        destroyBuffer(m_gpu, m_cubeIB);
        destroyBuffer(m_gpu, m_groundVB);
        destroyBuffer(m_gpu, m_groundIB);
        destroyBuffer(m_gpu, m_seedBuffer);
    }
    shutdownGpuContext(m_gpu);
}

std::uint32_t Renderer::seedPixelCount() const {
    return static_cast<std::uint32_t>(
        std::count_if(m_seedBytes.begin(), m_seedBytes.end(),
                      [](std::uint8_t value) { return value != 0; }));
}

bool Renderer::uploadMeshes(const SceneBuild& scenes, std::string* error) {
    auto upload = [&](const MeshData& mesh, GpuBuffer& vertexBuffer,
                      GpuBuffer& indexBuffer, MeshUploadInfo& info) -> bool {
        const VkDeviceSize vertexBytes =
            mesh.vertices.size() * sizeof(MeshVertex);
        const VkDeviceSize indexBytes =
            mesh.indices.size() * sizeof(std::uint32_t);
        if (!createBuffer(m_gpu, vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          vertexBuffer, error)) {
            return false;
        }
        if (!createBuffer(m_gpu, indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                          indexBuffer, error)) {
            return false;
        }
        std::memcpy(vertexBuffer.mapped, mesh.vertices.data(),
                    static_cast<std::size_t>(vertexBytes));
        std::memcpy(indexBuffer.mapped, mesh.indices.data(),
                    static_cast<std::size_t>(indexBytes));
        info.vertexBufferId = "VB_" + mesh.name;
        info.indexBufferId = "IB_" + mesh.name;
        info.meshSha256 = meshHash(mesh);
        info.indexCount = static_cast<std::uint32_t>(mesh.indices.size());
        return true;
    };
    return upload(scenes.cube, m_cubeVB, m_cubeIB, m_cubeInfo) &&
           upload(scenes.ground, m_groundVB, m_groundIB, m_groundInfo);
}

bool Renderer::createPipelines(std::string* error) {
    auto colorAttachment = [](VkFormat format, VkImageLayout initial,
                              VkImageLayout finalLayout) {
        VkAttachmentDescription attachment{};
        attachment.format = format;
        attachment.samples = VK_SAMPLE_COUNT_1_BIT;
        attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachment.initialLayout = initial;
        attachment.finalLayout = finalLayout;
        return attachment;
    };

    {
        VkAttachmentDescription attachments[3];
        attachments[0] = colorAttachment(
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        attachments[1] = colorAttachment(
            VK_FORMAT_R32_UINT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        attachments[2] = colorAttachment(
            VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
        attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        VkAttachmentReference colors[2] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkAttachmentReference depth{
            2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 2;
        subpass.pColorAttachments = colors;
        subpass.pDepthStencilAttachment = &depth;
        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        dependency.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                  VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = 3;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 1;
        info.pDependencies = &dependency;
        VkResult result =
            vkCreateRenderPass(m_gpu.device, &info, nullptr, &m_gbufferPass);
        if (result != VK_SUCCESS) {
            if (error) *error = "vkCreateRenderPass(gbuffer): " +
                                vulkanResultName(result);
            return false;
        }
    }

    auto makeSimplePass = [&](VkFormat color0, VkFormat color1,
                              bool twoColors, VkRenderPass* pass) -> bool {
        VkAttachmentDescription attachments[2];
        attachments[0] = colorAttachment(
            color0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        if (twoColors) {
            attachments[1] = colorAttachment(
                color1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        }
        VkAttachmentReference colors[2] = {
            {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
            {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = twoColors ? 2u : 1u;
        subpass.pColorAttachments = colors;
        VkSubpassDependency dependencies[2]{};
        dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[0].dstSubpass = 0;
        dependencies[0].srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        dependencies[0].dstStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].srcSubpass = 0;
        dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
        dependencies[1].srcStageMask =
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        dependencies[1].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        dependencies[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        VkRenderPassCreateInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        info.attachmentCount = twoColors ? 2u : 1u;
        info.pAttachments = attachments;
        info.subpassCount = 1;
        info.pSubpasses = &subpass;
        info.dependencyCount = 2;
        info.pDependencies = dependencies;
        const VkResult result =
            vkCreateRenderPass(m_gpu.device, &info, nullptr, pass);
        if (result != VK_SUCCESS) {
            if (error) *error = "vkCreateRenderPass: " + vulkanResultName(result);
            return false;
        }
        return true;
    };
    if (!makeSimplePass(VK_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R8_UNORM, true,
                        &m_edgePass)) {
        return false;
    }
    if (!makeSimplePass(VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_UNDEFINED, false,
                        &m_finalPass)) {
        return false;
    }

    VkDescriptorSetLayoutBinding edgeBindings[2]{};
    for (int i = 0; i < 2; ++i) {
        edgeBindings[i].binding = static_cast<std::uint32_t>(i);
        edgeBindings[i].descriptorCount = 1;
        edgeBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    edgeBindings[0].descriptorType =
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    edgeBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    VkDescriptorSetLayoutCreateInfo edgeLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    edgeLayoutInfo.bindingCount = 2;
    edgeLayoutInfo.pBindings = edgeBindings;
    if (vkCreateDescriptorSetLayout(m_gpu.device, &edgeLayoutInfo, nullptr,
                                    &m_edgeSetLayout) != VK_SUCCESS) {
        if (error) *error = "vkCreateDescriptorSetLayout(edge) failed";
        return false;
    }
    VkDescriptorSetLayoutBinding finalBindings[2]{};
    for (int i = 0; i < 2; ++i) {
        finalBindings[i].binding = static_cast<std::uint32_t>(i);
        finalBindings[i].descriptorType =
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        finalBindings[i].descriptorCount = 1;
        finalBindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    VkDescriptorSetLayoutCreateInfo finalLayoutInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    finalLayoutInfo.bindingCount = 2;
    finalLayoutInfo.pBindings = finalBindings;
    if (vkCreateDescriptorSetLayout(m_gpu.device, &finalLayoutInfo, nullptr,
                                    &m_finalSetLayout) != VK_SUCCESS) {
        if (error) *error = "vkCreateDescriptorSetLayout(final) failed";
        return false;
    }

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_NEVER;
    samplerInfo.maxLod = 0.0f;
    if (vkCreateSampler(m_gpu.device, &samplerInfo, nullptr, &m_sampler) !=
        VK_SUCCESS) {
        if (error) *error = "vkCreateSampler failed";
        return false;
    }

    VkDescriptorPoolSize poolSizes[2]{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[0].descriptorCount = 16;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 8;
    VkDescriptorPoolCreateInfo poolInfo{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 8;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(m_gpu.device, &poolInfo, nullptr,
                               &m_descriptorPool) != VK_SUCCESS) {
        if (error) *error = "vkCreateDescriptorPool failed";
        return false;
    }

    VkPushConstantRange gbufferRange{};
    gbufferRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    gbufferRange.offset = 0;
    gbufferRange.size = sizeof(GbufferPushConstants);
    VkPipelineLayoutCreateInfo gbufferLayoutInfo{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    gbufferLayoutInfo.pushConstantRangeCount = 1;
    gbufferLayoutInfo.pPushConstantRanges = &gbufferRange;
    if (vkCreatePipelineLayout(m_gpu.device, &gbufferLayoutInfo, nullptr,
                               &m_gbufferLayout) != VK_SUCCESS) {
        if (error) *error = "vkCreatePipelineLayout(gbuffer) failed";
        return false;
    }
    VkPushConstantRange edgeRange{};
    edgeRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    edgeRange.offset = 0;
    edgeRange.size = sizeof(EdgePushConstants);
    VkPipelineLayoutCreateInfo edgeLayout{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    edgeLayout.setLayoutCount = 1;
    edgeLayout.pSetLayouts = &m_edgeSetLayout;
    edgeLayout.pushConstantRangeCount = 1;
    edgeLayout.pPushConstantRanges = &edgeRange;
    if (vkCreatePipelineLayout(m_gpu.device, &edgeLayout, nullptr,
                               &m_edgeLayout) != VK_SUCCESS) {
        if (error) *error = "vkCreatePipelineLayout(edge) failed";
        return false;
    }
    VkPipelineLayoutCreateInfo finalLayout{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    finalLayout.setLayoutCount = 1;
    finalLayout.pSetLayouts = &m_finalSetLayout;
    if (vkCreatePipelineLayout(m_gpu.device, &finalLayout, nullptr,
                               &m_finalLayout) != VK_SUCCESS) {
        if (error) *error = "vkCreatePipelineLayout(final) failed";
        return false;
    }

    const VkShaderModule geomVert = createShaderModule(
        m_gpu, shaders::kGeomVertSpv, shaders::kGeomVertSpvSize, error);
    const VkShaderModule geomFrag = createShaderModule(
        m_gpu, shaders::kGeomFragSpv, shaders::kGeomFragSpvSize, error);
    const VkShaderModule fullscreenVert = createShaderModule(
        m_gpu, shaders::kFullscreenVertSpv,
        shaders::kFullscreenVertSpvSize, error);
    const VkShaderModule edgeFrag = createShaderModule(
        m_gpu, shaders::kEdgeFragSpv, shaders::kEdgeFragSpvSize, error);
    const VkShaderModule finalFrag = createShaderModule(
        m_gpu, shaders::kFinalFragSpv, shaders::kFinalFragSpvSize, error);
    if (geomVert == VK_NULL_HANDLE || geomFrag == VK_NULL_HANDLE ||
        fullscreenVert == VK_NULL_HANDLE || edgeFrag == VK_NULL_HANDLE ||
        finalFrag == VK_NULL_HANDLE) {
        return false;
    }

    auto makePipeline = [&](VkRenderPass renderPass, VkPipelineLayout layout,
                            VkShaderModule vertex, VkShaderModule fragment,
                            bool depth, bool vertexInput,
                            std::uint32_t colorCount,
                            VkPipeline* pipeline) -> bool {
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        stages[1].pName = "main";
        VkVertexInputBindingDescription binding{
            0, sizeof(MeshVertex), VK_VERTEX_INPUT_RATE_VERTEX};
        VkVertexInputAttributeDescription attributes[2] = {
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(MeshVertex, normal)}};
        VkPipelineVertexInputStateCreateInfo vertexInfo{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        if (vertexInput) {
            vertexInfo.vertexBindingDescriptionCount = 1;
            vertexInfo.pVertexBindingDescriptions = &binding;
            vertexInfo.vertexAttributeDescriptionCount = 2;
            vertexInfo.pVertexAttributeDescriptions = attributes;
        }
        VkPipelineInputAssemblyStateCreateInfo assembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkViewport viewport{0.0f, 0.0f, static_cast<float>(m_width),
                            static_cast<float>(m_height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, {m_width, m_height}};
        VkPipelineViewportStateCreateInfo viewportInfo{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewportInfo.viewportCount = 1;
        viewportInfo.pViewports = &viewport;
        viewportInfo.scissorCount = 1;
        viewportInfo.pScissors = &scissor;
        VkPipelineRasterizationStateCreateInfo raster{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_NONE;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depthInfo{
            VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depthInfo.depthTestEnable = depth ? VK_TRUE : VK_FALSE;
        depthInfo.depthWriteEnable = depth ? VK_TRUE : VK_FALSE;
        depthInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        VkPipelineColorBlendAttachmentState blendAttachments[2]{};
        for (std::uint32_t i = 0; i < colorCount; ++i) {
            blendAttachments[i].blendEnable = VK_FALSE;
            blendAttachments[i].colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
                                                  VK_COLOR_COMPONENT_G_BIT |
                                                  VK_COLOR_COMPONENT_B_BIT |
                                                  VK_COLOR_COMPONENT_A_BIT;
        }
        VkPipelineColorBlendStateCreateInfo blend{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend.attachmentCount = colorCount;
        blend.pAttachments = blendAttachments;
        VkGraphicsPipelineCreateInfo info{
            VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertexInfo;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewportInfo;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depthInfo;
        info.pColorBlendState = &blend;
        info.layout = layout;
        info.renderPass = renderPass;
        info.subpass = 0;
        const VkResult result =
            vkCreateGraphicsPipelines(m_gpu.device, VK_NULL_HANDLE, 1, &info,
                                      nullptr, pipeline);
        if (result != VK_SUCCESS) {
            if (error) *error = "vkCreateGraphicsPipelines: " +
                                vulkanResultName(result);
            return false;
        }
        return true;
    };

    const bool ok =
        makePipeline(m_gbufferPass, m_gbufferLayout, geomVert, geomFrag, true,
                     true, 2, &m_gbufferPipeline) &&
        makePipeline(m_edgePass, m_edgeLayout, fullscreenVert, edgeFrag, false,
                     false, 2, &m_edgePipeline) &&
        makePipeline(m_finalPass, m_finalLayout, fullscreenVert, finalFrag,
                     false, false, 1, &m_finalPipeline);
    vkDestroyShaderModule(m_gpu.device, geomVert, nullptr);
    vkDestroyShaderModule(m_gpu.device, geomFrag, nullptr);
    vkDestroyShaderModule(m_gpu.device, fullscreenVert, nullptr);
    vkDestroyShaderModule(m_gpu.device, edgeFrag, nullptr);
    vkDestroyShaderModule(m_gpu.device, finalFrag, nullptr);
    return ok;
}

bool Renderer::createTargets(std::uint32_t width, std::uint32_t height,
                             const std::string& sceneTag, std::string* error) {
    m_sceneTag = sceneTag;
    const VkImageUsageFlags sampledColor =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
        VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    const VkImageUsageFlags readColor =
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (!createImage(m_gpu, width, height, VK_FORMAT_R32G32B32A32_SFLOAT,
                     sampledColor, m_gbuffer, error)) {
        return false;
    }
    if (!createImage(m_gpu, width, height, VK_FORMAT_R32_UINT, readColor,
                     m_objectId, error)) {
        return false;
    }
    if (!createImage(m_gpu, width, height, VK_FORMAT_D32_SFLOAT,
                     VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, m_depth,
                     error)) {
        return false;
    }
    if (!createImage(m_gpu, width, height, VK_FORMAT_R32G32B32A32_SFLOAT,
                     sampledColor, m_metrics, error)) {
        return false;
    }
    if (!createImage(m_gpu, width, height, VK_FORMAT_R8_UNORM, readColor,
                     m_boundary, error)) {
        return false;
    }
    if (!createImage(m_gpu, width, height, VK_FORMAT_R8G8B8A8_UNORM,
                     readColor, m_final, error)) {
        return false;
    }
    return true;
}

void Renderer::destroyTargets() {
    if (m_gpu.device == VK_NULL_HANDLE) return;
    if (m_gbufferFB != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_gpu.device, m_gbufferFB, nullptr);
        m_gbufferFB = VK_NULL_HANDLE;
    }
    if (m_edgeFB != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_gpu.device, m_edgeFB, nullptr);
        m_edgeFB = VK_NULL_HANDLE;
    }
    if (m_finalFB != VK_NULL_HANDLE) {
        vkDestroyFramebuffer(m_gpu.device, m_finalFB, nullptr);
        m_finalFB = VK_NULL_HANDLE;
    }
    destroyImage(m_gpu, m_gbuffer);
    destroyImage(m_gpu, m_objectId);
    destroyImage(m_gpu, m_depth);
    destroyImage(m_gpu, m_metrics);
    destroyImage(m_gpu, m_boundary);
    destroyImage(m_gpu, m_final);
    if (m_queryPool != VK_NULL_HANDLE) {
        vkDestroyQueryPool(m_gpu.device, m_queryPool, nullptr);
        m_queryPool = VK_NULL_HANDLE;
    }
}

bool Renderer::createFramebuffers(const std::string& sceneTag,
                                  SceneFrameEvidence& evidence,
                                  std::string* error) {
    auto createFramebuffer = [&](VkRenderPass pass,
                                 const std::vector<VkImageView>& views,
                                 VkFramebuffer* framebuffer) -> bool {
        VkFramebufferCreateInfo info{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        info.renderPass = pass;
        info.attachmentCount = static_cast<std::uint32_t>(views.size());
        info.pAttachments = views.data();
        info.width = m_width;
        info.height = m_height;
        info.layers = 1;
        const VkResult result =
            vkCreateFramebuffer(m_gpu.device, &info, nullptr, framebuffer);
        if (result != VK_SUCCESS) {
            if (error) *error = "vkCreateFramebuffer: " +
                                vulkanResultName(result);
            return false;
        }
        return true;
    };
    if (!createFramebuffer(m_gbufferPass,
                           {m_gbuffer.view, m_objectId.view, m_depth.view},
                           &m_gbufferFB) ||
        !createFramebuffer(m_edgePass, {m_metrics.view, m_boundary.view},
                           &m_edgeFB) ||
        !createFramebuffer(m_finalPass, {m_final.view}, &m_finalFB)) {
        return false;
    }

    auto addFramebuffer = [&](const std::string& passName,
                              const std::string& framebufferId,
                              std::vector<AttachmentEvidence> attachments) {
        FramebufferEvidence fb;
        fb.id = evidence.framebuffers.size() + 1;
        fb.pass = passName;
        fb.framebufferId = framebufferId;
        fb.width = m_width;
        fb.height = m_height;
        fb.attachments = std::move(attachments);
        fb.time = nowPoint();
        evidence.framebuffers.push_back(std::move(fb));
    };
    auto attachment = [&](const std::string& name, const std::string& imageId,
                          VkFormat format, VkImageLayout initial,
                          VkImageLayout finalLayout, const std::string& clear) {
        AttachmentEvidence a;
        a.name = name;
        a.imageId = imageId;
        a.format = vulkanFormatName(format);
        a.initialLayout = vulkanLayoutName(initial);
        a.finalLayout = vulkanLayoutName(finalLayout);
        a.clear = clear;
        a.width = m_width;
        a.height = m_height;
        return a;
    };
    const std::string prefix = "FB_" + sceneTag + "_";
    const std::string imagePrefix = "IMG_" + sceneTag + "_";
    addFramebuffer("gbuffer", prefix + "gbuffer",
                   {attachment("normal_depth", imagePrefix + "gbuffer",
                               VK_FORMAT_R32G32B32A32_SFLOAT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               "0,0,0,0"),
                    attachment("object_id", imagePrefix + "objectId",
                               VK_FORMAT_R32_UINT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               "0,0,0,0"),
                    attachment("depth", imagePrefix + "depth",
                               VK_FORMAT_D32_SFLOAT,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                               "1.0")});
    addFramebuffer("edge", prefix + "edge",
                   {attachment("metrics", imagePrefix + "metrics",
                               VK_FORMAT_R32G32B32A32_SFLOAT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               "0,0,0,0"),
                    attachment("boundary", imagePrefix + "boundary",
                               VK_FORMAT_R8_UNORM,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               "0,0,0,0")});
    addFramebuffer("final", prefix + "final",
                   {attachment("color", imagePrefix + "final",
                               VK_FORMAT_R8G8B8A8_UNORM,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               "0.035,0.045,0.06,1")});
    return true;
}

void Renderer::updateDescriptors() {
    VkDescriptorImageInfo gbufferInfo{};
    gbufferInfo.sampler = m_sampler;
    gbufferInfo.imageView = m_gbuffer.view;
    gbufferInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorImageInfo metricsInfo{};
    metricsInfo.sampler = m_sampler;
    metricsInfo.imageView = m_metrics.view;
    metricsInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkDescriptorBufferInfo seedInfo{};
    seedInfo.buffer = m_seedBuffer.buffer;
    seedInfo.offset = 0;
    seedInfo.range = VK_WHOLE_SIZE;
    VkWriteDescriptorSet writes[4]{};
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_edgeSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorCount = 1;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].pImageInfo = &gbufferInfo;
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_edgeSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorCount = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].pBufferInfo = &seedInfo;
    writes[2] = writes[0];
    writes[2].dstSet = m_finalSet;
    writes[2].pImageInfo = &gbufferInfo;
    writes[3] = writes[0];
    writes[3].dstSet = m_finalSet;
    writes[3].dstBinding = 1;
    writes[3].pImageInfo = &metricsInfo;
    vkUpdateDescriptorSets(m_gpu.device, 4, writes, 0, nullptr);
}

void Renderer::transition(const std::string& pass, GpuImage& image,
                          const std::string& imageId,
                          VkImageAspectFlags aspect,
                          VkImageLayout newLayout,
                          VkPipelineStageFlags srcStage,
                          VkPipelineStageFlags dstStage,
                          VkAccessFlags srcAccess,
                          VkAccessFlags dstAccess,
                          VkCommandBuffer commandBuffer,
                          SceneFrameEvidence& evidence) {
    TransitionEvidence record;
    record.id = evidence.transitions.size() + 1;
    record.pass = pass;
    record.imageId = imageId;
    record.oldLayout = vulkanLayoutName(image.layout);
    record.newLayout = vulkanLayoutName(newLayout);
    record.srcStage = vulkanStageName(srcStage);
    record.dstStage = vulkanStageName(dstStage);
    record.srcAccess = vulkanAccessName(srcAccess);
    record.dstAccess = vulkanAccessName(dstAccess);
    record.commandOrdinal = ++evidence.commandOrdinal;
    record.time = nowPoint();
    evidence.transitions.push_back(std::move(record));

    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = image.layout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = {aspect, 0, 1, 0, 1};
    vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0,
                         nullptr, 1, &barrier);
    image.layout = newLayout;
}

bool Renderer::renderScene(const SceneDefinition& scene,
                           SceneFrameEvidence& evidence, std::string* error) {
    evidence.scene = scene.name;
    evidence.start = nowPoint();

    if (!createTargets(m_width, m_height, scene.name, error)) {
        destroyTargets();
        return false;
    }
    if (!createFramebuffers(scene.name, evidence, error)) {
        destroyTargets();
        return false;
    }

    vkResetDescriptorPool(m_gpu.device, m_descriptorPool, 0);
    VkDescriptorSetLayout layouts[2] = {m_edgeSetLayout, m_finalSetLayout};
    VkDescriptorSet sets[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkDescriptorSetAllocateInfo allocate{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocate.descriptorPool = m_descriptorPool;
    allocate.descriptorSetCount = 2;
    allocate.pSetLayouts = layouts;
    if (vkAllocateDescriptorSets(m_gpu.device, &allocate, sets) != VK_SUCCESS) {
        if (error) *error = "vkAllocateDescriptorSets failed";
        destroyTargets();
        return false;
    }
    m_edgeSet = sets[0];
    m_finalSet = sets[1];
    updateDescriptors();

    bool useTimestamps = m_gpu.timestampValid;
    if (useTimestamps) {
        VkQueryPoolCreateInfo queryInfo{
            VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
        queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
        queryInfo.queryCount = 6;
        if (vkCreateQueryPool(m_gpu.device, &queryInfo, nullptr,
                              &m_queryPool) != VK_SUCCESS) {
            m_queryPool = VK_NULL_HANDLE;
            useTimestamps = false;
        }
    }

    const VkDeviceSize pixelCount =
        static_cast<VkDeviceSize>(m_width) * m_height;
    const VkDeviceSize gbufferBytes = pixelCount * 16;
    const VkDeviceSize objectIdBytes = pixelCount * 4;
    const VkDeviceSize metricsBytes = pixelCount * 16;
    const VkDeviceSize boundaryBytes = pixelCount;
    const VkDeviceSize finalBytes = pixelCount * 4;
    const VkDeviceSize gbufferOffset = 0;
    const VkDeviceSize objectIdOffset = gbufferOffset + gbufferBytes;
    const VkDeviceSize metricsOffset = objectIdOffset + objectIdBytes;
    const VkDeviceSize boundaryOffset = metricsOffset + metricsBytes;
    const VkDeviceSize finalOffset = boundaryOffset + boundaryBytes;
    const VkDeviceSize totalBytes = finalOffset + finalBytes;
    GpuBuffer readback;
    if (!createBuffer(m_gpu, totalBytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                      readback, error)) {
        destroyTargets();
        return false;
    }
    VkCommandBuffer commandBuffer = beginOneShot(m_gpu, error);
    if (commandBuffer == VK_NULL_HANDLE) {
        destroyBuffer(m_gpu, readback);
        destroyTargets();
        return false;
    }
    if (useTimestamps) vkCmdResetQueryPool(commandBuffer, m_queryPool, 0, 6);

    auto writeTimestamp = [&](std::uint32_t query, VkPipelineStageFlagBits stage) {
        if (useTimestamps) {
            vkCmdWriteTimestamp(commandBuffer, stage, m_queryPool, query);
        }
    };

    PassTiming gbufferTiming;
    gbufferTiming.pass = "gbuffer";
    gbufferTiming.queryStart = 0;
    gbufferTiming.queryEnd = 1;
    gbufferTiming.timestampPeriodNs = m_gpu.timestampPeriod;
    evidence.passTimings.push_back(gbufferTiming);
    PassTiming edgeTiming;
    edgeTiming.pass = "edge";
    edgeTiming.queryStart = 2;
    edgeTiming.queryEnd = 3;
    edgeTiming.timestampPeriodNs = m_gpu.timestampPeriod;
    evidence.passTimings.push_back(edgeTiming);
    PassTiming finalTiming;
    finalTiming.pass = "final";
    finalTiming.queryStart = 4;
    finalTiming.queryEnd = 5;
    finalTiming.timestampPeriodNs = m_gpu.timestampPeriod;
    evidence.passTimings.push_back(finalTiming);

    const std::string imagePrefix = "IMG_" + scene.name + "_";
    transition("gbuffer", m_depth, imagePrefix + "depth",
               VK_IMAGE_ASPECT_DEPTH_BIT,
               VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT, 0,
               VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
               commandBuffer, evidence);
    transition("gbuffer", m_gbuffer, imagePrefix + "gbuffer",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, commandBuffer, evidence);
    transition("gbuffer", m_objectId, imagePrefix + "objectId",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, commandBuffer, evidence);

    evidence.passTimings[0].cpuStart = nowPoint();
    writeTimestamp(0, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkClearValue gbufferClears[3]{};
    gbufferClears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    gbufferClears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    gbufferClears[2].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo gbufferBegin{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    gbufferBegin.renderPass = m_gbufferPass;
    gbufferBegin.framebuffer = m_gbufferFB;
    gbufferBegin.renderArea = {{0, 0}, {m_width, m_height}};
    gbufferBegin.clearValueCount = 3;
    gbufferBegin.pClearValues = gbufferClears;
    vkCmdBeginRenderPass(commandBuffer, &gbufferBegin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_gbufferPipeline);
    GbufferPushConstants push{};
    push.proj = m_projection;
    for (const SceneDraw& draw : scene.draws) {
        const bool cube = draw.mesh == MeshType::Cube;
        const GpuBuffer& vertexBuffer = cube ? m_cubeVB : m_groundVB;
        const GpuBuffer& indexBuffer = cube ? m_cubeIB : m_groundIB;
        const MeshUploadInfo& mesh = cube ? m_cubeInfo : m_groundInfo;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &vertexBuffer.buffer,
                               &offset);
        vkCmdBindIndexBuffer(commandBuffer, indexBuffer.buffer, 0,
                             VK_INDEX_TYPE_UINT32);
        push.modelView = draw.modelView;
        vkCmdPushConstants(commandBuffer, m_gbufferLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0,
                           sizeof(GbufferPushConstants), &push);
        DrawCallEvidence record;
        record.id = evidence.draws.size() + 1;
        record.pass = "gbuffer";
        record.passIndex = 0;
        record.objectIndex = draw.objectIndex;
        record.objectId = draw.logicalId;
        record.objectIdValue = draw.objectIdValue;
        record.instanceIndex = 0;
        record.firstInstance = draw.objectIdValue;
        record.vertexOffset = 0;
        record.firstIndex = 0;
        record.indexCount = mesh.indexCount;
        record.instanceCount = 1;
        record.vertexBufferId = mesh.vertexBufferId;
        record.indexBufferId = mesh.indexBufferId;
        record.pipelineId = "PIPE_gbuffer";
        record.pipeLayoutId = "PL_gbuffer";
        record.descriptorSetId = "none";
        record.meshSha256 = mesh.meshSha256;
        record.modelTranslation =
            {draw.model.m[12], draw.model.m[13], draw.model.m[14]};
        record.duplicate = draw.duplicate;
        record.repeatOf = draw.repeatOf;
        record.commandOrdinal = ++evidence.commandOrdinal;
        record.time = nowPoint();
        evidence.draws.push_back(std::move(record));
        vkCmdDrawIndexed(commandBuffer, mesh.indexCount, 1, 0, 0,
                         draw.objectIdValue);
    }
    vkCmdEndRenderPass(commandBuffer);
    writeTimestamp(1, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    evidence.passTimings[0].cpuEnd = nowPoint();

    transition("gbuffer", m_gbuffer, imagePrefix + "gbuffer",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
               commandBuffer, evidence);
    transition("gbuffer", m_objectId, imagePrefix + "objectId",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, commandBuffer, evidence);

    transition("edge", m_metrics, imagePrefix + "metrics",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, commandBuffer, evidence);
    transition("edge", m_boundary, imagePrefix + "boundary",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, commandBuffer, evidence);
    evidence.passTimings[1].cpuStart = nowPoint();
    writeTimestamp(2, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkClearValue edgeClears[2]{};
    edgeClears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    edgeClears[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
    VkRenderPassBeginInfo edgeBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    edgeBegin.renderPass = m_edgePass;
    edgeBegin.framebuffer = m_edgeFB;
    edgeBegin.renderArea = {{0, 0}, {m_width, m_height}};
    edgeBegin.clearValueCount = 2;
    edgeBegin.pClearValues = edgeClears;
    vkCmdBeginRenderPass(commandBuffer, &edgeBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_edgePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_edgeLayout, 0, 1, &m_edgeSet, 0, nullptr);
    EdgePushConstants edgePush{};
    edgePush.texel[0] = 1.0f / static_cast<float>(m_width);
    edgePush.texel[1] = 1.0f / static_cast<float>(m_height);
    edgePush.depthThreshold = m_outlineDepth;
    edgePush.normalThreshold = m_normalThreshold;
    edgePush.gtDepthEps = m_gtDepthEps;
    edgePush.gtNormalEps = m_gtNormalEps;
    edgePush.width = m_width;
    edgePush.height = m_height;
    vkCmdPushConstants(commandBuffer, m_edgeLayout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       sizeof(EdgePushConstants), &edgePush);
    {
        DrawCallEvidence record;
        record.id = evidence.draws.size() + 1;
        record.pass = "edge";
        record.passIndex = 1;
        record.objectId = "fullscreen.edge";
        record.indexCount = 3;
        record.instanceCount = 1;
        record.pipelineId = "PIPE_edge";
        record.pipeLayoutId = "PL_edge";
        record.descriptorSetId = "DS_edge";
        record.meshSha256 = "procedural_fullscreen_triangle";
        record.commandOrdinal = ++evidence.commandOrdinal;
        record.time = nowPoint();
        evidence.draws.push_back(std::move(record));
    }
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    writeTimestamp(3, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    evidence.passTimings[1].cpuEnd = nowPoint();

    transition("edge", m_boundary, imagePrefix + "boundary",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, commandBuffer, evidence);
    transition("final", m_metrics, imagePrefix + "metrics",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
               commandBuffer, evidence);
    transition("final", m_final, imagePrefix + "final",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, commandBuffer, evidence);
    evidence.passTimings[2].cpuStart = nowPoint();
    writeTimestamp(4, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
    VkClearValue finalClear{};
    finalClear.color = {{0.035f, 0.045f, 0.060f, 1.0f}};
    VkRenderPassBeginInfo finalBegin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    finalBegin.renderPass = m_finalPass;
    finalBegin.framebuffer = m_finalFB;
    finalBegin.renderArea = {{0, 0}, {m_width, m_height}};
    finalBegin.clearValueCount = 1;
    finalBegin.pClearValues = &finalClear;
    vkCmdBeginRenderPass(commandBuffer, &finalBegin,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                      m_finalPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_finalLayout, 0, 1, &m_finalSet, 0, nullptr);
    {
        DrawCallEvidence record;
        record.id = evidence.draws.size() + 1;
        record.pass = "final";
        record.passIndex = 2;
        record.objectId = "fullscreen.final";
        record.indexCount = 3;
        record.instanceCount = 1;
        record.pipelineId = "PIPE_final";
        record.pipeLayoutId = "PL_final";
        record.descriptorSetId = "DS_final";
        record.meshSha256 = "procedural_fullscreen_triangle";
        record.commandOrdinal = ++evidence.commandOrdinal;
        record.time = nowPoint();
        evidence.draws.push_back(std::move(record));
    }
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);
    writeTimestamp(5, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    evidence.passTimings[2].cpuEnd = nowPoint();

    transition("final", m_final, imagePrefix + "final",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT,
               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, commandBuffer, evidence);
    transition("final", m_gbuffer, imagePrefix + "gbuffer",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, commandBuffer, evidence);
    transition("final", m_metrics, imagePrefix + "metrics",
               VK_IMAGE_ASPECT_COLOR_BIT,
               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_SHADER_READ_BIT,
               VK_ACCESS_TRANSFER_READ_BIT, commandBuffer, evidence);

    auto copyImage = [&](GpuImage& image, VkDeviceSize offset) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {m_width, m_height, 1};
        vkCmdCopyImageToBuffer(commandBuffer, image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                               readback.buffer, 1, &region);
    };
    copyImage(m_gbuffer, gbufferOffset);
    copyImage(m_objectId, objectIdOffset);
    copyImage(m_metrics, metricsOffset);
    copyImage(m_boundary, boundaryOffset);
    copyImage(m_final, finalOffset);
    evidence.commandOrdinal += 5;

    evidence.submitStart = nowPoint();
    if (!endOneShot(m_gpu, commandBuffer, error)) {
        destroyBuffer(m_gpu, readback);
        destroyTargets();
        return false;
    }
    evidence.submitEnd = nowPoint();

    if (useTimestamps) {
        std::array<std::uint64_t, 6> ticks{};
        const VkResult result = vkGetQueryPoolResults(
            m_gpu.device, m_queryPool, 0, 6, sizeof(ticks), ticks.data(),
            sizeof(std::uint64_t), VK_QUERY_RESULT_64_BIT);
        if (result == VK_SUCCESS) {
            for (PassTiming& timing : evidence.passTimings) {
                timing.startTicks = ticks[timing.queryStart];
                timing.endTicks = ticks[timing.queryEnd];
                timing.valid = timing.endTicks >= timing.startTicks;
                if (timing.valid) {
                    timing.elapsedNs = static_cast<std::uint64_t>(
                        static_cast<double>(timing.endTicks - timing.startTicks) *
                        m_gpu.timestampPeriod);
                }
            }
        }
    }

    const auto* mapped = static_cast<const std::uint8_t*>(readback.mapped);
    const std::size_t pixels =
        static_cast<std::size_t>(m_width) * m_height;
    std::vector<std::uint8_t> gbufferRaw(
        mapped + gbufferOffset, mapped + gbufferOffset + gbufferBytes);
    std::vector<std::uint8_t> objectIdRaw(
        mapped + objectIdOffset, mapped + objectIdOffset + objectIdBytes);
    std::vector<std::uint8_t> metricsRaw(
        mapped + metricsOffset, mapped + metricsOffset + metricsBytes);
    std::vector<std::uint8_t> boundaryRaw(
        mapped + boundaryOffset, mapped + boundaryOffset + boundaryBytes);
    std::vector<std::uint8_t> finalRaw(
        mapped + finalOffset, mapped + finalOffset + finalBytes);

    evidence.metricsValues.resize(pixels * 4);
    std::memcpy(evidence.metricsValues.data(), metricsRaw.data(),
                metricsRaw.size());
    evidence.objectIdValues.resize(pixels);
    std::memcpy(evidence.objectIdValues.data(), objectIdRaw.data(),
                objectIdRaw.size());
    evidence.boundaryValues.assign(boundaryRaw.begin(), boundaryRaw.end());
    evidence.seedValues = m_seedBytes;
    evidence.finalImage = rgbaImageFromBytes(finalRaw, m_width, m_height);
    evidence.metricsImage = metricsImageFromFloats(
        evidence.metricsValues, m_width, m_height, m_outlineDepth,
        m_normalThreshold);
    evidence.boundaryImage =
        maskImageFromBytes(evidence.boundaryValues, m_width, m_height);
    evidence.gbufferImage =
        gbufferImageFromFloats(gbufferRaw, m_width, m_height);

    evidence.gbuffer = {"normal_depth", imagePrefix + "gbuffer",
                        vulkanFormatName(VK_FORMAT_R32G32B32A32_SFLOAT),
                        sha256Hex(gbufferRaw.data(), gbufferRaw.size()), "",
                        m_width, m_height, gbufferRaw.size()};
    evidence.objectId = {"object_id", imagePrefix + "objectId",
                         vulkanFormatName(VK_FORMAT_R32_UINT),
                         sha256Hex(objectIdRaw.data(), objectIdRaw.size()), "",
                         m_width, m_height, objectIdRaw.size()};
    evidence.metrics = {"metrics", imagePrefix + "metrics",
                        vulkanFormatName(VK_FORMAT_R32G32B32A32_SFLOAT),
                        sha256Hex(metricsRaw.data(), metricsRaw.size()), "",
                        m_width, m_height, metricsRaw.size()};
    evidence.boundaryMask = {"boundary", imagePrefix + "boundary",
                             vulkanFormatName(VK_FORMAT_R8_UNORM),
                             sha256Hex(boundaryRaw.data(), boundaryRaw.size()),
                             "", m_width, m_height, boundaryRaw.size()};
    std::vector<std::uint32_t> seedWords(
        static_cast<std::size_t>(pixelCount), 0u);
    for (std::size_t i = 0; i < m_seedBytes.size(); ++i) {
        seedWords[i] = m_seedBytes[i] ? 1u : 0u;
    }
    evidence.boundarySeed = {
        "boundary_seed", "BUF_" + scene.name + "_boundarySeed",
        "VK_DESCRIPTOR_TYPE_STORAGE_BUFFER",
        sha256Hex(seedWords.data(), seedWords.size() * sizeof(std::uint32_t)),
        "", m_width, m_height,
        static_cast<std::uint64_t>(seedWords.size() * sizeof(std::uint32_t))};
    evidence.finalColor = {"final_color", imagePrefix + "final",
                           vulkanFormatName(VK_FORMAT_R8G8B8A8_UNORM),
                           sha256Hex(finalRaw.data(), finalRaw.size()), "",
                           m_width, m_height, finalRaw.size()};

    destroyBuffer(m_gpu, readback);
    destroyTargets();
    evidence.end = nowPoint();
    return true;
}

}  // namespace ssor
