#pragma once

#include "core/scene.h"
#include "core/timestamp.h"
#include "core/util.h"
#include "render/vk_context.h"

#include <cstdint>
#include <string>
#include <vector>

namespace ssor {

struct DrawCallEvidence {
    std::uint64_t id = 0;
    std::string pass;
    std::uint32_t passIndex = 0;
    std::uint32_t objectIndex = 0;
    std::string objectId;
    std::uint32_t objectIdValue = 0;
    std::uint32_t instanceIndex = 0;
    std::uint32_t firstInstance = 0;
    std::uint32_t vertexOffset = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 1;
    std::string vertexBufferId;
    std::string indexBufferId;
    std::string pipelineId;
    std::string pipeLayoutId;
    std::string descriptorSetId;
    std::string meshSha256;
    Vec3f modelTranslation;
    bool duplicate = false;
    std::uint32_t repeatOf = kInvalidObject;
    std::uint64_t commandOrdinal = 0;
    TimestampPoint time;
};

struct TransitionEvidence {
    std::uint64_t id = 0;
    std::string pass;
    std::string imageId;
    std::string oldLayout;
    std::string newLayout;
    std::string srcStage;
    std::string dstStage;
    std::string srcAccess;
    std::string dstAccess;
    std::uint64_t commandOrdinal = 0;
    TimestampPoint time;
};

struct AttachmentEvidence {
    std::string name;
    std::string imageId;
    std::string format;
    std::string initialLayout;
    std::string finalLayout;
    std::string clear;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
};

struct FramebufferEvidence {
    std::uint64_t id = 0;
    std::string pass;
    std::string framebufferId;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<AttachmentEvidence> attachments;
    TimestampPoint time;
};

struct PassTiming {
    std::string pass;
    std::uint32_t queryStart = 0;
    std::uint32_t queryEnd = 0;
    bool valid = false;
    double timestampPeriodNs = 0.0;
    std::uint64_t startTicks = 0;
    std::uint64_t endTicks = 0;
    std::uint64_t elapsedNs = 0;
    TimestampPoint cpuStart;
    TimestampPoint cpuEnd;
};

struct ImageEvidence {
    std::string name;
    std::string logicalId;
    std::string format;
    std::string sha256;
    std::string ppmPath;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t bytes = 0;
};

struct SceneFrameEvidence {
    std::string scene;
    TimestampPoint start;
    TimestampPoint end;
    TimestampPoint submitStart;
    TimestampPoint submitEnd;
    std::uint64_t commandOrdinal = 0;
    std::uint64_t submitIndex = 0;
    std::vector<DrawCallEvidence> draws;
    std::vector<TransitionEvidence> transitions;
    std::vector<FramebufferEvidence> framebuffers;
    std::vector<PassTiming> passTimings;
    ImageEvidence gbuffer;
    ImageEvidence objectId;
    ImageEvidence metrics;
    ImageEvidence boundaryMask;
    ImageEvidence boundarySeed;
    ImageEvidence finalColor;
    std::vector<float> metricsValues;
    std::vector<std::uint32_t> objectIdValues;
    std::vector<std::uint8_t> boundaryValues;
    std::vector<std::uint8_t> seedValues;
    ImageRgba8 finalImage;
    ImageRgba8 metricsImage;
    ImageRgba8 boundaryImage;
    ImageRgba8 gbufferImage;
};

struct MeshUploadInfo {
    std::string vertexBufferId;
    std::string indexBufferId;
    std::string meshSha256;
    std::uint32_t indexCount = 0;
};

class Renderer {
public:
    bool init(const RawMaterial& material, const SceneBuild& scenes,
              std::string* error);
    void shutdown();
    bool renderScene(const SceneDefinition& scene, SceneFrameEvidence& evidence,
                     std::string* error);

    const GpuContext& gpu() const { return m_gpu; }
    const MeshUploadInfo& cubeInfo() const { return m_cubeInfo; }
    const MeshUploadInfo& groundInfo() const { return m_groundInfo; }
    std::uint32_t seedPixelCount() const;

private:
    bool createPipelines(std::string* error);
    bool uploadMeshes(const SceneBuild& scenes, std::string* error);
    bool createTargets(std::uint32_t width, std::uint32_t height,
                       const std::string& sceneTag, std::string* error);
    void destroyTargets();
    bool createFramebuffers(const std::string& sceneTag,
                            SceneFrameEvidence& evidence, std::string* error);
    void updateDescriptors();
    void transition(const std::string& pass, GpuImage& image,
                    const std::string& imageId, VkImageAspectFlags aspect,
                    VkImageLayout newLayout, VkPipelineStageFlags srcStage,
                    VkPipelineStageFlags dstStage, VkAccessFlags srcAccess,
                    VkAccessFlags dstAccess, VkCommandBuffer commandBuffer,
                    SceneFrameEvidence& evidence);
    GpuContext m_gpu;
    std::uint32_t m_width = 0;
    std::uint32_t m_height = 0;
    float m_outlineDepth = 0.0f;
    float m_normalThreshold = 0.0f;
    float m_gtDepthEps = 0.0f;
    float m_gtNormalEps = 0.0f;
    Mat4 m_projection;

    VkRenderPass m_gbufferPass = VK_NULL_HANDLE;
    VkRenderPass m_edgePass = VK_NULL_HANDLE;
    VkRenderPass m_finalPass = VK_NULL_HANDLE;
    VkPipelineLayout m_gbufferLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_edgeLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_finalLayout = VK_NULL_HANDLE;
    VkPipeline m_gbufferPipeline = VK_NULL_HANDLE;
    VkPipeline m_edgePipeline = VK_NULL_HANDLE;
    VkPipeline m_finalPipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_edgeSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_finalSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkSampler m_sampler = VK_NULL_HANDLE;

    GpuBuffer m_cubeVB;
    GpuBuffer m_cubeIB;
    GpuBuffer m_groundVB;
    GpuBuffer m_groundIB;
    MeshUploadInfo m_cubeInfo;
    MeshUploadInfo m_groundInfo;

    GpuImage m_gbuffer;
    GpuImage m_objectId;
    GpuImage m_depth;
    GpuImage m_metrics;
    GpuImage m_boundary;
    GpuImage m_final;
    VkFramebuffer m_gbufferFB = VK_NULL_HANDLE;
    VkFramebuffer m_edgeFB = VK_NULL_HANDLE;
    VkFramebuffer m_finalFB = VK_NULL_HANDLE;
    VkDescriptorSet m_edgeSet = VK_NULL_HANDLE;
    VkDescriptorSet m_finalSet = VK_NULL_HANDLE;
    std::string m_sceneTag;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    std::vector<std::uint8_t> m_seedBytes;
    GpuBuffer m_seedBuffer;
};

}  // namespace ssor
