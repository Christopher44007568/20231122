#include "vk_util.h"

#include <fstream>

uint32_t findMemoryType(const Gpu& gpu, uint32_t typeBits, VkMemoryPropertyFlags props) {
    for (uint32_t i = 0; i < gpu.memProps.memoryTypeCount; ++i) {
        if ((typeBits & (1u << i)) &&
            (gpu.memProps.memoryTypes[i].propertyFlags & props) == props)
            return i;
    }
    std::fprintf(stderr, "no suitable memory type\n");
    std::exit(1);
}

Buffer createBuffer(const Gpu& gpu, VkDeviceSize size, VkBufferUsageFlags usage,
                    VkMemoryPropertyFlags props) {
    Buffer b;
    b.size = size;
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(gpu.device, &bi, nullptr, &b.buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(gpu.device, b.buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(gpu.device, &ai, nullptr, &b.mem));
    VK_CHECK(vkBindBufferMemory(gpu.device, b.buf, b.mem, 0));
    return b;
}

void destroyBuffer(const Gpu& gpu, Buffer& b) {
    if (b.buf) vkDestroyBuffer(gpu.device, b.buf, nullptr);
    if (b.mem) vkFreeMemory(gpu.device, b.mem, nullptr);
    b = {};
}

Image createImage2D(const Gpu& gpu, uint32_t w, uint32_t h, VkFormat format,
                    VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    Image im;
    im.format = format;
    im.width = w;
    im.height = h;
    VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ii.imageType = VK_IMAGE_TYPE_2D;
    ii.format = format;
    ii.extent = {w, h, 1};
    ii.mipLevels = 1;
    ii.arrayLayers = 1;
    ii.samples = VK_SAMPLE_COUNT_1_BIT;
    ii.tiling = VK_IMAGE_TILING_OPTIMAL;
    ii.usage = usage;
    ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VK_CHECK(vkCreateImage(gpu.device, &ii, nullptr, &im.img));
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(gpu.device, im.img, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(gpu, req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VK_CHECK(vkAllocateMemory(gpu.device, &ai, nullptr, &im.mem));
    VK_CHECK(vkBindImageMemory(gpu.device, im.img, im.mem, 0));
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = im.img;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format;
    vi.subresourceRange = {aspect, 0, 1, 0, 1};
    VK_CHECK(vkCreateImageView(gpu.device, &vi, nullptr, &im.view));
    return im;
}

void destroyImage(const Gpu& gpu, Image& im) {
    if (im.view) vkDestroyImageView(gpu.device, im.view, nullptr);
    if (im.img) vkDestroyImage(gpu.device, im.img, nullptr);
    if (im.mem) vkFreeMemory(gpu.device, im.mem, nullptr);
    im = {};
}

VkShaderModule loadShaderModule(VkDevice device, const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "cannot open shader %s\n", path.c_str());
        std::exit(1);
    }
    std::vector<char> data((size_t)f.tellg());
    f.seekg(0);
    f.read(data.data(), (std::streamsize)data.size());
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = data.size();
    ci.pCode = reinterpret_cast<const uint32_t*>(data.data());
    VkShaderModule mod;
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}

void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageAspectFlags aspect,
                     VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange = {aspect, 0, 1, 0, 1};
    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

VkCommandBuffer beginOneShot(VkDevice device, VkCommandPool pool) {
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ai.commandPool = pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = 1;
    VkCommandBuffer cmd;
    VK_CHECK(vkAllocateCommandBuffers(device, &ai, &cmd));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &bi));
    return cmd;
}

void endOneShot(VkDevice device, VkCommandPool pool, VkQueue queue, VkCommandBuffer cmd) {
    VK_CHECK(vkEndCommandBuffer(cmd));
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cmd;
    VK_CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));
    vkFreeCommandBuffers(device, pool, 1, &cmd);
}
