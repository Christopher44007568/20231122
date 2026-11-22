#include "evidence.hpp"

#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {

std::string boolText(bool value)
{
    return value ? "true" : "false";
}

std::string gpuStatsJson(const std::array<uint32_t, 4>& stats)
{
    std::ostringstream out;
    out << "{\"samples\": " << stats[0]
        << ", \"missing_fragment_samples\": " << stats[1]
        << ", \"nan_rejected\": " << stats[2]
        << ", \"positive_density_samples\": " << stats[3] << "}";
    return out.str();
}

std::string frameJson(const EvidenceFrame& frame)
{
    std::ostringstream out;
    out << "{\"image\": " << imageMetricsJson(frame.metrics)
        << ", \"gpu\": " << gpuStatsJson(frame.gpuStats) << "}";
    return out.str();
}

std::string degradationJson(const std::vector<std::string>& degradations)
{
    std::ostringstream out;
    out << "[";
    for (size_t index = 0; index < degradations.size(); ++index) {
        if (index != 0) {
            out << ", ";
        }
        out << "\"" << degradations[index] << "\"";
    }
    out << "]";
    return out.str();
}

std::string warningsJson(const std::vector<std::string>& warnings)
{
    return degradationJson(warnings);
}

std::string statusSummary(const EvidenceRun& run)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(8)
        << "status=" << run.status << "\n"
        << "mode=" << run.mode << "\n"
        << "scenario=" << run.config.scenario << "\n"
        << "device=" << run.deviceName << "\n"
        << "render_tier=" << run.plan.tier << "\n"
        << "render_size=" << run.plan.width << "x" << run.plan.height << "\n"
        << "steps=" << run.plan.steps << "\n"
        << "boundary_frame_before=" << run.boundaryFrameBefore << "\n"
        << "boundary_frame_after=" << run.boundaryFrameAfter << "\n"
        << "checkpoint_fnv1a64=" << std::hex << run.checkpoint.hash << std::dec
        << "\n"
        << "checkpoint_bytes=" << run.checkpoint.fileBytes << "\n"
        << "probe_before_rgb=" << run.boundaryChange.beforeProbe.rgb[0] << ","
        << run.boundaryChange.beforeProbe.rgb[1] << ","
        << run.boundaryChange.beforeProbe.rgb[2] << "\n"
        << "probe_after_rgb=" << run.boundaryChange.afterProbe.rgb[0] << ","
        << run.boundaryChange.afterProbe.rgb[1] << ","
        << run.boundaryChange.afterProbe.rgb[2] << "\n"
        << "probe_delta_luminance="
        << run.boundaryChange.probeDeltaLuminance << "\n"
        << "hotspot_pixel=" << run.boundaryChange.afterHotspot.x << ","
        << run.boundaryChange.afterHotspot.y << "\n"
        << "hotspot_before_rgb=" << run.boundaryChange.beforeHotspot.rgb[0]
        << "," << run.boundaryChange.beforeHotspot.rgb[1] << ","
        << run.boundaryChange.beforeHotspot.rgb[2] << "\n"
        << "hotspot_after_rgb=" << run.boundaryChange.afterHotspot.rgb[0]
        << "," << run.boundaryChange.afterHotspot.rgb[1] << ","
        << run.boundaryChange.afterHotspot.rgb[2] << "\n"
        << "hotspot_delta_luminance="
        << run.boundaryChange.hotspotDeltaLuminance << "\n"
        << "full_mean_abs_difference="
        << run.boundaryChange.meanAbsDifference << "\n"
        << "full_rmse=" << run.boundaryChange.rmse << "\n"
        << "replay_exact=" << boolText(run.replayExact) << "\n"
        << "missing_fragment_samples=" << run.afterFrame.gpuStats[1] << "\n"
        << "positive_density_samples=" << run.afterFrame.gpuStats[3] << "\n"
        << "nan_rejected=" << run.afterFrame.gpuStats[2] << "\n"
        << "degraded=" << boolText(run.plan.tier > 0) << "\n";
    for (const std::string& reason : run.plan.degradations) {
        out << "degradation=" << reason << "\n";
    }
    return out.str();
}

} // namespace

