#pragma once

#include "skinning_format.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace skr {

// One recorded resource-barrier event: which image changed layout and how.
struct ResourceStateEvent {
    std::string resource;
    std::string stageMask;
    std::string accessMask;
    std::string oldLayout;
    std::string newLayout;
};

// Draw-call level evidence captured around vkCmdDraw.
struct DrawCallRecord {
    uint32_t index = 0;
    uint32_t vertexCount = 0;
    uint32_t instanceCount = 0;
    uint32_t firstVertex = 0;
    uint32_t firstInstance = 0;
    VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    std::string pipelineName;
    std::string framebufferName;
    std::vector<std::string> vertexBindings; // buffer:offset:stride
    std::string indexBuffer;                 // offset, index type
    uint32_t skinMatrixCount = 0;
};

struct FramebufferRecord {
    std::string name;
    uint32_t width = 0;
    uint32_t height = 0;
    VkFormat colorFormat = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
    VkImageLayout finalColorLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    std::vector<ResourceStateEvent> transitions;
};

struct Snapshot {
    std::string label;                 // e.g. "legacy_v1" / "current_v2"
    FormatRevision revision = FormatRevision::Unknown;
    std::string objectName;
    std::string deviceName;
    uint64_t frameId = 0;
    FramebufferRecord framebuffer;
    DrawCallRecord drawCall;
    std::vector<ResourceStateEvent> allTransitions;

    // Raw RGBA8 readback, plus the evidence paths filled in by the driver.
    std::vector<uint8_t> pixels;
    uint32_t width = 0;
    uint32_t height = 0;
    std::string imagePath;
};

} // namespace skr
