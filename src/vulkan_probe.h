#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace probe {

struct VersionSnapshot {
    std::string source;       // where this snapshot came from
    uint32_t apiVersion = 0;  // raw VK_MAKE_VERSION value
    uint32_t driverVersion = 0;
    std::string deviceName;   // empty for instance-level snapshots
    std::string capturedAt;   // ISO-8601 UTC
};

struct ExtensionRecord {
    std::string name;
    uint32_t specVersion = 0;
};

struct MsaaEvidence {
    uint32_t colorSampleCounts = 0;  // raw VkSampleCountFlags bitmask
    uint32_t depthSampleCounts = 0;
    uint32_t requestedCount = 0;     // sample count under evaluation
};

struct ProbeResult {
    bool glfwVulkanSupported = false;
    std::string glfwVersion;
    std::vector<std::string> requiredInstanceExtensions;
    VersionSnapshot instanceSnapshot;              // snapshot A
    std::vector<VersionSnapshot> deviceSnapshots;  // snapshot B (per device)
    std::vector<ExtensionRecord> deviceExtensions; // first device
    MsaaEvidence msaa;                             // first device
};

// Initializes GLFW (Null platform), creates a Vulkan instance, enumerates
// physical devices, and fills ProbeResult. Throws std::runtime_error on
// hard failure. Always cleans up Vulkan/GLFW state before returning.
ProbeResult runProbe();

std::string formatVersion(uint32_t v);

} // namespace probe
