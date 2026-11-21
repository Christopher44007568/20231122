#include "page_surface.hpp"

#include <cstring>
#include <stdexcept>

namespace gcdump::gui {

namespace {
void require(VkResult r, const char* what) {
    if (r != VK_SUCCESS)
        throw std::runtime_error(std::string("page surface: ") + what);
}

void barrier(VkCommandBuffer cmd, VkImage image,
             VkImageLayout oldLayout, VkImageLayout newLayout,
             VkAccessFlags srcAccess, VkAccessFlags dstAccess,
             VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    b.srcAccessMask = srcAccess;
    b.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0,
                         nullptr, 1, &b);
}
} // namespace

PageSurface::PageSurface(VkPhysicalDevice physical, VkDevice device)
    : physical_(physical), device_(device) {}

PageSurface::~PageSurface() { destroyImage(); }

void PageSurface::destroyImage() {
    if (image_) vkDestroyImage(device_, image_, nullptr);
    if (memory_) vkFreeMemory(device_, memory_, nullptr);
    image_ = VK_NULL_HANDLE;
    memory_ = VK_NULL_HANDLE;
}

uint32_t PageSurface::findMemoryType(uint32_t typeBits,
                                     VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    throw std::runtime_error("no host-visible image memory type");
}

void PageSurface::ensureImage(uint32_t width, uint32_t height) {
    if (image_ && width_ == width && height_ == height) return;
    destroyImage();
    width_ = width;
    height_ = height;

    VkImageCreateInfo ci{};
    ci.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = VK_FORMAT_R8G8B8A8_UNORM;
    ci.extent = {width, height, 1};
    ci.mipLevels = 1;
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_LINEAR;
    ci.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    ci.initialLayout = VK_IMAGE_LAYOUT_PREINITIALIZED;
    require(vkCreateImage(device_, &ci, nullptr, &image_), "vkCreateImage");

    VkMemoryRequirements req{};
    vkGetImageMemoryRequirements(device_, image_, &req);
    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(
        req.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    require(vkAllocateMemory(device_, &ai, nullptr, &memory_),
            "vkAllocateMemory");
    require(vkBindImageMemory(device_, image_, memory_, 0),
            "vkBindImageMemory");
}

void PageSurface::upload(const uint8_t* rgba, uint32_t width,
                         uint32_t height) {
    ensureImage(width, height);
    VkImageSubresource sub{};
    sub.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    VkSubresourceLayout layout{};
    vkGetImageSubresourceLayout(device_, image_, &sub, &layout);

    void* mapped = nullptr;
    require(vkMapMemory(device_, memory_, 0, VK_WHOLE_SIZE, 0, &mapped),
            "vkMapMemory");
    auto* dst = static_cast<uint8_t*>(mapped);
    for (uint32_t row = 0; row < height; ++row) {
        std::memcpy(dst + layout.offset + row * layout.rowPitch,
                    rgba + static_cast<size_t>(row) * width * 4,
                    static_cast<size_t>(width) * 4);
    }
    vkUnmapMemory(device_, memory_);
}

void PageSurface::blitTo(VkCommandBuffer cmd, VkImage target,
                         VkExtent2D targetExtent) {
    barrier(cmd, image_, VK_IMAGE_LAYOUT_PREINITIALIZED,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_HOST_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(cmd, target, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.srcOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {static_cast<int32_t>(width_),
                          static_cast<int32_t>(height_), 1};
    blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstOffsets[0] = {0, 0, 0};
    blit.dstOffsets[1] = {static_cast<int32_t>(targetExtent.width),
                          static_cast<int32_t>(targetExtent.height), 1};
    vkCmdBlitImage(cmd, image_,
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, target,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   VK_FILTER_LINEAR);

    barrier(cmd, target, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

} // namespace gcdump::gui