std::string utcTimestamp()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void writeEvidenceBundle(const std::string& directory, EvidenceRun& run)
{
    const std::filesystem::path root(directory);
    std::filesystem::create_directories(root);
    if (run.timestampUtc.empty()) {
        run.timestampUtc = utcTimestamp();
    }

    writePpm((root / "boundary_before.ppm").string(), run.beforeImage);
    writePpm((root / "boundary_after.ppm").string(), run.afterImage);
    writeRawRgba16((root / "boundary_before.rgba16").string(),
                   run.checkpoint.halfPixels);
    std::vector<uint16_t> afterHalf(run.afterImage.rgba.size());
    std::vector<uint16_t> replayHalf(run.replayImage.rgba.size());
    std::vector<uint16_t> continuationHalf(run.continuationImage.rgba.size());
    for (size_t index = 0; index < afterHalf.size(); ++index) {
        afterHalf[index] = floatToHalf(run.afterImage.rgba[index]);
    }
    for (size_t index = 0; index < replayHalf.size(); ++index) {
        replayHalf[index] = floatToHalf(run.replayImage.rgba[index]);
    }
    for (size_t index = 0; index < continuationHalf.size(); ++index) {
        continuationHalf[index] = floatToHalf(run.continuationImage.rgba[index]);
    }
    writeRawRgba16((root / "boundary_after.rgba16").string(), afterHalf);
    if (!run.replayImage.rgba.empty()) {
        writePpm((root / "boundary_replay.ppm").string(), run.replayImage);
        writeRawRgba16((root / "boundary_replay.rgba16").string(), replayHalf);
    }
    if (!run.continuationImage.rgba.empty()) {
        writePpm((root / "continuation.ppm").string(), run.continuationImage);
        writeRawRgba16((root / "continuation.rgba16").string(),
                       continuationHalf);
    }
    writeText((root / "summary.txt").string(), statusSummary(run));

    std::ostringstream json;
    json << std::fixed << std::setprecision(8)
         << "{\n"
         << "  \"schema\": \"fog-stability-evidence/v1\",\n"
         << "  \"generated_utc\": \"" << run.timestampUtc << "\",\n"
         << "  \"mode\": \"" << run.mode << "\",\n"
         << "  \"status\": \"" << run.status << "\",\n"
         << "  \"scenario\": \"" << run.config.scenario << "\",\n"
         << "  \"config\": " << fogConfigJson(run.config) << ",\n"
         << "  \"device\": {\n"
         << "    \"name\": \"" << run.deviceName << "\",\n"
         << "    \"type\": \"" << run.deviceType << "\",\n"
         << "    \"device_memory_mib\": "
         << (run.deviceMemoryBytes / (1024ull * 1024ull)) << ",\n"
         << "    \"budget_mib\": "
         << (run.memoryBudgetBytes / (1024ull * 1024ull)) << ",\n"
         << "    \"budget_simulated\": " << boolText(run.simulatedBudget)
         << "\n  },\n"
         << "  \"resource_plan\": {\n"
         << "    \"tier\": " << run.plan.tier << ",\n"
         << "    \"label\": \"" << run.plan.label << "\",\n"
         << "    \"width\": " << run.plan.width << ",\n"
         << "    \"height\": " << run.plan.height << ",\n"
         << "    \"steps\": " << run.plan.steps << ",\n"
         << "    \"estimated_mib\": "
         << (run.plan.estimatedBytes / (1024ull * 1024ull)) << ",\n"
         << "    \"degradations\": " << degradationJson(run.plan.degradations)
         << "\n  },\n"
         << "  \"checkpoint\": " << checkpointJson(run.checkpoint) << ",\n"
         << "  \"boundary\": {\n"
         << "    \"frame_before\": " << run.boundaryFrameBefore << ",\n"
         << "    \"frame_after\": " << run.boundaryFrameAfter << ",\n"
         << "    \"change\": " << comparisonJson(run.boundaryChange) << ",\n"
         << "    \"replay_exact\": " << boolText(run.replayExact) << ",\n"
         << "    \"replay_change\": "
         << comparisonJson(run.replayChange) << "\n"
         << "  },\n"
         << "  \"frames\": {\n"
         << "    \"before\": " << frameJson(run.beforeFrame) << ",\n"
         << "    \"after\": " << frameJson(run.afterFrame) << ",\n"
         << "    \"replay\": " << frameJson(run.replayFrame) << ",\n"
         << "    \"continuation\": " << frameJson(run.continuationFrame) << "\n"
         << "  },\n"
         << "  \"warnings\": " << warningsJson(run.warnings) << ",\n"
         << "  \"artifacts\": [\n"
         << "    \"checkpoint.fogcp\",\n"
         << "    \"checkpoint.json\",\n"
         << "    \"boundary_before.ppm\",\n"
         << "    \"boundary_before.rgba16\",\n"
         << "    \"boundary_after.ppm\",\n"
         << "    \"boundary_after.rgba16\",\n"
         << "    \"boundary_replay.ppm\",\n"
         << "    \"continuation.ppm\",\n"
         << "    \"summary.txt\"\n"
         << "  ]\n"
         << "}\n";

    writeText((root / "run.json").string(), json.str());
    writeText((root / "checkpoint.json").string(),
              checkpointJson(run.checkpoint) + "\n");
    std::cout << "evidence: " << (root / "run.json").string() << "\n"
              << "status: " << run.status << "\n"
              << "boundary probe delta luminance: "
              << run.boundaryChange.probeDeltaLuminance << "\n"
              << "boundary hotspot delta luminance: "
              << run.boundaryChange.hotspotDeltaLuminance << " at "
              << run.boundaryChange.afterHotspot.x << ","
              << run.boundaryChange.afterHotspot.y << "\n"
              << "replay exact: " << boolText(run.replayExact) << "\n";
}
