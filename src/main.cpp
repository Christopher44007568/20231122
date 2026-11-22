// Geometry shader output checker.
// Captures GS-emitted vertices into a host-visible SSBO and verifies,
// per invocation: input vertices, generated primitives, max_vertices,
// and actual buffer writes. Supports pausing on a single invocation.

#include <vulkan/vulkan.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

namespace {

constexpr uint32_t kMaxVertices = 8;     // must match MAX_VERTS in capture.geom
constexpr uint32_t kInvocations = 4;     // must match INVOCATIONS in capture.geom
constexpr uint32_t kCapacity = 1024;     // capture record capacity
constexpr uint32_t kNoDrop = 0xffffffffu;

struct VertexRec {
    uint32_t invocation;
    uint32_t seq;
    uint32_t inputVerts;
    uint32_t pad;
    float pos[4];
};

struct CaptureHeader {
    uint32_t emittedTotal;
    uint32_t droppedTotal;
    uint32_t firstDropInv;
    uint32_t capacity;
};

struct PushConstants {
    uint32_t emitCount;
    uint32_t inspectOnly;
    uint32_t inspectInvocation;
    uint32_t maxVertices;
};

#define VK_CHECK(x)                                                     \
    do {                                                                \
        VkResult r_ = (x);                                              \
        if (r_ != VK_SUCCESS) {                                         \
            std::fprintf(stderr, "VK_CHECK failed (%d) at %s:%d\n",     \
                         (int)r_, __FILE__, __LINE__);                  \
            std::exit(1);                                               \
        }                                                               \
    } while (0)

std::vector<char> readFile(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); std::exit(1); }
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<char> data(n);
    if (std::fread(data.data(), 1, n, f) != (size_t)n) { std::exit(1); }
    std::fclose(f);
    return data;
}

struct VulkanCtx {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice phys = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout descLayout = VK_NULL_HANDLE;
    VkDescriptorPool descPool = VK_NULL_HANDLE;
    VkDescriptorSet descSet = VK_NULL_HANDLE;
    VkPipeline pipelines[3] = {};
    VkBuffer vertexBuf = VK_NULL_HANDLE;
    VkDeviceMemory vertexMem = VK_NULL_HANDLE;
    VkBuffer captureBuf = VK_NULL_HANDLE;
    VkDeviceMemory captureMem = VK_NULL_HANDLE;
    void* captureMapped = nullptr;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
};

uint32_t findMemoryType(VkPhysicalDevice phys, uint32_t typeBits,
                        VkMemoryPropertyFlags props) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; ++i)
        if ((typeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & props) == props)
            return i;
    std::fprintf(stderr, "no suitable memory type\n");
    std::exit(1);
}

void createBuffer(VulkanCtx& c, VkDeviceSize size, VkBufferUsageFlags usage,
                  VkMemoryPropertyFlags props, VkBuffer& buf,
                  VkDeviceMemory& mem) {
    VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bi.size = size;
    bi.usage = usage;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(c.device, &bi, nullptr, &buf));
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(c.device, buf, &req);
    VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    ai.allocationSize = req.size;
    ai.memoryTypeIndex = findMemoryType(c.phys, req.memoryTypeBits, props);
    VK_CHECK(vkAllocateMemory(c.device, &ai, nullptr, &mem));
    VK_CHECK(vkBindBufferMemory(c.device, buf, mem, 0));
}

