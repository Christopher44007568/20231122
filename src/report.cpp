#include "report.hpp"

#include "hash.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>

namespace lshc {
namespace {

std::string jsonEscape(const std::string& text)
{
    std::ostringstream out;
    for (unsigned char character : text) {
        switch (character) {
            case '"':
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (character < 0x20u) {
                    out << "\\u" << std::hex << std::setw(4)
                        << std::setfill('0') << static_cast<unsigned>(character)
                        << std::dec;
                } else {
                    out << static_cast<char>(character);
                }
        }
    }
    return out.str();
}

std::string quote(const std::string& text)
{
    return "\"" + jsonEscape(text) + "\"";
}

std::string number(double value)
{
    if (!std::isfinite(value)) {
        return "null";
    }
    std::ostringstream out;
    out << std::setprecision(9) << value;
    return out.str();
}

std::string hex32(uint32_t value)
{
    std::ostringstream out;
    out << "0x" << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

void writeRecord(std::ostream& out, const ShadowRecord& record)
{
    out << "{\"format\":" << quote(record.legacy ? "legacy-v1" : "current-v2")
        << ",\"object_id\":" << record.objectId
        << ",\"draw_call\":" << record.drawCall
        << ",\"raw_offset\":" << record.rawOffset
        << ",\"raw_size\":" << record.rawSize
        << ",\"crc32c\":" << quote(hex32(record.storedCrc))
        << ",\"integrity_ok\":" << (record.integrityOk ? "true" : "false")
        << ",\"params\":{\"center\":[" << number(record.centerX) << ','
        << number(record.centerY) << "],\"radius\":" << number(record.radius)
        << ",\"light\":[" << number(record.lightX) << ','
        << number(record.lightY) << ',' << number(record.lightZ)
        << "],\"strength\":" << number(record.strength)
        << ",\"penumbra\":" << number(record.penumbra)
        << ",\"resource_revision\":" << record.resourceRevision
        << ",\"pipeline_id\":" << record.pipelineId
        << ",\"content_hash\":\"" << std::hex << record.contentHash
        << std::dec << "\"}}";
}

void writeDrawCall(std::ostream& out, const DrawCallEvidence& draw)
{
    out << "{\"ordinal\":" << draw.ordinal
        << ",\"command_buffer\":" << draw.commandBuffer
        << ",\"pipeline\":" << draw.pipeline
        << ",\"pipeline_layout\":" << draw.pipelineLayout
        << ",\"descriptor_set\":" << draw.descriptorSet
        << ",\"topology\":" << quote(draw.topology)
        << ",\"vertex_count\":" << draw.vertexCount
        << ",\"instance_count\":" << draw.instanceCount
        << ",\"first_vertex\":" << draw.firstVertex
        << ",\"first_instance\":" << draw.firstInstance
        << ",\"descriptor_binding\":" << quote(draw.descriptorBinding) << '}';
}

void writeResource(std::ostream& out, const ResourceStateEvidence& resource)
{
    out << "{\"kind\":" << quote(resource.kind)
        << ",\"handle\":" << resource.handle
        << ",\"memory_handle\":" << resource.memoryHandle
        << ",\"memory_bytes\":" << resource.memoryBytes
        << ",\"format\":" << quote(resource.format)
        << ",\"layout_before\":" << quote(resource.layoutBefore)
        << ",\"layout_after\":" << quote(resource.layoutAfter)
        << ",\"write_stage\":" << quote(resource.writeStage)
        << ",\"write_access\":" << quote(resource.writeAccess)
        << ",\"content_crc32c\":" << quote(hex32(
               static_cast<uint32_t>(resource.contentCrc32c)))
        << ",\"sha256\":" << quote(resource.objectSha256) << '}';
}

void writeFramebuffer(std::ostream& out,
                      const FramebufferEvidence& framebuffer)
{
    out << "{\"render_pass\":" << framebuffer.renderPass
        << ",\"framebuffer\":" << framebuffer.framebuffer
        << ",\"color_image_view\":" << framebuffer.colorImageView
        << ",\"width\":" << framebuffer.width
        << ",\"height\":" << framebuffer.height
        << ",\"attachment_count\":" << framebuffer.attachmentCount
        << ",\"format\":" << quote(framebuffer.format)
        << ",\"clear_color\":" << quote(framebuffer.clearColor)
        << ",\"content_crc32c\":" << quote(framebuffer.contentCrc32c)
        << ",\"content_sha256\":" << quote(framebuffer.contentSha256) << '}';
}

void writeRender(std::ostream& out, const RenderEvidence& render,
                 const std::string& artifact)
{
    out << "{\"backend\":" << quote(render.backend)
        << ",\"device\":" << quote(render.deviceName)
        << ",\"driver\":" << quote(render.driverName)
        << ",\"draw_call\":";
    writeDrawCall(out, render.drawCall);
    out << ",\"resources\":{\"uniform_buffer\":";
    writeResource(out, render.uniformBuffer);
    out << ",\"color_target\":";
    writeResource(out, render.colorTarget);
    out << "},\"framebuffer\":";
    writeFramebuffer(out, render.framebuffer);
    out << ",\"artifact\":" << quote(artifact) << '}';
}

void writeDiff(std::ostream& out, const DiffStats& diff)
{
    out << "{\"total_pixels\":" << diff.totalPixels
        << ",\"changed_pixels\":" << diff.changedPixels
        << ",\"changed_ratio\":" << number(diff.changedRatio)
        << ",\"mean_abs\":" << number(diff.meanAbs)
        << ",\"rmse\":" << number(diff.rmse)
        << ",\"max_abs\":" << diff.maxAbs
        << ",\"visual_change\":" << (diff.changedPixels != 0u ? "true" : "false")
        << ",\"bounds\":";
    if (diff.hasBounds) {
        out << "{\"x\":" << diff.minX << ",\"y\":" << diff.minY
            << ",\"width\":" << (diff.maxX - diff.minX + 1u)
            << ",\"height\":" << (diff.maxY - diff.minY + 1u) << '}';
    } else {
        out << "null";
    }
    out << '}';
}

std::string snapshotJson(const std::string& side, const SnapshotInfo& snapshot)
{
    std::ostringstream out;
    out << "{\"side\":" << quote(side) << ",\"label\":" << quote(snapshot.label)
        << ",\"path\":" << quote(snapshot.path)
        << ",\"format\":" << quote(snapshot.formatName)
        << ",\"format_version\":" << snapshot.formatVersion
        << ",\"record_count\":" << snapshot.declaredRecordCount
        << ",\"record_stride\":" << snapshot.recordStride
        << ",\"snapshot_id\":" << snapshot.snapshotId
        << ",\"file_size\":" << snapshot.fileSize
        << ",\"record_data_offset\":" << snapshot.recordDataOffset
        << ",\"sha256\":" << quote(snapshot.sha256) << '}';
    return out.str();
}

std::string anomalyJson(const RecordAnomaly& anomaly)
{
    std::ostringstream out;
    out << "{\"kind\":" << quote(anomaly.kind)
        << ",\"detail\":" << quote(anomaly.detail)
        << ",\"object_id\":" << anomaly.objectId
        << ",\"raw_offset\":" << anomaly.rawOffset
        << ",\"raw_size\":" << anomaly.raw.size()
        << ",\"raw_hex\":" << quote(hexBytes(anomaly.raw)) << '}';
    return out.str();
}

}  // namespace

std::string utcNow()
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &value);
#else
    gmtime_r(&value, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

void writeReviewArtifacts(const ReviewResult& result)
{
    const std::filesystem::path output(result.outputDirectory);
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) {
        throw std::runtime_error("cannot create review directory: " +
                                 output.string() + ": " + error.message());
    }

    std::ofstream records(output / "records.jsonl",
                          std::ios::binary | std::ios::trunc);
    if (!records) {
        throw std::runtime_error("cannot write records.jsonl");
    }
    for (const ObjectReview& object : result.objects) {
        records << "{\"schema\":\"lshc-review/1\",\"object_id\":"
                << object.objectId << ",\"status\":" << quote(object.status)
                << ",\"legacy_record\":";
        if (object.legacyRecord) {
            writeRecord(records, *object.legacyRecord);
        } else {
            records << "null";
        }
        records << ",\"current_record\":";
        if (object.currentRecord) {
            writeRecord(records, *object.currentRecord);
        } else {
            records << "null";
        }

        records << ",\"draw_call\":{\"legacy\":";
        records << (object.legacyRecord
                        ? std::to_string(object.legacyRecord->drawCall)
                        : "null")
                << ",\"current\":";
        records << (object.currentRecord
                        ? std::to_string(object.currentRecord->drawCall)
                        : "null")
                << ",\"stable\":";
        records << ((object.legacyRecord && object.currentRecord)
                        ? (object.legacyRecord->drawCall ==
                                   object.currentRecord->drawCall
                               ? "true"
                               : "false")
                        : "false")
                << "},";

        records << "\"renders\":{\"legacy\":";
        if (object.legacyRender) {
            writeRender(records, *object.legacyRender, object.legacyFrame);
        } else {
            records << "null";
        }
        records << ",\"current\":";
        if (object.currentRender) {
            writeRender(records, *object.currentRender, object.currentFrame);
        } else {
            records << "null";
        }
        records << "},\"framebuffer_sha_equal\":";
        const bool frameHashesEqual =
            object.legacyRender && object.currentRender &&
            object.legacyRender->framebuffer.contentSha256 ==
                object.currentRender->framebuffer.contentSha256;
        records << (frameHashesEqual ? "true" : "false") << ',';

        records << "\"visual_diff\":";
        if (object.diff) {
            writeDiff(records, *object.diff);
        } else {
            records << "null";
        }
        records << ",\"evidence\":{\"legacy_frame\":";
        records << (object.legacyFrame.empty() ? "null"
                                               : quote(object.legacyFrame));
        records << ",\"current_frame\":";
        records << (object.currentFrame.empty() ? "null"
                                                : quote(object.currentFrame));
        records << ",\"diff_frame\":";
        records << (object.diffFrame.empty() ? "null" : quote(object.diffFrame));
        records << "},\"errors\":{\"legacy\":";
        records << (object.legacyError.empty() ? "null"
                                               : quote(object.legacyError));
        records << ",\"current\":";
        records << (object.currentError.empty() ? "null"
                                                : quote(object.currentError));
        records << "},\"continued_after_failure\":"
                << (object.continuedAfterFailure ? "true" : "false") << "}\n";
    }
    if (!records) {
        throw std::runtime_error("failed writing records.jsonl");
    }
    records.close();

    std::map<std::string, uint64_t> statusCounts;
    for (const ObjectReview& object : result.objects) {
        ++statusCounts[object.status];
    }

    std::ofstream manifest(output / "manifest.json",
                           std::ios::binary | std::ios::trunc);
    if (!manifest) {
        throw std::runtime_error("cannot write manifest.json");
    }
    manifest << "{\n  \"schema\": \"lshc-review/1\",\n"
             << "  \"tool\": {\"name\": \"lshc_review\", \"version\": \""
             << LSHC_VERSION
             << "\", \"language\": \"C++17\", \"graphics\": [\"Vulkan\", \"GLFW\"]},\n"
             << "  \"started_at\": " << quote(result.startedAtUtc) << ",\n"
             << "  \"completed_at\": " << quote(result.completedAtUtc)
             << ",\n  \"snapshots\": [\n    "
             << snapshotJson("legacy", result.legacySnapshot) << ",\n    "
             << snapshotJson("current", result.currentSnapshot)
             << "\n  ],\n  \"truncated_record\": ";
    if (result.truncated) {
        manifest << "{\"declared_index\":" << result.truncated->declaredIndex
                 << ",\"offset\":" << result.truncated->offset
                 << ",\"available_bytes\":" << result.truncated->availableBytes
                 << ",\"expected_bytes\":" << result.truncated->expectedBytes
                 << ",\"partial_object_id\":";
        manifest << (result.truncated->hasObjectId
                         ? std::to_string(result.truncated->partialObjectId)
                         : "null");
        manifest << ",\"partial_draw_call\":";
        manifest << (result.truncated->hasDrawCall
                         ? std::to_string(result.truncated->partialDrawCall)
                         : "null");
        manifest << ",\"raw_tail\":" << quote(result.truncatedTailRelative)
                 << ",\"raw_hex\":" << quote(hexBytes(result.truncated->rawTail))
                 << '}';
    } else {
        manifest << "null";
    }

    manifest << ",\n  \"anomalies\": [\n";
    bool firstAnomaly = true;
    auto appendAnomalies = [&](const std::string& side,
                               const std::vector<RecordAnomaly>& anomalies) {
        for (const RecordAnomaly& anomaly : anomalies) {
            if (!firstAnomaly) {
                manifest << ",\n";
            }
            firstAnomaly = false;
            std::ostringstream entry;
            entry << "{\"side\":" << quote(side) << ','
                  << anomalyJson(anomaly).substr(1);
            manifest << "    " << entry.str();
        }
    };
    appendAnomalies("legacy", result.legacyAnomalies);
    appendAnomalies("current", result.currentAnomalies);
    manifest << "\n  ],\n  \"object_counts\": {";
    bool firstCount = true;
    for (const auto& entry : statusCounts) {
        if (!firstCount) {
            manifest << ", ";
        }
        firstCount = false;
        manifest << quote(entry.first) << ": " << entry.second;
    }
    manifest << "},\n  \"runtime\": {\"glfw_initialized\":"
             << (result.runtime.glfwInitialized ? "true" : "false")
             << ",\"glfw_vulkan_supported\":"
             << (result.runtime.glfwVulkanSupported ? "true" : "false")
             << ",\"glfw_version\":" << quote(result.runtime.glfwVersion)
             << ",\"glfw_error\":" << quote(result.runtime.glfwError)
             << ",\"backend\":" << quote(result.runtime.backend)
             << ",\"vulkan_api\":" << quote(result.runtime.vulkanApi)
             << ",\"device\":" << quote(result.runtime.deviceName)
             << ",\"driver\":" << quote(result.runtime.driverName)
             << ",\"instance_extensions\":[";
    for (std::size_t index = 0;
         index < result.runtime.instanceExtensions.size(); ++index) {
        if (index != 0u) {
            manifest << ',';
        }
        manifest << quote(result.runtime.instanceExtensions[index]);
    }
    manifest << "]},\n  \"cleanup\": {\"temp_directory\":"
             << quote(result.cleanup.tempDirectory)
             << ",\"temp_removed\":"
             << (result.cleanup.tempRemoved ? "true" : "false")
             << ",\"vulkan_shutdown\":"
             << (result.cleanup.vulkanShutdown ? "true" : "false")
             << ",\"live_transient_objects\":"
             << result.cleanup.liveTransientObjects << "},\n  \"warnings\":[";
    for (std::size_t index = 0; index < result.warnings.size(); ++index) {
        if (index != 0u) {
            manifest << ',';
        }
        manifest << quote(result.warnings[index]);
    }
    manifest << "]\n}\n";
    if (!manifest) {
        throw std::runtime_error("failed writing manifest.json");
    }
    manifest.close();

    std::ofstream markdown(output / "review.md",
                           std::ios::binary | std::ios::trunc);
    if (!markdown) {
        throw std::runtime_error("cannot write review.md");
    }
    markdown << "# Light shadow cache review\n\n"
             << "- Legacy snapshot: `" << result.legacySnapshot.label
             << "` (`" << result.legacySnapshot.formatName << "`, "
             << result.legacySnapshot.sha256 << ")\n"
             << "- Current snapshot: `" << result.currentSnapshot.label
             << "` (`" << result.currentSnapshot.formatName << "`, "
             << result.currentSnapshot.sha256 << ")\n"
             << "- Runtime: " << result.runtime.backend << " on "
             << result.runtime.deviceName << " / " << result.runtime.glfwVersion
             << "\n\n"
             << "## Truncated record\n\n";
    if (result.truncated) {
        markdown << "The declared table has an undecodable tail at offset "
                 << result.truncated->offset << " ("
                 << result.truncated->availableBytes << " of "
                 << result.truncated->expectedBytes << " bytes). ";
        if (result.truncated->hasObjectId) {
            markdown << "Partial object id is `"
                     << result.truncated->partialObjectId << "`. ";
        }
        markdown << "Raw bytes are preserved in `"
                 << result.truncatedTailRelative << "`.\n\n";
    } else {
        markdown << "No truncated record was found.\n\n";
    }

    markdown << "## Object correlation\n\n"
             << "| Object | Status | Legacy draw | Current draw | Changed px | "
                "Mean abs | Framebuffer SHA | Evidence |\n"
             << "|---:|---|---:|---:|---:|---:|---|---|\n";
    for (const ObjectReview& object : result.objects) {
        markdown << "| " << object.objectId << " | `" << object.status << "`";
        markdown << " | " << (object.legacyRecord
                                  ? std::to_string(object.legacyRecord->drawCall)
                                  : "-");
        markdown << " | " << (object.currentRecord
                                  ? std::to_string(object.currentRecord->drawCall)
                                  : "-");
        markdown << " | "
                 << (object.diff ? std::to_string(object.diff->changedPixels)
                                 : "-");
        markdown << " | " << (object.diff ? number(object.diff->meanAbs) : "-");
        markdown << " | ";
        const std::string frameHash =
            object.legacyRender
                ? object.legacyRender->framebuffer.contentSha256
                : (object.currentRender
                       ? object.currentRender->framebuffer.contentSha256
                       : "");
        markdown << (frameHash.empty() ? "-" : frameHash.substr(0u, 16u))
                 << " | ";
        if (!object.legacyFrame.empty()) {
            markdown << "[legacy](" << object.legacyFrame << ") ";
        }
        if (!object.currentFrame.empty()) {
            markdown << "[current](" << object.currentFrame << ") ";
        }
        if (!object.diffFrame.empty()) {
            markdown << "[diff](" << object.diffFrame << ")";
        }
        if (object.legacyFrame.empty() && object.currentFrame.empty() &&
            object.diffFrame.empty()) {
            markdown << "-";
        }
        markdown << " |\n";
    }

    markdown << "\n## Exceptions and continuation\n\n";
    bool hadFailure = false;
    for (const ObjectReview& object : result.objects) {
        if (object.legacyError.empty() && object.currentError.empty()) {
            continue;
        }
        hadFailure = true;
        markdown << "- Object " << object.objectId << ": ";
        if (!object.legacyError.empty()) {
            markdown << "legacy: " << object.legacyError << ". ";
        }
        if (!object.currentError.empty()) {
            markdown << "current: " << object.currentError << ". ";
        }
        markdown << (object.continuedAfterFailure
                         ? "Processing continued with the remaining objects."
                         : "No remaining object needed processing.");
        markdown << '\n';
    }
    if (!hadFailure) {
        markdown << "No per-object exception was raised.\n";
    }

    markdown << "\n## Cleanup\n\n"
             << "- Temporary directory: `" << result.cleanup.tempDirectory
             << "`\n"
             << "- Removed: " << (result.cleanup.tempRemoved ? "yes" : "no")
             << "\n"
             << "- Vulkan shutdown: "
             << (result.cleanup.vulkanShutdown ? "yes" : "no") << "\n"
             << "- Live transient Vulkan objects: "
             << result.cleanup.liveTransientObjects << "\n";
    if (!result.warnings.empty()) {
        markdown << "\n## Warnings\n\n";
        for (const std::string& warning : result.warnings) {
            markdown << "- " << warning << '\n';
        }
    }
    if (!markdown) {
        throw std::runtime_error("failed writing review.md");
    }
    markdown.close();

    std::ofstream cleanup(output / "cleanup.json",
                          std::ios::binary | std::ios::trunc);
    if (!cleanup) {
        throw std::runtime_error("cannot write cleanup.json");
    }
    cleanup << "{\"temp_directory\":"
            << quote(result.cleanup.tempDirectory) << ",\"temp_removed\":"
            << (result.cleanup.tempRemoved ? "true" : "false")
            << ",\"vulkan_shutdown\":"
            << (result.cleanup.vulkanShutdown ? "true" : "false")
            << ",\"live_transient_objects\":"
            << result.cleanup.liveTransientObjects << "}\n";
    if (!cleanup) {
        throw std::runtime_error("failed writing cleanup.json");
    }
    cleanup.close();

    std::vector<std::filesystem::path> files;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(output)) {
        if (!entry.is_regular_file() ||
            entry.path().filename() == "checksums.sha256") {
            continue;
        }
        files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::ofstream checksums(output / "checksums.sha256",
                            std::ios::binary | std::ios::trunc);
    if (!checksums) {
        throw std::runtime_error("cannot write checksums.sha256");
    }
    for (const std::filesystem::path& file : files) {
        const std::string relative =
            std::filesystem::relative(file, output).generic_string();
        checksums << sha256File(file.string()) << "  " << relative << '\n';
    }
    if (!checksums) {
        throw std::runtime_error("failed writing checksums.sha256");
    }
}

}  // namespace lshc


