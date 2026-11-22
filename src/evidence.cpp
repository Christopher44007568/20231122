#include "evidence.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <sstream>

namespace ssor {
namespace {

std::uint8_t luma(const std::uint8_t* rgba) {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(rgba[0]) * 77 +
         static_cast<std::uint32_t>(rgba[1]) * 150 +
         static_cast<std::uint32_t>(rgba[2]) * 29) >>
        8);
}

void putPixel(ImageRgba8& image, std::uint32_t x, std::uint32_t y,
              std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (x >= image.width || y >= image.height) return;
    if (image.pixels.size() < static_cast<std::size_t>(image.width) *
                                  image.height * 4) {
        return;
    }
    const std::size_t p =
        (static_cast<std::size_t>(y) * image.width + x) * 4;
    image.pixels[p + 0] = r;
    image.pixels[p + 1] = g;
    image.pixels[p + 2] = b;
    image.pixels[p + 3] = 255;
}

void drawMarker(ImageRgba8& image, std::uint32_t x, std::uint32_t y,
                std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            const int px = static_cast<int>(x) + dx;
            const int py = static_cast<int>(y) + dy;
            if (px >= 0 && py >= 0) {
                putPixel(image, static_cast<std::uint32_t>(px),
                         static_cast<std::uint32_t>(py), r, g, b);
            }
        }
    }
}

}  // namespace

DiffResult computeDiff(const ImageRgba8& baseline, const ImageRgba8& repeat,
                       std::uint8_t threshold) {
    DiffResult result;
    result.width = std::min(baseline.width, repeat.width);
    result.height = std::min(baseline.height, repeat.height);
    result.threshold = threshold;
    if (result.width == 0 || result.height == 0) return result;
    result.diffImage.width = result.width;
    result.diffImage.height = result.height;
    result.diffImage.pixels.resize(
        static_cast<std::size_t>(result.width) * result.height * 4);

    std::uint64_t sumError = 0;
    std::uint64_t diffCount = 0;
    double sumX = 0.0;
    double sumY = 0.0;
    std::uint32_t minX = result.width;
    std::uint32_t minY = result.height;
    std::uint32_t maxX = 0;
    std::uint32_t maxY = 0;
    for (std::uint32_t y = 0; y < result.height; ++y) {
        for (std::uint32_t x = 0; x < result.width; ++x) {
            const std::size_t p =
                (static_cast<std::size_t>(y) * result.width + x) * 4;
            const std::size_t q =
                (static_cast<std::size_t>(y) * baseline.width + x) * 4;
            const std::size_t r =
                (static_cast<std::size_t>(y) * repeat.width + x) * 4;
            std::uint32_t maxDiff = 0;
            for (int c = 0; c < 3; ++c) {
                const int diff = std::abs(static_cast<int>(baseline.pixels[q + c]) -
                                          static_cast<int>(repeat.pixels[r + c]));
                sumError += static_cast<std::uint64_t>(diff);
                maxDiff = std::max(maxDiff, static_cast<std::uint32_t>(diff));
            }
            result.maxAbsError = std::max(result.maxAbsError, maxDiff);
            const bool changed = maxDiff > threshold;
            if (changed) {
                ++diffCount;
                sumX += x;
                sumY += y;
                minX = std::min(minX, x);
                minY = std::min(minY, y);
                maxX = std::max(maxX, x);
                maxY = std::max(maxY, y);
            }
            const std::uint8_t baseLuma = luma(&baseline.pixels[q]);
            const std::uint8_t repeatLuma = luma(&repeat.pixels[r]);
            const std::uint8_t amplified = static_cast<std::uint8_t>(
                std::min<std::uint32_t>(255, maxDiff * 8));
            result.diffImage.pixels[p + 0] = changed ? 255 : amplified;
            result.diffImage.pixels[p + 1] =
                static_cast<std::uint8_t>(baseLuma / 3);
            result.diffImage.pixels[p + 2] =
                static_cast<std::uint8_t>(repeatLuma / 3);
            result.diffImage.pixels[p + 3] = 255;
        }
    }
    const std::uint64_t pixels =
        static_cast<std::uint64_t>(result.width) * result.height;
    result.meanAbsError =
        static_cast<double>(sumError) / static_cast<double>(pixels * 3);
    if (diffCount > 0) {
        result.diffPixels = diffCount;
        result.bboxValid = true;
        result.x0 = minX;
        result.y0 = minY;
        result.x1 = maxX + 1;
        result.y1 = maxY + 1;
        result.centroidX = sumX / static_cast<double>(diffCount);
        result.centroidY = sumY / static_cast<double>(diffCount);
    }
    return result;
}

