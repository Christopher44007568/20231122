#pragma once

#include "core/material.h"
#include "core/timestamp.h"
#include "core/util.h"
#include "render/renderer.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ssor {

struct DiffResult {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t diffPixels = 0;
    std::uint8_t threshold = 8;
    double meanAbsError = 0.0;
    std::uint32_t maxAbsError = 0;
    bool bboxValid = false;
    std::uint32_t x0 = 0;
    std::uint32_t y0 = 0;
    std::uint32_t x1 = 0;
    std::uint32_t y1 = 0;
    double centroidX = 0.0;
    double centroidY = 0.0;
    ImageRgba8 diffImage;
};

DiffResult computeDiff(const ImageRgba8& baseline, const ImageRgba8& repeat,
                       std::uint8_t threshold = 8);

struct BoundaryProbeResult {
    std::uint32_t index = 0;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::uint64_t sourceOffset = 0;
    std::string expected;
    std::uint32_t objectIndex = 0;
    bool detectedBoundary = false;
    bool outline = false;
    float depthDiff = 0.0f;
    float normalDiff = 0.0f;
    bool matched = false;
};

struct BoundaryReport {
    std::uint32_t total = 0;
    std::uint32_t boundaryTotal = 0;
    std::uint32_t boundaryMatched = 0;
    std::uint32_t interiorTotal = 0;
    std::uint32_t interiorMatched = 0;
    std::uint32_t backgroundTotal = 0;
    std::uint32_t backgroundMatched = 0;
    std::uint32_t missedBoundaries = 0;
    std::uint32_t falsePositives = 0;
    std::vector<BoundaryProbeResult> probes;
    ImageRgba8 overlay;
};

BoundaryReport evaluateBoundary(const RawMaterial& material,
                                const SceneFrameEvidence& baseline);

struct ObjectAttribution {
    std::uint32_t objectIdValue = 0;
    std::uint32_t objectIndex = 0;
    std::string objectId;
    bool duplicate = false;
    std::uint64_t pixelCount = 0;
    std::vector<std::uint64_t> drawCallIds;
};

struct CorrelationResult {
    DiffResult diff;
    std::vector<ObjectAttribution> attribution;
    std::uint64_t duplicateDiffPixels = 0;
    std::uint64_t duplicateOutlinePixels = 0;
    bool sharedMeshBuffer = false;
    bool sharedPipeline = false;
    bool sharedDescriptorSet = false;
    bool anomalyDetected = false;
    std::string verdict;
};

CorrelationResult correlateScenes(const RawMaterial& material,
                                  const SceneFrameEvidence& baseline,
                                  const SceneFrameEvidence& repeat);

struct EnvironmentInfo {
    std::string deviceName;
    std::string deviceType;
    std::string apiVersion;
    std::string driverVersion;
    std::string glfwVersion;
    std::string icdPath;
    bool validationEnabled = false;
    bool timestampValid = false;
    float timestampPeriod = 0.0f;
    std::string compiler;
    std::string buildType;
};

EnvironmentInfo environmentFromGpu(const GpuContext& gpu);
nlohmann::ordered_json environmentToJson(const EnvironmentInfo& environment);

nlohmann::ordered_json timeToJson(const TimestampPoint& time);
nlohmann::ordered_json drawToJson(const DrawCallEvidence& draw,
                                  const std::string& scene);
nlohmann::ordered_json frameToJson(const SceneFrameEvidence& frame);
nlohmann::ordered_json diffToJson(const DiffResult& diff);
nlohmann::ordered_json boundaryToJson(const BoundaryReport& report);
nlohmann::ordered_json correlationToJson(const CorrelationResult& correlation);

class EvidenceChain {
public:
    bool open(const std::string& path, std::string* error);
    bool append(const std::string& type, const nlohmann::ordered_json& payload,
                nlohmann::ordered_json* eventOut, std::string* error);
    bool writeHead(const std::string& path, std::string* error) const;
    std::uint64_t count() const { return m_count; }
    const std::string& head() const { return m_prevHash; }
    const std::string& path() const { return m_path; }

private:
    std::ofstream m_out;
    std::string m_prevHash = "GENESIS";
    std::string m_path;
    std::uint64_t m_count = 0;
};

bool verifyEvidenceChain(const std::string& path, std::string* error,
                         std::uint64_t* count, std::string* head);

}  // namespace ssor
