#pragma once

#include "cache_format.hpp"
#include "image.hpp"
#include "vulkan_review.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace lshc {

struct ObjectReview {
    uint32_t objectId = 0;
    std::string status;
    std::optional<ShadowRecord> legacyRecord;
    std::optional<ShadowRecord> currentRecord;
    std::optional<RenderEvidence> legacyRender;
    std::optional<RenderEvidence> currentRender;
    std::string legacyError;
    std::string currentError;
    std::string legacyFrame;
    std::string currentFrame;
    std::string diffFrame;
    std::optional<DiffStats> diff;
    bool continuedAfterFailure = false;
};

struct RuntimeEvidence {
    bool glfwInitialized = false;
    bool glfwVulkanSupported = false;
    std::string glfwVersion;
    std::string glfwError;
    std::string backend;
    std::string vulkanApi;
    std::string deviceName;
    std::string driverName;
    std::vector<std::string> instanceExtensions;
};

struct CleanupEvidence {
    std::string tempDirectory;
    bool tempRemoved = false;
    bool vulkanShutdown = false;
    uint32_t liveTransientObjects = 0;
};

struct ReviewResult {
    std::string startedAtUtc;
    std::string completedAtUtc;
    std::string outputDirectory;
    SnapshotInfo legacySnapshot;
    SnapshotInfo currentSnapshot;
    std::optional<TruncatedRecord> truncated;
    std::string truncatedTailRelative;
    std::vector<RecordAnomaly> legacyAnomalies;
    std::vector<RecordAnomaly> currentAnomalies;
    std::vector<ObjectReview> objects;
    RuntimeEvidence runtime;
    CleanupEvidence cleanup;
    std::vector<std::string> warnings;
};

void writeReviewArtifacts(const ReviewResult& result);
std::string utcNow();

}  // namespace lshc