BoundaryReport evaluateBoundary(const RawMaterial& material,
                                const SceneFrameEvidence& baseline) {
    BoundaryReport report;
    report.overlay = baseline.finalImage;
    const std::uint32_t width = baseline.finalImage.width;
    const std::uint32_t height = baseline.finalImage.height;
    if (width == 0 || height == 0 ||
        baseline.metricsValues.size() <
            static_cast<std::size_t>(width) * height * 4) {
        return report;
    }
    for (std::size_t i = 0; i < material.boundaryPixels.size(); ++i) {
        const BoundaryProbe& probe = material.boundaryPixels[i];
        if (probe.x >= width || probe.y >= height) continue;
        const std::size_t pixel =
            (static_cast<std::size_t>(probe.y) * width + probe.x) * 4;
        BoundaryProbeResult result;
        result.index = static_cast<std::uint32_t>(i);
        result.x = probe.x;
        result.y = probe.y;
        result.sourceOffset = probe.recordOffset;
        result.expected = probeKindName(probe.expected);
        result.objectIndex = probe.objectIndex;
        result.depthDiff = baseline.metricsValues[pixel + 0];
        result.normalDiff = baseline.metricsValues[pixel + 1];
        result.detectedBoundary = baseline.metricsValues[pixel + 2] > 0.5f;
        result.outline = baseline.metricsValues[pixel + 3] > 0.5f;
        switch (probe.expected) {
            case ProbeKind::Boundary:
                ++report.boundaryTotal;
                result.matched = result.detectedBoundary;
                if (result.matched) {
                    ++report.boundaryMatched;
                } else {
                    ++report.missedBoundaries;
                }
                drawMarker(report.overlay, probe.x, probe.y,
                           result.matched ? 40 : 255,
                           result.matched ? 255 : 30, 40);
                break;
            case ProbeKind::Interior:
                ++report.interiorTotal;
                result.matched = !result.detectedBoundary;
                if (result.matched) {
                    ++report.interiorMatched;
                } else {
                    ++report.falsePositives;
                }
                drawMarker(report.overlay, probe.x, probe.y, 40, 120,
                           result.matched ? 255 : 255);
                break;
            case ProbeKind::Background:
                ++report.backgroundTotal;
                result.matched = !result.detectedBoundary;
                if (result.matched) {
                    ++report.backgroundMatched;
                } else {
                    ++report.falsePositives;
                }
                drawMarker(report.overlay, probe.x, probe.y,
                           result.matched ? 40 : 255,
                           result.matched ? 255 : 220,
                           result.matched ? 220 : 40);
                break;
        }
        report.probes.push_back(result);
    }
    report.total = static_cast<std::uint32_t>(report.probes.size());
    return report;
}

