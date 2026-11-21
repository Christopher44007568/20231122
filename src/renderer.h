#pragma once

#include "scene.h"
#include "vk_util.h"

#include <string>
#include <vector>

struct GLFWwindow;

// Must match `BrokenPixel` in shaders/diag.frag (std430).
struct DiagRecord {
    uint32_t x, y, flags, pad;
    float depthDiff, normalDiff;
    float depthThreshold, normalThreshold;
    float dz[4]; // L, R, D, U
    float dn[4]; // L, R, D, U
};
static_assert(sizeof(DiagRecord) == 64, "DiagRecord layout mismatch");

struct FrameParams {
    int sceneIndex = 0;
    float depthThreshold = 0.03f;
    float normalThreshold = 0.25f;
    float gtDepthEps = 0.004f; // second-difference of 1/viewZ
    float gtNormalEps = 0.02f;
    bool runDiagnostics = false;
};

class Renderer {
public:
    // window == nullptr selects headless mode (no surface/swapchain).
    void init(GLFWwindow* window, uint32_t width, uint32_t height);
    void shutdown();

    void uploadScene(const SceneSet& scenes);
    // Renders one frame. In headless mode the composited 2x2 debug view is
    // kept in an offscreen image for readback.
    void renderFrame(const SceneSet& scenes, const FrameParams& params);

    // Valid after renderFrame with runDiagnostics=true.
    const std::vector<DiagRecord>& diagRecords() const { return m_diagRecords; }
    // Saves the last composited frame as binary PPM (headless mode only).
    void saveFramePPM(const std::string& path);

    bool windowShouldClose() const;
    uint32_t width() const { return m_width; }
    uint32_t height() const { return m_height; }

private:
    void createInstance(GLFWwindow* window);
    void pickDevice();
    void createSwapchain();
    void destroySwapchain();
    void createTargets();
    void destroyTargets();
    void createPipelines();
    VkCommandBuffer recordFrame(const SceneSet& scenes, const FrameParams& params,
                                VkFramebuffer displayFB);
    void readbackDiagnostics();

    GLFWwindow* m_window = nullptr;
    bool m_headless = false;
    uint32_t m_width = 0, m_height = 0;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    Gpu m_gpu;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;

    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkFormat m_swapFormat = VK_FORMAT_UNDEFINED;
    std::vector<VkImage> m_swapImages;
    std::vector<VkImageView> m_swapViews;
    std::vector<VkFramebuffer> m_swapFbs;
    VkSemaphore m_semAcquire = VK_NULL_HANDLE;
    VkSemaphore m_semDone = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;

    Image m_gbuf;     // rgba16f: view normal + view depth
    Image m_depth;    // d32
    Image m_metrics;  // rgba16f: depthDiff, normalDiff, boundary, mask
    Image m_display;  // headless only: composited 2x2 view

    VkRenderPass m_geomRP = VK_NULL_HANDLE;
    VkRenderPass m_edgeRP = VK_NULL_HANDLE;
    VkRenderPass m_diagRP = VK_NULL_HANDLE;
    VkRenderPass m_displayRP = VK_NULL_HANDLE;
    VkFramebuffer m_geomFB = VK_NULL_HANDLE;
    VkFramebuffer m_edgeFB = VK_NULL_HANDLE;
    VkFramebuffer m_diagFB = VK_NULL_HANDLE;
    VkFramebuffer m_displayFB = VK_NULL_HANDLE;

    VkSampler m_sampler = VK_NULL_HANDLE;

    VkPipelineLayout m_geomPL = VK_NULL_HANDLE;
    VkPipeline m_geomPipe = VK_NULL_HANDLE;
    VkPipelineLayout m_edgePL = VK_NULL_HANDLE;
    VkPipeline m_edgePipe = VK_NULL_HANDLE;
    VkPipelineLayout m_diagPL = VK_NULL_HANDLE;
    VkPipeline m_diagPipe = VK_NULL_HANDLE;
    VkPipelineLayout m_displayPL = VK_NULL_HANDLE;
    VkPipeline m_displayPipe = VK_NULL_HANDLE;

    VkDescriptorPool m_descPool = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_edgeDSL = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_diagDSL = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_displayDSL = VK_NULL_HANDLE;
    VkDescriptorSet m_edgeDS = VK_NULL_HANDLE;
    VkDescriptorSet m_diagDS = VK_NULL_HANDLE;
    VkDescriptorSet m_displayDS = VK_NULL_HANDLE;

    Buffer m_diagBuf; // host-visible SSBO
    static constexpr uint32_t kMaxDiagRecords = 65536;
    std::vector<DiagRecord> m_diagRecords;

    Buffer m_cubeVB, m_cubeIB, m_planeVB, m_planeIB;
    uint32_t m_cubeIndexCount = 0, m_planeIndexCount = 0;

    Buffer m_readback; // headless frame readback
};