void initInstanceAndDevice(VulkanCtx& c) {
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "gsochecker";
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ii.pApplicationInfo = &app;
    VK_CHECK(vkCreateInstance(&ii, nullptr, &c.instance));

    uint32_t n = 0;
    vkEnumeratePhysicalDevices(c.instance, &n, nullptr);
    if (n == 0) { std::fprintf(stderr, "no Vulkan devices\n"); std::exit(1); }
    std::vector<VkPhysicalDevice> devs(n);
    vkEnumeratePhysicalDevices(c.instance, &n, devs.data());
    c.phys = devs[0];
    VkPhysicalDeviceProperties dp;
    vkGetPhysicalDeviceProperties(c.phys, &dp);
    std::printf("[info] device: %s\n", dp.deviceName);

    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qp(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(c.phys, &qn, qp.data());
    for (uint32_t i = 0; i < qn; ++i)
        if (qp[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { c.queueFamily = i; break; }

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qi.queueFamilyIndex = c.queueFamily;
    qi.queueCount = 1;
    qi.pQueuePriorities = &prio;
    VkPhysicalDeviceFeatures feats{};
    feats.geometryShader = VK_TRUE;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.pEnabledFeatures = &feats;
    VK_CHECK(vkCreateDevice(c.phys, &di, nullptr, &c.device));
    vkGetDeviceQueue(c.device, c.queueFamily, 0, &c.queue);

    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pi.queueFamilyIndex = c.queueFamily;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VK_CHECK(vkCreateCommandPool(c.device, &pi, nullptr, &c.cmdPool));
}

void initRenderPassAndPipelines(VulkanCtx& c) {
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 0;
    sub.pDepthStencilAttachment = nullptr;
    VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    ri.attachmentCount = 0;
    ri.subpassCount = 1;
    ri.pSubpasses = &sub;
    VK_CHECK(vkCreateRenderPass(c.device, &ri, nullptr, &c.renderPass));

    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = c.renderPass;
    fi.attachmentCount = 0;
    fi.width = 64;
    fi.height = 64;
    fi.layers = 1;
    VK_CHECK(vkCreateFramebuffer(c.device, &fi, nullptr, &c.framebuffer));

    VkPushConstantRange pcr{VK_SHADER_STAGE_GEOMETRY_BIT, 0, sizeof(PushConstants)};
    VkDescriptorSetLayoutBinding bind{};
    bind.binding = 0;
    bind.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bind.descriptorCount = 1;
    bind.stageFlags = VK_SHADER_STAGE_GEOMETRY_BIT;
    VkDescriptorSetLayoutCreateInfo dli{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dli.bindingCount = 1;
    dli.pBindings = &bind;
    VK_CHECK(vkCreateDescriptorSetLayout(c.device, &dli, nullptr, &c.descLayout));

    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &c.descLayout;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges = &pcr;
    VK_CHECK(vkCreatePipelineLayout(c.device, &pli, nullptr, &c.pipelineLayout));

    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1};
    VkDescriptorPoolCreateInfo dpi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dpi.maxSets = 1;
    dpi.poolSizeCount = 1;
    dpi.pPoolSizes = &ps;
    VK_CHECK(vkCreateDescriptorPool(c.device, &dpi, nullptr, &c.descPool));
    VkDescriptorSetAllocateInfo dai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    dai.descriptorPool = c.descPool;
    dai.descriptorSetCount = 1;
    dai.pSetLayouts = &c.descLayout;
    VK_CHECK(vkAllocateDescriptorSets(c.device, &dai, &c.descSet));

    auto vertCode = readFile(std::string(SPV_DIR) + "/passthrough.vert.spv");
    VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    si.codeSize = vertCode.size();
    si.pCode = (const uint32_t*)vertCode.data();
    VkShaderModule vertMod;
    VK_CHECK(vkCreateShaderModule(c.device, &si, nullptr, &vertMod));

    VkVertexInputBindingDescription vbind{0, sizeof(float) * 4, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription vattr{0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, 0};

    static const VkPrimitiveTopology topos[3] = {
        VK_PRIMITIVE_TOPOLOGY_POINT_LIST,
        VK_PRIMITIVE_TOPOLOGY_LINE_LIST,
        VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    static const char* topoNames[3] = {"points", "lines", "triangles"};

    for (int t = 0; t < 3; ++t) {
        char path[256];
        std::snprintf(path, sizeof(path), "%s/capture_topo%d.geom.spv", SPV_DIR, t);
        auto geomCode = readFile(path);
        VkShaderModuleCreateInfo gi{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        gi.codeSize = geomCode.size();
        gi.pCode = (const uint32_t*)geomCode.data();
        VkShaderModule geomMod;
        VK_CHECK(vkCreateShaderModule(c.device, &gi, nullptr, &geomMod));

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        stages[1].module = geomMod;
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vin{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vin.vertexBindingDescriptionCount = 1;
        vin.pVertexBindingDescriptions = &vbind;
        vin.vertexAttributeDescriptionCount = 1;
        vin.pVertexAttributeDescriptions = &vattr;
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = topos[t];
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.rasterizerDiscardEnable = VK_TRUE;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2;
        dyn.pDynamicStates = dynStates;

        VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        pi.stageCount = 2;
        pi.pStages = stages;
        pi.pVertexInputState = &vin;
        pi.pInputAssemblyState = &ia;
        pi.pViewportState = &vp;
        pi.pRasterizationState = &rs;
        pi.pMultisampleState = &ms;
        pi.pDynamicState = &dyn;
        pi.layout = c.pipelineLayout;
        pi.renderPass = c.renderPass;
        VK_CHECK(vkCreateGraphicsPipelines(c.device, VK_NULL_HANDLE, 1, &pi,
                                           nullptr, &c.pipelines[t]));
        vkDestroyShaderModule(c.device, geomMod, nullptr);
        std::printf("[info] pipeline ready: %s\n", topoNames[t]);
    }
    vkDestroyShaderModule(c.device, vertMod, nullptr);
}

void initBuffersAndCommands(VulkanCtx& c) {
    // One input primitive worth of vertices (3 max); cases use what they need.
    float verts[3][4] = {
        {0.0f, 0.0f, 0.0f, 1.0f},
        {0.5f, 0.0f, 0.0f, 1.0f},
        {0.0f, 0.5f, 0.0f, 1.0f},
    };
    createBuffer(c, sizeof(verts), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 c.vertexBuf, c.vertexMem);
    void* p = nullptr;
    VK_CHECK(vkMapMemory(c.device, c.vertexMem, 0, sizeof(verts), 0, &p));
    std::memcpy(p, verts, sizeof(verts));
    vkUnmapMemory(c.device, c.vertexMem);

    VkDeviceSize capSize = sizeof(CaptureHeader) + sizeof(VertexRec) * kCapacity;
    createBuffer(c, capSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 c.captureBuf, c.captureMem);
    VK_CHECK(vkMapMemory(c.device, c.captureMem, 0, capSize, 0, &c.captureMapped));

    VkDescriptorBufferInfo dbi{c.captureBuf, 0, VK_WHOLE_SIZE};
    VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    w.dstSet = c.descSet;
    w.dstBinding = 0;
    w.descriptorCount = 1;
    w.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    w.pBufferInfo = &dbi;
    vkUpdateDescriptorSets(c.device, 1, &w, 0, nullptr);

    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = c.cmdPool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VK_CHECK(vkAllocateCommandBuffers(c.device, &cai, &c.cmd));
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VK_CHECK(vkCreateFence(c.device, &fci, nullptr, &c.fence));
}

// Runs one capture pass and returns the header; records are read from the
// mapped buffer afterwards.
CaptureHeader runCase(VulkanCtx& c, int topo, uint32_t inputVertCount,
                      const PushConstants& pc) {
    auto* hdr = (CaptureHeader*)c.captureMapped;
    hdr->emittedTotal = 0;
    hdr->droppedTotal = 0;
    hdr->firstDropInv = kNoDrop;
    hdr->capacity = kCapacity;

    VK_CHECK(vkResetCommandBuffer(c.cmd, 0));
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VK_CHECK(vkBeginCommandBuffer(c.cmd, &bi));
    VkRenderPassBeginInfo rbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = c.renderPass;
    rbi.framebuffer = c.framebuffer;
    rbi.renderArea = {{0, 0}, {64, 64}};
    vkCmdBeginRenderPass(c.cmd, &rbi, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, c.pipelines[topo]);
    vkCmdBindDescriptorSets(c.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            c.pipelineLayout, 0, 1, &c.descSet, 0, nullptr);
    vkCmdPushConstants(c.cmd, c.pipelineLayout, VK_SHADER_STAGE_GEOMETRY_BIT,
                       0, sizeof(pc), &pc);
    VkDeviceSize off = 0;
    vkCmdBindVertexBuffers(c.cmd, 0, 1, &c.vertexBuf, &off);
    vkCmdDraw(c.cmd, inputVertCount, 1, 0, 0);
    vkCmdEndRenderPass(c.cmd);
    VK_CHECK(vkEndCommandBuffer(c.cmd));

    VkSubmitInfo sub{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    sub.commandBufferCount = 1;
    sub.pCommandBuffers = &c.cmd;
    VK_CHECK(vkQueueSubmit(c.queue, 1, &sub, c.fence));
    VK_CHECK(vkWaitForFences(c.device, 1, &c.fence, VK_TRUE, UINT64_MAX));
    VK_CHECK(vkResetFences(c.device, 1, &c.fence));
    return *hdr;
}

uint32_t primsFor(uint32_t topo, uint32_t verts) {
    if (topo == 0) return verts;                    // points
    if (topo == 1) return verts > 0 ? verts - 1 : 0; // line strip
    return verts > 1 ? verts - 2 : 0;                // triangle strip
}

struct CaseResult {
    uint32_t perInvEmitted[kInvocations] = {};
    uint32_t perInvInput[kInvocations] = {};
    uint32_t totalEmitted = 0;
    uint32_t totalPrims = 0;
    uint32_t dropped = 0;
    uint32_t firstDropInv = kNoDrop;
    int firstAnomalousInv = -1;
};

CaseResult analyze(const CaptureHeader& hdr, const VertexRec* recs,
                   uint32_t topo, uint32_t emitCount, uint32_t maxVertices,
                   uint32_t inputVertCount) {
    CaseResult r;
    r.totalEmitted = hdr.emittedTotal;
    r.dropped = hdr.droppedTotal;
    r.firstDropInv = hdr.firstDropInv;
    uint32_t stored = std::min(hdr.emittedTotal, kCapacity);
    for (uint32_t i = 0; i < stored; ++i) {
        uint32_t inv = recs[i].invocation;
        if (inv < kInvocations) {
            r.perInvEmitted[inv]++;
            r.perInvInput[inv] = recs[i].inputVerts;
        }
    }
    uint32_t expected = std::min(emitCount, maxVertices);
    for (uint32_t inv = 0; inv < kInvocations; ++inv) {
        if (r.perInvInput[inv] == 0)
            r.perInvInput[inv] = inputVertCount; // no record written (zero output)
        r.totalPrims += primsFor(topo, r.perInvEmitted[inv]);
        bool bad = (r.perInvEmitted[inv] != expected) ||
                   (r.firstDropInv == inv);
        if (bad && r.firstAnomalousInv < 0)
            r.firstAnomalousInv = (int)inv;
    }
    return r;
}

void printCaseReport(const char* topoName, const char* scenario, uint32_t topo,
                     uint32_t emitCount, uint32_t maxVertices,
                     const CaseResult& r) {
    std::printf("\n=== topo=%s scenario=%s emit=%u max_vertices=%u ===\n",
                topoName, scenario, emitCount, maxVertices);
    std::printf(" inv | input verts | requested | max_vertices | captured | prims\n");
    for (uint32_t inv = 0; inv < kInvocations; ++inv) {
        bool anomaly = (int)inv == r.firstAnomalousInv;
        std::printf(" %3u | %11u | %9u | %12u | %8u | %5u%s\n",
                    inv, r.perInvInput[inv], emitCount, maxVertices,
                    r.perInvEmitted[inv], primsFor(topo, r.perInvEmitted[inv]),
                    anomaly ? "  <-- first anomalous" : "");
    }
    uint64_t usedBytes = (uint64_t)r.totalEmitted * sizeof(VertexRec);
    uint64_t capBytes = (uint64_t)kCapacity * sizeof(VertexRec);
    std::printf(" summary: emitted=%u prims=%u dropped=%u buffer=%llu/%llu bytes (%.1f%%)\n",
                r.totalEmitted, r.totalPrims, r.dropped,
                (unsigned long long)std::min<uint64_t>(usedBytes, capBytes),
                (unsigned long long)capBytes,
                100.0 * (double)std::min<uint64_t>(usedBytes, capBytes) / capBytes);
    if (r.firstAnomalousInv >= 0)
        std::printf(" first anomalous invocation: %d\n", r.firstAnomalousInv);
    else
        std::printf(" first anomalous invocation: none\n");
    if (emitCount > maxVertices)
        std::printf(" note: requested emit (%u) exceeds max_vertices (%u);"
                    " primitives beyond the limit are dropped/undefined\n",
                    emitCount, maxVertices);
    if (r.dropped > 0)
        std::printf(" note: capture buffer full, %u record(s) not stored"
                    " (first drop at invocation %u)\n", r.dropped, r.firstDropInv);
}

// Pause mode: re-run with only one invocation enabled and dump its records.
void inspectInvocation(VulkanCtx& c, uint32_t topo, uint32_t inputVertCount,
                       uint32_t emitCount, uint32_t inv) {
    PushConstants pc{emitCount, 1u, inv, kMaxVertices};
    CaptureHeader hdr = runCase(c, (int)topo, inputVertCount, pc);
    auto* recs = (const VertexRec*)((const char*)c.captureMapped + sizeof(CaptureHeader));
    uint32_t stored = std::min(hdr.emittedTotal, kCapacity);
    std::printf("\n--- paused at invocation %u (emit=%u) ---\n", inv, emitCount);
    std::printf(" seq | input verts | position\n");
    for (uint32_t i = 0; i < stored; ++i) {
        std::printf(" %3u | %11u | (%.2f %.2f %.2f %.2f)\n",
                    recs[i].seq, recs[i].inputVerts,
                    recs[i].pos[0], recs[i].pos[1], recs[i].pos[2], recs[i].pos[3]);
    }
    std::printf(" invocation %u: %u vertices, %u primitives\n",
                inv, hdr.emittedTotal, primsFor(topo, hdr.emittedTotal));
}

void usage(const char* argv0) {
    std::printf(
        "usage: %s [--topo points|lines|triangles|all] [--emit N]\n"
        "          [--inspect K] [--non-interactive]\n"
        "default: run zero/at-limit/over-limit scenarios for all topologies,\n"
        "then pause on the first anomalous invocation found.\n", argv0);
}

} // namespace

int main(int argc, char** argv) {
    std::string topoArg = "all";
    int emitOverride = -1;
    int inspectArg = -1;
    bool interactive = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--topo" && i + 1 < argc) topoArg = argv[++i];
        else if (a == "--emit" && i + 1 < argc) emitOverride = std::atoi(argv[++i]);
        else if (a == "--inspect" && i + 1 < argc) inspectArg = std::atoi(argv[++i]);
        else if (a == "--non-interactive") interactive = false;
        else if (a == "--help") { usage(argv[0]); return 0; }
        else { usage(argv[0]); return 1; }
    }

    // GLFW window (Null platform when no window system is available); the
    // event loop is pumped while paused on an invocation.
    glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
    if (glfwInit() != GLFW_TRUE) {
        std::fprintf(stderr, "glfwInit failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = glfwCreateWindow(64, 64, "gsochecker", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "glfwCreateWindow failed\n");
        glfwTerminate();
        return 1;
    }

    VulkanCtx c;
    initInstanceAndDevice(c);
    initRenderPassAndPipelines(c);
    initBuffersAndCommands(c);

    static const char* topoNames[3] = {"points", "lines", "triangles"};
    static const uint32_t inputVerts[3] = {1, 2, 3};
    struct Scenario { const char* name; uint32_t emit; };
    Scenario scenarios[3] = {
        {"zero-output", 0},
        {"at-limit", kMaxVertices},
        {"over-limit", kMaxVertices + 3},
    };
    if (emitOverride >= 0)
        scenarios[0] = {"custom", (uint32_t)emitOverride};
    int scenarioCount = emitOverride >= 0 ? 1 : 3;

    int topoBegin = 0, topoEnd = 3;
    if (topoArg == "points") { topoBegin = 0; topoEnd = 1; }
    else if (topoArg == "lines") { topoBegin = 1; topoEnd = 2; }
    else if (topoArg == "triangles") { topoBegin = 2; topoEnd = 3; }
    else if (topoArg != "all") { usage(argv[0]); return 1; }

    int firstBadTopo = -1, firstBadInv = -1;
    uint32_t firstBadEmit = 0;
    for (int t = topoBegin; t < topoEnd; ++t) {
        for (int s = 0; s < scenarioCount; ++s) {
            PushConstants pc{scenarios[s].emit, 0u, 0u, kMaxVertices};
            CaptureHeader hdr = runCase(c, t, inputVerts[t], pc);
            auto* recs = (const VertexRec*)((const char*)c.captureMapped +
                                            sizeof(CaptureHeader));
            CaseResult r = analyze(hdr, recs, (uint32_t)t, scenarios[s].emit,
                                   kMaxVertices, inputVerts[t]);
            printCaseReport(topoNames[t], scenarios[s].name, (uint32_t)t,
                            scenarios[s].emit, kMaxVertices, r);
            if (r.firstAnomalousInv >= 0 && firstBadTopo < 0) {
                firstBadTopo = t;
                firstBadInv = r.firstAnomalousInv;
                firstBadEmit = scenarios[s].emit;
            }
        }
    }

    if (inspectArg >= 0) {
        int t = topoBegin;
        uint32_t emit = emitOverride >= 0 ? (uint32_t)emitOverride : kMaxVertices;
        inspectInvocation(c, (uint32_t)t, inputVerts[t], emit, (uint32_t)inspectArg);
    } else if (firstBadTopo >= 0) {
        std::printf("\n[check] pausing on first anomalous invocation: topo=%s inv=%d\n",
                    topoNames[firstBadTopo], firstBadInv);
        inspectInvocation(c, (uint32_t)firstBadTopo, inputVerts[firstBadTopo],
                          firstBadEmit, (uint32_t)firstBadInv);
    }

    if (interactive) {
        std::printf("\npress Enter to exit ");
        std::fflush(stdout);
        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();
            int ch = std::getchar();
            if (ch == '\n' || ch == EOF) break;
        }
        std::printf("\n");
    }

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}