CorrelationResult correlateScenes(const RawMaterial& material,
                                  const SceneFrameEvidence& baseline,
                                  const SceneFrameEvidence& repeat) {
    CorrelationResult result;
    result.diff = computeDiff(baseline.finalImage, repeat.finalImage);
    const std::uint32_t width = repeat.finalImage.width;
    const std::uint32_t height = repeat.finalImage.height;
    if (width == 0 || height == 0 ||
        repeat.objectIdValues.size() < static_cast<std::size_t>(width) * height) {
        result.verdict = "NO_IMAGE_EVIDENCE";
        return result;
    }
    const int duplicateIndex = material.duplicateObjectIndex();
    const std::uint32_t duplicateValue =
        duplicateIndex >= 0 ? static_cast<std::uint32_t>(duplicateIndex + 1) : 0;
    std::map<std::uint32_t, ObjectAttribution> attributions;
    auto addAttribution = [&](const SceneFrameEvidence& frame,
                              std::uint32_t value) {
        if (value == 0 || value > material.objects.size()) return;
        auto& item = attributions[value];
        if (item.objectIdValue != 0) return;
        const std::uint32_t index = value - 1;
        const ObjectDef& object = material.objects[index];
        item.objectIdValue = value;
        item.objectIndex = index;
        item.objectId = object.id;
        item.duplicate = object.duplicate();
        for (const DrawCallEvidence& draw : frame.draws) {
            if (draw.pass == "gbuffer" && draw.objectIdValue == value) {
                item.drawCallIds.push_back(draw.id);
            }
        }
    };

    if (result.diff.bboxValid) {
        for (std::uint32_t y = result.diff.y0; y < result.diff.y1; ++y) {
            for (std::uint32_t x = result.diff.x0; x < result.diff.x1; ++x) {
                const std::size_t p =
                    (static_cast<std::size_t>(y) * width + x) * 4;
                const std::size_t q =
                    (static_cast<std::size_t>(y) * baseline.finalImage.width +
                     x) *
                    4;
                std::uint32_t maxDiff = 0;
                for (int c = 0; c < 3; ++c) {
                    maxDiff = std::max<std::uint32_t>(
                        maxDiff,
                        static_cast<std::uint32_t>(std::abs(
                            static_cast<int>(baseline.finalImage.pixels[q + c]) -
                            static_cast<int>(repeat.finalImage.pixels[p + c]))));
                }
                const bool changed = maxDiff > result.diff.threshold;
                if (!changed) continue;
                std::set<std::uint32_t> ids;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        const int sx = static_cast<int>(x) + dx;
                        const int sy = static_cast<int>(y) + dy;
                        if (sx < 0 || sy < 0 ||
                            sx >= static_cast<int>(width) ||
                            sy >= static_cast<int>(height)) {
                            continue;
                        }
                        const std::uint32_t value =
                            repeat.objectIdValues[static_cast<std::size_t>(sy) *
                                                      width +
                                                  static_cast<std::size_t>(sx)];
                        if (value != 0) ids.insert(value);
                    }
                }
                for (std::uint32_t value : ids) {
                    addAttribution(repeat, value);
                    ++attributions[value].pixelCount;
                }
                const std::size_t metricIndex =
                    (static_cast<std::size_t>(y) * width + x) * 4 + 3;
                if (metricIndex < repeat.metricsValues.size() &&
                    repeat.metricsValues[metricIndex] > 0.5f &&
                    duplicateValue != 0 &&
                    ids.count(duplicateValue) != 0) {
                    ++result.duplicateOutlinePixels;
                }
            }
        }
    }
    for (auto& pair : attributions) {
        if (pair.second.objectIdValue == duplicateValue) {
            result.duplicateDiffPixels = pair.second.pixelCount;
        }
        result.attribution.push_back(pair.second);
    }
    std::sort(result.attribution.begin(), result.attribution.end(),
              [](const ObjectAttribution& a, const ObjectAttribution& b) {
                  return a.objectIndex < b.objectIndex;
              });

    if (duplicateIndex >= 0) {
        const std::uint32_t baseIndex =
            material.objects[duplicateIndex].repeatOf;
        const DrawCallEvidence* baseDraw = nullptr;
        const DrawCallEvidence* duplicateDraw = nullptr;
        for (const DrawCallEvidence& draw : repeat.draws) {
            if (draw.pass != "gbuffer") continue;
            if (draw.objectIndex == baseIndex) baseDraw = &draw;
            if (draw.objectIndex == static_cast<std::uint32_t>(duplicateIndex)) {
                duplicateDraw = &draw;
            }
        }
        if (baseDraw && duplicateDraw) {
            result.sharedMeshBuffer =
                !baseDraw->meshSha256.empty() &&
                baseDraw->meshSha256 == duplicateDraw->meshSha256;
            result.sharedPipeline =
                !baseDraw->pipelineId.empty() &&
                baseDraw->pipelineId == duplicateDraw->pipelineId &&
                baseDraw->pipeLayoutId == duplicateDraw->pipeLayoutId;
            result.sharedDescriptorSet =
                baseDraw->descriptorSetId != "none" &&
                baseDraw->descriptorSetId == duplicateDraw->descriptorSetId;
        }
    }
    result.anomalyDetected =
        result.diff.diffPixels > 50 && result.duplicateDiffPixels > 0 &&
        result.duplicateOutlinePixels > 0 && result.sharedMeshBuffer &&
        result.sharedPipeline;
    result.verdict = result.anomalyDetected
                         ? "REPEATED_OBJECT_OUTLINE_ANOMALY"
                         : "NO_DUPLICATE_ANOMALY";
    return result;
}

