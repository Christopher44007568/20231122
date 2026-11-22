#pragma once

#include "image.hpp"
#include "model.hpp"

#include <cstdint>
#include <set>
#include <string>
#include <vector>

namespace rr {

struct DeviceInfo {
    std::string name;
    std::string driverName;
    std::string driverInfo;
    std::uint32_t apiVersion = 0;
    std::uint32_t driverVersion = 0;
    std::uint32_t vendorId = 0;
    std::uint32_t deviceId = 0;
    std::uint32_t queueFamily = 0;
    std::string deviceType;
    bool wideLines = false;
    bool fillModeNonSolid = false;
    bool depthClamp = false;
    std::uint32_t maxFramebufferWidth = 0;
    std::uint32_t maxFramebufferHeight = 0;
    float lineWidthRange[2] = {1.0F, 1.0F};
    std::string vulkanLoaderVersion;
};

struct DrawRender {
    std::size_t sequence = 0;
    std::string id;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::string beforeSha256;
    std::string afterSha256;
    DiffStats change;
    bool hasSavedImage = false;
    Image savedImage;
};

struct RenderPassResult {
    bool ok = false;
    std::string error;
    std::string passId;
    std::string ruleId;
    Rule effectiveRule;
    std::vector<std::string> warnings;
    Image initialImage;
    Image finalImage;
    std::string initialSha256;
    std::string finalSha256;
    std::vector<DrawRender> draws;
    std::uint64_t elapsedMicroseconds = 0;
};

struct RenderOptions {
    std::set<std::size_t> savedDrawIndices;
    bool enableValidation = false;
    int preferredDeviceIndex = -1;
    std::string preferredDeviceName;
};

class VulkanRenderer {
public:
    struct Impl;

    VulkanRenderer();
    ~VulkanRenderer();

    VulkanRenderer(const VulkanRenderer&) = delete;
    VulkanRenderer& operator=(const VulkanRenderer&) = delete;

    bool initialize(const RenderOptions& options, std::string& error);
    RenderPassResult renderPass(const Scenario& scenario,
                                const Pass& pass,
                                const RenderOptions& options);

    const DeviceInfo& deviceInfo() const;
    bool initialized() const;

private:
    Impl* impl_ = nullptr;
};

std::string hexColor(const std::array<std::uint8_t, 4>& rgba);
std::string vulkanVersionString(std::uint32_t version);

}  // namespace rr
