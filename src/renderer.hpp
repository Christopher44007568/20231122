#pragma once

#include "selection.hpp"
#include "vk_context.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace inst {

struct TransitionRecord {
    std::string resource;
    std::string oldLayout;
    std::string newLayout;
    std::string stage;
    std::string access;
};

struct DrawRecord {
    uint32_t index = 0;
    uint32_t model = 0;
    std::string modelName;
    uint32_t indexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstIndex = 0;
    uint32_t firstInstance = 0;
    std::string vertexBuffer;
    std::string indexBuffer;
    std::string instanceBuffer;
};

struct RenderSnapshot {
    std::string label;
    std::string policy;
    std::string order;
    std::string selectionHash;
    std::string drawOrderHash;
    std::string deviceName;
    std::string imageHash;
    std::string imagePath;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<SelectedInstance> instances;
    std::vector<DrawRecord> draws;
    std::vector<TransitionRecord> transitions;
    std::vector<uint8_t> rgba;
};

class InstanceRenderer {
public:
    InstanceRenderer(VulkanContext& context,
                     const fs::path& vertexShader,
                     const fs::path& fragmentShader);
    ~InstanceRenderer();

    InstanceRenderer(const InstanceRenderer&) = delete;
    InstanceRenderer& operator=(const InstanceRenderer&) = delete;

    RenderSnapshot render(const SelectionResult& selection,
                          const std::string& label);

private:
    struct MeshRange {
        uint32_t firstVertex = 0;
        uint32_t vertexCount = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
    };

    VulkanContext& context_;
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
    VkBufferResource meshBuffer_;
    VkBufferResource indexBuffer_;
    VkBufferResource readbackBuffer_;
    VkImageLayout colorLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImageLayout depthLayout_ = VK_IMAGE_LAYOUT_UNDEFINED;
    MeshRange meshes_[3];
    std::vector<TransitionRecord> transitions_;

    void createRenderTarget();
    void createMeshCatalog();
    void createPipeline(const fs::path& vertexShader,
                        const fs::path& fragmentShader);
    void transitionColor(VkCommandBuffer commandBuffer,
                         VkImageLayout newLayout,
                         const std::string& resource);
    void transitionDepth(VkCommandBuffer commandBuffer);
};

} // namespace inst