EnvironmentInfo environmentFromGpu(const GpuContext& gpu) {
    EnvironmentInfo info;
    info.deviceName = gpu.properties.deviceName;
    switch (gpu.properties.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_CPU: info.deviceType = "CPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: info.deviceType = "VIRTUAL_GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: info.deviceType = "DISCRETE_GPU"; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: info.deviceType = "INTEGRATED_GPU"; break;
        default: info.deviceType = "OTHER"; break;
    }
    const std::uint32_t api = gpu.properties.apiVersion;
    info.apiVersion = std::to_string(VK_VERSION_MAJOR(api)) + "." +
                      std::to_string(VK_VERSION_MINOR(api)) + "." +
                      std::to_string(VK_VERSION_PATCH(api));
    std::ostringstream driver;
    driver << "0x" << std::hex << gpu.properties.driverVersion;
    info.driverVersion = driver.str();
    info.glfwVersion = gpu.glfwVersion;
    info.icdPath = gpu.icdPath;
    info.validationEnabled = gpu.validationEnabled;
    info.timestampValid = gpu.timestampValid;
    info.timestampPeriod = gpu.timestampPeriod;
#if defined(__clang__)
    info.compiler = "clang " __clang_version__;
#elif defined(__GNUC__)
    info.compiler = "gcc " __VERSION__;
#else
    info.compiler = "unknown";
#endif
#ifdef NDEBUG
    info.buildType = "Release";
#else
    info.buildType = "Debug";
#endif
    return info;
}

nlohmann::ordered_json timeToJson(const TimestampPoint& time) {
    return {{"utc", time.utc},
            {"epochMs", time.epochMs},
            {"monotonicNs", time.monotonicNs}};
}

nlohmann::ordered_json environmentToJson(const EnvironmentInfo& environment) {
    return {
        {"deviceName", environment.deviceName},
        {"deviceType", environment.deviceType},
        {"apiVersion", environment.apiVersion},
        {"driverVersion", environment.driverVersion},
        {"glfwVersion", environment.glfwVersion},
        {"icdPath", environment.icdPath},
        {"validationEnabled", environment.validationEnabled},
        {"timestampValid", environment.timestampValid},
        {"timestampPeriod", environment.timestampPeriod},
        {"compiler", environment.compiler},
        {"buildType", environment.buildType},
    };
}

