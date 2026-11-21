#pragma once

#include "skeleton.hpp"
#include "snapshot.hpp"
#include "vk_common.hpp"

namespace skr {

// Owns the offscreen render target and the GPU-skinning pipeline. It renders
// one mesh into a reusable framebuffer per call and returns a fully populated
// Snapshot including the RGBA readback, draw-call record and the ordered
// resource-state transition trail.
class SkinningRenderer {
public:
    SkinningRenderer(VulkanContext& ctx, const uint32_t* vertSpv,
                     size_t vertSpvWords, const uint32_t* fragSpv,
                     size_t fragSpvWords);
    ~SkinningRenderer();

    SkinningRenderer(const SkinningRenderer&) = delete;
    SkinningRenderer& operator=(const SkinningRenderer&) = delete;

    Snapshot render(const SkinnedMesh& mesh, const std::string& label,
                    uint64_t frameId);

private:
    struct GpuBuffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize size = 0;
    };

    void createRenderTarget();
    void createPipeline(const uint32_t* vertSpv, size_t vertSpvWords,
                        const uint32_t* fragSpv, size_t fragSpvWords);
    GpuBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                           VkMemoryPropertyFlags props);
    void destroyBuffer(GpuBuffer& b);
    void transitionImage(VkImage image, VkImageLayout oldLayout,
                         VkImageLayout newLayout, VkCommandBuffer cmd,
                         const std::string& resourceName,
                         VkImageAspectFlags aspect);

    VulkanContext& ctx_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;

    VkImage colorImage_ = VK_NULL_HANDLE;
    VkDeviceMemory colorMemory_ = VK_NULL_HANDLE;
    VkImageView colorView_ = VK_NULL_HANDLE;
    VkImage depthImage_ = VK_NULL_HANDLE;
    VkDeviceMemory depthMemory_ = VK_NULL_HANDLE;
    VkImageView depthView_ = VK_NULL_HANDLE;
    VkFramebuffer framebuffer_ = VK_NULL_HANDLE;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    GpuBuffer stagingReadback_;
    std::vector<ResourceStateEvent> pendingTransitions_;
};

} // namespace skr
