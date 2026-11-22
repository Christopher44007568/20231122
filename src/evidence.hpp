#pragma once

#include "image.hpp"
#include "json.hpp"
#include "model.hpp"
#include "platform.hpp"
#include "vulkan_renderer.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace rr {

struct ArtifactRecord {
    std::string path;
    std::uint64_t bytes = 0;
    std::string sha256;
};

struct DrawEvidence {
    std::size_t sequence = 0;
    std::string id;
    std::uint32_t firstIndex = 0;
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::string beforeSha256;
    std::string afterSha256;
    DiffStats change;
    std::string savedFramePath;
    std::string savedFrameSha256;
};

struct ReferenceEvidence {
    std::string pass;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::array<std::uint8_t, 4> expected{};
    std::array<std::uint8_t, 4> actual{};
    std::uint32_t tolerance = 0;
    std::uint32_t maxDelta = 0;
    bool passed = false;
};

struct ReferenceSummary {
    std::uint64_t total = 0;
    std::uint64_t failed = 0;
    std::uint64_t omitted = 0;
    std::vector<ReferenceEvidence> reported;
};

struct PassEvidence {
    std::string id;
    std::string ruleId;
    Rule effectiveRule;
    std::vector<std::string> warnings;
    std::string initialSha256;
    std::string finalSha256;
    std::string framePpm;
    std::string framePam;
    std::string changeDiffPpm;
    DiffStats changedFromClear;
    std::uint64_t elapsedMicroseconds = 0;
    std::uint32_t framebufferWidth = 0;
    std::uint32_t framebufferHeight = 0;
    std::size_t vertexBufferBytes = 0;
    std::size_t indexBufferBytes = 0;
    std::size_t drawCount = 0;
    std::vector<DrawEvidence> draws;
    ReferenceSummary references;
};

struct EdgeSampleEvidence {
    std::string pass;
    std::string probe;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::array<std::uint8_t, 4> rgba{};
};

struct SharedEdgeEvidence {
    std::string id;
    std::string drawA;
    std::string drawB;
    std::array<double, 2> a{};
    std::array<double, 2> b{};
    std::vector<EdgeSampleEvidence> samples;
};

struct BoundaryEvidence {
    bool present = false;
    std::string parameter;
    double threshold = 0.0;
    std::string beforePass;
    std::string afterPass;
    std::string beforeValue;
    std::string afterValue;
    std::string beforeRequestedValue;
    std::string afterRequestedValue;
    std::string beforeImage;
    std::string afterImage;
    std::string diffImage;
    DiffStats diff;
    bool hasSamplePixel = false;
    std::uint32_t sampleX = 0;
    std::uint32_t sampleY = 0;
    std::array<std::uint8_t, 4> beforeRgba{};
    std::array<std::uint8_t, 4> afterRgba{};
    std::uint32_t sampleMaxDelta = 0;
};

struct RunEvidence {
    std::string status = "error";
    std::string generatedAt;
    std::string inputPath;
    std::uint64_t inputBytes = 0;
    std::string inputSha256;
    std::string inputSha256Scope = "full";
    bool emptyInput = false;
    std::string scenarioName;
    Limits limits;
    PlatformInfo platform;
    DeviceInfo device;
    std::uint32_t scenarioWidth = 0;
    std::uint32_t scenarioHeight = 0;
    std::size_t vertices = 0;
    std::size_t indices = 0;
    std::size_t rules = 0;
    std::size_t draws = 0;
    std::size_t passes = 0;
    std::size_t sharedEdges = 0;
    std::size_t referencePixels = 0;
    std::uint64_t totalDrawSubmissions = 0;
    std::uint64_t durationMilliseconds = 0;
    std::vector<Diagnostic> warnings;
    std::vector<Diagnostic> errors;
    std::vector<PassEvidence> passEvidence;
    std::vector<SharedEdgeEvidence> edgeEvidence;
    BoundaryEvidence boundary;
    std::vector<ArtifactRecord> artifacts;
};

class EvidenceWriter {
public:
    explicit EvidenceWriter(std::filesystem::path outputDirectory);

    bool initialize(std::string& error);
    bool saveText(const std::string& relativePath,
                  const std::string& text,
                  std::string* storedPath,
                  std::string& error);
    bool saveImage(const std::string& relativePath,
                   const Image& image,
                   std::string* storedPath,
                   std::string& error);
    bool savePam(const std::string& relativePath,
                 const Image& image,
                 std::string* storedPath,
                 std::string& error);
    bool saveDiff(const std::string& relativePath,
                  const Image& before,
                  const Image& after,
                  std::string* storedPath,
                  std::string& error);
    bool finish(RunEvidence evidence, std::string& error);

    const std::filesystem::path& directory() const;
    std::string absolutePath(const std::string& relativePath) const;

private:
    bool recordArtifact(const std::string& relativePath, std::string& error);
    void recordArtifactFromPath(const std::string& relativePath,
                                const std::filesystem::path& absolute);

    std::filesystem::path directory_;
    std::vector<ArtifactRecord> artifacts_;
};

std::string safeName(const std::string& value);

}  // namespace rr