nlohmann::ordered_json drawToJson(const DrawCallEvidence& draw,
                                  const std::string& scene) {
    nlohmann::ordered_json json;
    json["id"] = draw.id;
    json["scene"] = scene;
    json["pass"] = draw.pass;
    json["passIndex"] = draw.passIndex;
    json["objectIndex"] = draw.objectIndex;
    json["objectId"] = draw.objectId;
    json["objectIdValue"] = draw.objectIdValue;
    json["instanceIndex"] = draw.instanceIndex;
    json["firstInstance"] = draw.firstInstance;
    json["vertexOffset"] = draw.vertexOffset;
    json["firstIndex"] = draw.firstIndex;
    json["indexCount"] = draw.indexCount;
    json["instanceCount"] = draw.instanceCount;
    json["vertexBufferId"] = draw.vertexBufferId;
    json["indexBufferId"] = draw.indexBufferId;
    json["pipelineId"] = draw.pipelineId;
    json["pipeLayoutId"] = draw.pipeLayoutId;
    json["descriptorSetId"] = draw.descriptorSetId;
    json["meshSha256"] = draw.meshSha256;
    json["modelTranslation"] = {draw.modelTranslation.x, draw.modelTranslation.y,
                                draw.modelTranslation.z};
    json["duplicate"] = draw.duplicate;
    json["repeatOf"] = draw.repeatOf == kInvalidObject
                           ? nlohmann::ordered_json(nullptr)
                           : nlohmann::ordered_json(draw.repeatOf);
    json["commandOrdinal"] = draw.commandOrdinal;
    json["time"] = timeToJson(draw.time);
    return json;
}

nlohmann::ordered_json imageToJson(const ImageEvidence& image) {
    return {
        {"name", image.name},
        {"logicalId", image.logicalId},
        {"format", image.format},
        {"width", image.width},
        {"height", image.height},
        {"bytes", image.bytes},
        {"sha256", image.sha256},
        {"ppmPath", image.ppmPath},
    };
}

nlohmann::ordered_json frameToJson(const SceneFrameEvidence& frame) {
    nlohmann::ordered_json json;
    json["scene"] = frame.scene;
    json["start"] = timeToJson(frame.start);
    json["end"] = timeToJson(frame.end);
    json["submitStart"] = timeToJson(frame.submitStart);
    json["submitEnd"] = timeToJson(frame.submitEnd);
    json["commandOrdinal"] = frame.commandOrdinal;
    json["submitIndex"] = frame.submitIndex;
    nlohmann::ordered_json draws = nlohmann::ordered_json::array();
    for (const DrawCallEvidence& draw : frame.draws) {
        draws.push_back(drawToJson(draw, frame.scene));
    }
    json["drawCalls"] = std::move(draws);
    nlohmann::ordered_json transitions = nlohmann::ordered_json::array();
    for (const TransitionEvidence& transition : frame.transitions) {
        transitions.push_back({
            {"id", transition.id},
            {"pass", transition.pass},
            {"imageId", transition.imageId},
            {"oldLayout", transition.oldLayout},
            {"newLayout", transition.newLayout},
            {"srcStage", transition.srcStage},
            {"dstStage", transition.dstStage},
            {"srcAccess", transition.srcAccess},
            {"dstAccess", transition.dstAccess},
            {"commandOrdinal", transition.commandOrdinal},
            {"time", timeToJson(transition.time)},
        });
    }
    json["resourceTransitions"] = std::move(transitions);
    nlohmann::ordered_json framebuffers = nlohmann::ordered_json::array();
    for (const FramebufferEvidence& framebuffer : frame.framebuffers) {
        nlohmann::ordered_json attachments = nlohmann::ordered_json::array();
        for (const AttachmentEvidence& attachment : framebuffer.attachments) {
            attachments.push_back({
                {"name", attachment.name},
                {"imageId", attachment.imageId},
                {"format", attachment.format},
                {"initialLayout", attachment.initialLayout},
                {"finalLayout", attachment.finalLayout},
                {"clear", attachment.clear},
                {"width", attachment.width},
                {"height", attachment.height},
            });
        }
        framebuffers.push_back({
            {"id", framebuffer.id},
            {"pass", framebuffer.pass},
            {"framebufferId", framebuffer.framebufferId},
            {"width", framebuffer.width},
            {"height", framebuffer.height},
            {"attachments", std::move(attachments)},
            {"time", timeToJson(framebuffer.time)},
        });
    }
    json["framebuffers"] = std::move(framebuffers);
    nlohmann::ordered_json timings = nlohmann::ordered_json::array();
    for (const PassTiming& timing : frame.passTimings) {
        timings.push_back({
            {"pass", timing.pass},
            {"gpuValid", timing.valid},
            {"timestampPeriodNs", timing.timestampPeriodNs},
            {"startTicks", timing.startTicks},
            {"endTicks", timing.endTicks},
            {"elapsedNs", timing.elapsedNs},
            {"cpuStart", timeToJson(timing.cpuStart)},
            {"cpuEnd", timeToJson(timing.cpuEnd)},
        });
    }
    json["passTimings"] = std::move(timings);
    json["images"] = {
        {"normalDepth", imageToJson(frame.gbuffer)},
        {"objectId", imageToJson(frame.objectId)},
        {"metrics", imageToJson(frame.metrics)},
        {"boundary", imageToJson(frame.boundaryMask)},
        {"boundarySeed", imageToJson(frame.boundarySeed)},
        {"final", imageToJson(frame.finalColor)},
    };
    json["seedSetPixels"] = static_cast<std::uint64_t>(
        std::count_if(frame.seedValues.begin(), frame.seedValues.end(),
                      [](std::uint8_t value) { return value != 0; }));
    return json;
}

nlohmann::ordered_json diffToJson(const DiffResult& diff) {
    nlohmann::ordered_json bbox = nullptr;
    if (diff.bboxValid) {
        bbox = {{"x0", diff.x0}, {"y0", diff.y0},
                {"x1", diff.x1}, {"y1", diff.y1}};
    }
    return {
        {"width", diff.width},
        {"height", diff.height},
        {"threshold", diff.threshold},
        {"diffPixels", diff.diffPixels},
        {"meanAbsError", diff.meanAbsError},
        {"maxAbsError", diff.maxAbsError},
        {"bbox", bbox},
        {"centroid", {diff.centroidX, diff.centroidY}},
    };
}

nlohmann::ordered_json boundaryToJson(const BoundaryReport& report) {
    nlohmann::ordered_json probes = nlohmann::ordered_json::array();
    for (const BoundaryProbeResult& probe : report.probes) {
        probes.push_back({
            {"index", probe.index},
            {"x", probe.x},
            {"y", probe.y},
            {"sourceOffset", probe.sourceOffset},
            {"expected", probe.expected},
            {"objectIndex", probe.objectIndex},
            {"detectedBoundary", probe.detectedBoundary},
            {"outline", probe.outline},
            {"depthDiff", probe.depthDiff},
            {"normalDiff", probe.normalDiff},
            {"matched", probe.matched},
        });
    }
    return {
        {"total", report.total},
        {"boundaryTotal", report.boundaryTotal},
        {"boundaryMatched", report.boundaryMatched},
        {"interiorTotal", report.interiorTotal},
        {"interiorMatched", report.interiorMatched},
        {"backgroundTotal", report.backgroundTotal},
        {"backgroundMatched", report.backgroundMatched},
        {"missedBoundaries", report.missedBoundaries},
        {"falsePositives", report.falsePositives},
        {"probes", std::move(probes)},
    };
}

nlohmann::ordered_json correlationToJson(
    const CorrelationResult& correlation) {
    nlohmann::ordered_json attribution = nlohmann::ordered_json::array();
    for (const ObjectAttribution& item : correlation.attribution) {
        attribution.push_back({
            {"objectIdValue", item.objectIdValue},
            {"objectIndex", item.objectIndex},
            {"objectId", item.objectId},
            {"duplicate", item.duplicate},
            {"pixelCount", item.pixelCount},
            {"drawCallIds", item.drawCallIds},
        });
    }
    return {
        {"verdict", correlation.verdict},
        {"anomalyDetected", correlation.anomalyDetected},
        {"imageDifference", diffToJson(correlation.diff)},
        {"duplicateDiffPixels", correlation.duplicateDiffPixels},
        {"duplicateOutlinePixels", correlation.duplicateOutlinePixels},
        {"sharedMeshBuffer", correlation.sharedMeshBuffer},
        {"sharedPipeline", correlation.sharedPipeline},
        {"sharedDescriptorSet", correlation.sharedDescriptorSet},
        {"objectAttribution", std::move(attribution)},
    };
}

bool EvidenceChain::open(const std::string& path, std::string* error) {
    const std::string parent = std::filesystem::path(path).parent_path().string();
    if (!parent.empty() && !makeDirs(parent)) {
        if (error) *error = "cannot create evidence directory " + parent;
        return false;
    }
    m_out.open(path, std::ios::binary | std::ios::trunc);
    if (!m_out) {
        if (error) *error = "cannot open evidence chain " + path;
        return false;
    }
    m_path = path;
    m_prevHash = "GENESIS";
    m_count = 0;
    return true;
}

bool EvidenceChain::append(const std::string& type,
                           const nlohmann::ordered_json& payload,
                           nlohmann::ordered_json* eventOut,
                           std::string* error) {
    if (!m_out) {
        if (error) *error = "evidence chain is not open";
        return false;
    }
    const TimestampPoint time = nowTimestamp();
    nlohmann::ordered_json event;
    event["seq"] = m_count;
    event["utc"] = time.utc;
    event["epoch_ms"] = time.epochMs;
    event["monotonic_ns"] = time.monotonicNs;
    event["type"] = type;
    event["payload"] = payload;
    event["prev_hash"] = m_prevHash;
    const std::string canonical = event.dump();
    const std::string hash = sha256Hex(canonical);
    event["hash"] = hash;
    m_out << event.dump() << "\n";
    m_out.flush();
    if (!m_out) {
        if (error) *error = "failed to append evidence event";
        return false;
    }
    if (eventOut) *eventOut = event;
    m_prevHash = hash;
    ++m_count;
    return true;
}

bool EvidenceChain::writeHead(const std::string& path, std::string* error) const {
    nlohmann::ordered_json head;
    head["path"] = m_path;
    head["count"] = m_count;
    head["head"] = m_prevHash;
    head["writtenUtc"] = utcNowMicros();
    if (!writeText(path, head.dump(2) + "\n")) {
        if (error) *error = "cannot write evidence head " + path;
        return false;
    }
    return true;
}

bool verifyEvidenceChain(const std::string& path, std::string* error,
                         std::uint64_t* count, std::string* head) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open evidence chain " + path;
        return false;
    }
    std::string prev = "GENESIS";
    std::uint64_t expectedSeq = 0;
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::ordered_json event;
        try {
            event = nlohmann::ordered_json::parse(line);
        } catch (const std::exception& ex) {
            if (error) *error = "invalid JSON at chain seq " +
                                std::to_string(expectedSeq) + ": " + ex.what();
            return false;
        }
        if (!event.contains("seq") || event["seq"] != expectedSeq) {
            if (error) *error = "unexpected chain sequence at " +
                                std::to_string(expectedSeq);
            return false;
        }
        if (!event.contains("prev_hash") || event["prev_hash"] != prev) {
            if (error) *error = "broken prev_hash at seq " +
                                std::to_string(expectedSeq);
            return false;
        }
        if (!event.contains("hash")) {
            if (error) *error = "missing hash at seq " +
                                std::to_string(expectedSeq);
            return false;
        }
        const std::string recorded = event["hash"].get<std::string>();
        event.erase("hash");
        const std::string actual = sha256Hex(event.dump());
        if (recorded != actual) {
            if (error) *error = "hash mismatch at seq " +
                                std::to_string(expectedSeq);
            return false;
        }
        prev = recorded;
        ++expectedSeq;
    }
    if (count) *count = expectedSeq;
    if (head) *head = prev;
    return true;
}

}  // namespace ssor
