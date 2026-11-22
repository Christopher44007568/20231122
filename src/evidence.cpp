#include "evidence.hpp"

#include "checksum.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace rr {
namespace {

void writeIndent(std::ostream& stream, int indent) {
    for (int i = 0; i < indent; ++i) {
        stream.put(' ');
    }
}

void dumpJson(std::ostream& stream, const JsonValue& value, int indent, int step) {
    if (value.isNull()) {
        stream << "null";
    } else if (value.isBool()) {
        stream << (value.asBool() ? "true" : "false");
    } else if (value.isNumber()) {
        stream << jsonNumber(value.asNumber());
    } else if (value.isString()) {
        stream << jsonEscape(value.asString());
    } else if (value.isArray()) {
        const auto& array = value.asArray();
        if (array.empty()) {
            stream << "[]";
            return;
        }
        stream << "[\n";
        for (std::size_t i = 0; i < array.size(); ++i) {
            writeIndent(stream, indent + step);
            dumpJson(stream, array[i], indent + step, step);
            if (i + 1U != array.size()) {
                stream.put(',');
            }
            stream.put('\n');
        }
        writeIndent(stream, indent);
        stream.put(']');
    } else {
        const auto& object = value.asObject();
        if (object.empty()) {
            stream << "{}";
            return;
        }
        stream << "{\n";
        for (std::size_t i = 0; i < object.size(); ++i) {
            writeIndent(stream, indent + step);
            stream << jsonEscape(object[i].first) << ": ";
            dumpJson(stream, object[i].second, indent + step, step);
            if (i + 1U != object.size()) {
                stream.put(',');
            }
            stream.put('\n');
        }
        writeIndent(stream, indent);
        stream.put('}');
    }
}

JsonValue array2(double a, double b) {
    JsonValue value = JsonValue::array();
    value.asArray().push_back(JsonValue::number(a));
    value.asArray().push_back(JsonValue::number(b));
    return value;
}

JsonValue array4(const std::array<std::uint8_t, 4>& rgba) {
    JsonValue value = JsonValue::array();
    for (const auto channel : rgba) {
        value.asArray().push_back(JsonValue::number(static_cast<double>(channel)));
    }
    return value;
}

JsonValue diagnosticsJson(const std::vector<Diagnostic>& diagnostics) {
    JsonValue array = JsonValue::array();
    for (const auto& diagnostic : diagnostics) {
        JsonValue item = JsonValue::object();
        item.set("path", JsonValue::string(diagnostic.path));
        item.set("message", JsonValue::string(diagnostic.message));
        array.asArray().push_back(std::move(item));
    }
    return array;
}

JsonValue stringsJson(const std::vector<std::string>& values) {
    JsonValue array = JsonValue::array();
    for (const auto& value : values) {
        array.asArray().push_back(JsonValue::string(value));
    }
    return array;
}

JsonValue diffStatsJson(const DiffStats& stats) {
    JsonValue value = JsonValue::object();
    value.set("comparable", JsonValue::boolean(stats.comparable));
    value.set("changed_pixels", JsonValue::number(static_cast<double>(stats.changedPixels)));
    value.set("total_pixels", JsonValue::number(static_cast<double>(stats.totalPixels)));
    value.set("max_channel_delta", JsonValue::number(stats.maxChannelDelta));
    value.set("mean_absolute_error", JsonValue::number(stats.meanAbsoluteError));
    value.set("first_difference",
              stats.hasFirstDiff
                  ? array2(static_cast<double>(stats.firstDiffX),
                           static_cast<double>(stats.firstDiffY))
                  : JsonValue::null());
    if (stats.box.empty) {
        value.set("changed_bbox", JsonValue::null());
    } else {
        JsonValue box = JsonValue::array();
        box.asArray().push_back(JsonValue::number(stats.box.minX));
        box.asArray().push_back(JsonValue::number(stats.box.minY));
        box.asArray().push_back(JsonValue::number(stats.box.maxX));
        box.asArray().push_back(JsonValue::number(stats.box.maxY));
        value.set("changed_bbox", std::move(box));
    }
    return value;
}

bool readFile(const std::filesystem::path& path, std::string& out, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open " + path.string();
        return false;
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        error = "cannot read " + path.string();
        return false;
    }
    out = contents.str();
    return true;
}

bool writeFile(const std::filesystem::path& path,
               const std::string& contents,
               std::string& error) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot create " + path.string();
        return false;
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!stream) {
        error = "cannot write " + path.string();
        return false;
    }
    return true;
}

std::string hashFile(const std::filesystem::path& path) {
    std::string contents;
    std::string error;
    if (!readFile(path, contents, error)) {
        return {};
    }
    return sha256Hex(contents);
}

}  // namespace

std::string safeName(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        const bool allowed = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                             (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        out.push_back(allowed ? c : '_');
    }
    if (out.empty() || out == "." || out == "..") {
        out = "item";
    }
    return out;
}

EvidenceWriter::EvidenceWriter(std::filesystem::path outputDirectory)
    : directory_(std::move(outputDirectory)) {}

const std::filesystem::path& EvidenceWriter::directory() const {
    return directory_;
}

std::string EvidenceWriter::absolutePath(const std::string& relativePath) const {
    return (directory_ / relativePath).string();
}

bool EvidenceWriter::initialize(std::string& error) {
    std::error_code code;
    std::filesystem::create_directories(directory_, code);
    if (code) {
        error = "cannot create evidence directory: " + code.message();
        return false;
    }
    return true;
}

bool EvidenceWriter::recordArtifact(const std::string& relativePath, std::string& error) {
    const std::filesystem::path relative(relativePath);
    if (relative.empty() || relative.is_absolute()) {
        error = "artifact path must be relative";
        return false;
    }
    for (const auto& component : relative) {
        if (component == "..") {
            error = "artifact path must not escape the evidence directory";
            return false;
        }
    }
    const auto absolute = directory_ / relative;
    std::error_code code;
    if (!std::filesystem::exists(absolute, code)) {
        error = "artifact does not exist: " + relativePath;
        return false;
    }
    recordArtifactFromPath(relative.generic_string(), absolute);
    return true;
}

bool EvidenceWriter::saveText(const std::string& relativePath,
                              const std::string& text,
                              std::string* storedPath,
                              std::string& error) {
    const auto path = directory_ / relativePath;
    if (!writeFile(path, text, error)) {
        return false;
    }
    if (!recordArtifact(relativePath, error)) {
        return false;
    }
    if (storedPath) {
        *storedPath = relativePath;
    }
    return true;
}

bool EvidenceWriter::saveImage(const std::string& relativePath,
                               const Image& image,
                               std::string* storedPath,
                               std::string& error) {
    const auto path = directory_ / relativePath;
    if (!writePpm(path.string(), image)) {
        error = "cannot write image " + relativePath;
        return false;
    }
    if (!recordArtifact(relativePath, error)) {
        return false;
    }
    if (storedPath) {
        *storedPath = relativePath;
    }
    return true;
}

bool EvidenceWriter::savePam(const std::string& relativePath,
                             const Image& image,
                             std::string* storedPath,
                             std::string& error) {
    const auto path = directory_ / relativePath;
    if (!writePam(path.string(), image)) {
        error = "cannot write image " + relativePath;
        return false;
    }
    if (!recordArtifact(relativePath, error)) {
        return false;
    }
    if (storedPath) {
        *storedPath = relativePath;
    }
    return true;
}

bool EvidenceWriter::saveDiff(const std::string& relativePath,
                              const Image& before,
                              const Image& after,
                              std::string* storedPath,
                              std::string& error) {
    const auto path = directory_ / relativePath;
    if (!writeDiffPpm(path.string(), before, after)) {
        error = "cannot write diff image " + relativePath;
        return false;
    }
    if (!recordArtifact(relativePath, error)) {
        return false;
    }
    if (storedPath) {
        *storedPath = relativePath;
    }
    return true;
}

void EvidenceWriter::recordArtifactFromPath(
    const std::string& relativePath,
    const std::filesystem::path& absolute) {
    std::error_code code;
    const auto size = std::filesystem::file_size(absolute, code);
    ArtifactRecord record;
    record.path = relativePath;
    record.bytes = code ? 0U : static_cast<std::uint64_t>(size);
    record.sha256 = hashFile(absolute);
    for (auto& existing : artifacts_) {
        if (existing.path == relativePath) {
            existing = std::move(record);
            return;
        }
    }
    artifacts_.push_back(std::move(record));
}

bool EvidenceWriter::finish(RunEvidence evidence, std::string& error) {
    evidence.artifacts = artifacts_;

    JsonValue root = JsonValue::object();
    root.set("schema", JsonValue::string("raster-regression-evidence/v1"));
    root.set("status", JsonValue::string(evidence.status));
    root.set("generated_at", JsonValue::string(evidence.generatedAt));
    root.set("duration_ms",
             JsonValue::number(static_cast<double>(evidence.durationMilliseconds)));

    JsonValue input = JsonValue::object();
    input.set("path", JsonValue::string(evidence.inputPath));
    input.set("bytes", JsonValue::number(static_cast<double>(evidence.inputBytes)));
    input.set("sha256", JsonValue::string(evidence.inputSha256));
    input.set("sha256_scope", JsonValue::string(evidence.inputSha256Scope));
    input.set("empty", JsonValue::boolean(evidence.emptyInput));
    root.set("input", std::move(input));

    JsonValue scenario = JsonValue::object();
    scenario.set("name", JsonValue::string(evidence.scenarioName));
    scenario.set("framebuffer_width", JsonValue::number(evidence.scenarioWidth));
    scenario.set("framebuffer_height", JsonValue::number(evidence.scenarioHeight));
    scenario.set("vertices", JsonValue::number(static_cast<double>(evidence.vertices)));
    scenario.set("indices", JsonValue::number(static_cast<double>(evidence.indices)));
    scenario.set("rules", JsonValue::number(static_cast<double>(evidence.rules)));
    scenario.set("draws", JsonValue::number(static_cast<double>(evidence.draws)));
    scenario.set("passes", JsonValue::number(static_cast<double>(evidence.passes)));
    scenario.set("shared_edges",
                 JsonValue::number(static_cast<double>(evidence.sharedEdges)));
    scenario.set("reference_pixels",
                 JsonValue::number(static_cast<double>(evidence.referencePixels)));
    scenario.set("draw_submissions",
                 JsonValue::number(static_cast<double>(evidence.totalDrawSubmissions)));
    root.set("scenario", std::move(scenario));

    JsonValue limits = JsonValue::object();
    limits.set("max_input_bytes",
               JsonValue::number(static_cast<double>(evidence.limits.max_input_bytes)));
    limits.set("max_vertices",
               JsonValue::number(static_cast<double>(evidence.limits.max_vertices)));
    limits.set("max_indices",
               JsonValue::number(static_cast<double>(evidence.limits.max_indices)));
    limits.set("max_rules",
               JsonValue::number(static_cast<double>(evidence.limits.max_rules)));
    limits.set("max_draws",
               JsonValue::number(static_cast<double>(evidence.limits.max_draws)));
    limits.set("max_passes",
               JsonValue::number(static_cast<double>(evidence.limits.max_passes)));
    limits.set("max_reference_pixels",
               JsonValue::number(static_cast<double>(evidence.limits.max_reference_pixels)));
    limits.set("max_shared_edges",
               JsonValue::number(static_cast<double>(evidence.limits.max_shared_edges)));
    limits.set("max_total_draw_submissions",
               JsonValue::number(static_cast<double>(
                   evidence.limits.max_total_draw_submissions)));
    limits.set("max_saved_draw_images",
               JsonValue::number(static_cast<double>(
                   evidence.limits.max_saved_draw_images)));
    limits.set("max_reported_reference_pixels",
               JsonValue::number(static_cast<double>(
                   evidence.limits.max_reported_reference_pixels)));
    limits.set("max_reported_edge_samples",
               JsonValue::number(static_cast<double>(
                   evidence.limits.max_reported_edge_samples)));
    root.set("limits", std::move(limits));

    JsonValue environment = JsonValue::object();
    environment.set("language", JsonValue::string("C++17"));
    environment.set("version", JsonValue::string(RR_VERSION));
    environment.set("build_type", JsonValue::string(RR_BUILD_TYPE));
    JsonValue platform = JsonValue::object();
    platform.set("glfw_initialized", JsonValue::boolean(evidence.platform.initialized));
    platform.set("glfw_version", JsonValue::string(evidence.platform.versionString));
    platform.set("glfw_vulkan_supported",
                 JsonValue::boolean(evidence.platform.vulkanSupported));
    platform.set("glfw_error", JsonValue::string(evidence.platform.error));
    platform.set("required_vulkan_extensions",
                 stringsJson(evidence.platform.requiredVulkanExtensions));
    environment.set("platform", std::move(platform));
    JsonValue device = JsonValue::object();
    device.set("name", JsonValue::string(evidence.device.name));
    device.set("driver_name", JsonValue::string(evidence.device.driverName));
    device.set("driver_info", JsonValue::string(evidence.device.driverInfo));
    device.set("api_version",
               JsonValue::string(vulkanVersionString(evidence.device.apiVersion)));
    device.set("loader_version", JsonValue::string(evidence.device.vulkanLoaderVersion));
    device.set("vendor_id", JsonValue::number(evidence.device.vendorId));
    device.set("device_id", JsonValue::number(evidence.device.deviceId));
    device.set("device_type", JsonValue::string(evidence.device.deviceType));
    device.set("queue_family", JsonValue::number(evidence.device.queueFamily));
    device.set("wide_lines", JsonValue::boolean(evidence.device.wideLines));
    device.set("fill_mode_non_solid",
               JsonValue::boolean(evidence.device.fillModeNonSolid));
    device.set("depth_clamp", JsonValue::boolean(evidence.device.depthClamp));
    device.set("max_framebuffer_width",
               JsonValue::number(evidence.device.maxFramebufferWidth));
    device.set("max_framebuffer_height",
               JsonValue::number(evidence.device.maxFramebufferHeight));
    environment.set("device", std::move(device));
    root.set("environment", std::move(environment));
    root.set("warnings", diagnosticsJson(evidence.warnings));
    root.set("errors", diagnosticsJson(evidence.errors));

    JsonValue passes = JsonValue::array();
    for (const auto& pass : evidence.passEvidence) {
        JsonValue item = JsonValue::object();
        item.set("id", JsonValue::string(pass.id));
        item.set("rule", JsonValue::string(pass.ruleId));
        item.set("pipeline_state", ruleToJson(pass.effectiveRule));
        item.set("warnings", stringsJson(pass.warnings));
        item.set("elapsed_us",
                 JsonValue::number(static_cast<double>(pass.elapsedMicroseconds)));

        JsonValue resources = JsonValue::object();
        JsonValue vertexBuffer = JsonValue::object();
        vertexBuffer.set("usage", JsonValue::string("VERTEX|TRANSFER_DST"));
        vertexBuffer.set("bytes",
                         JsonValue::number(static_cast<double>(pass.vertexBufferBytes)));
        resources.set("vertex_buffer", std::move(vertexBuffer));
        JsonValue indexBuffer = JsonValue::object();
        indexBuffer.set("usage", JsonValue::string("INDEX|TRANSFER_DST"));
        indexBuffer.set("bytes",
                        JsonValue::number(static_cast<double>(pass.indexBufferBytes)));
        resources.set("index_buffer", std::move(indexBuffer));
        JsonValue attachment = JsonValue::object();
        attachment.set("format", JsonValue::string("VK_FORMAT_R8G8B8A8_UNORM"));
        attachment.set("extent", array2(pass.framebufferWidth, pass.framebufferHeight));
        attachment.set("usage",
                       JsonValue::string("COLOR_ATTACHMENT|TRANSFER_SRC|TRANSFER_DST"));
        JsonValue layouts = JsonValue::array();
        for (const char* layout : {"VK_IMAGE_LAYOUT_UNDEFINED",
                                   "VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL",
                                   "VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL",
                                   "VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL"}) {
            layouts.asArray().push_back(JsonValue::string(layout));
        }
        attachment.set("layout_sequence", std::move(layouts));
        resources.set("color_attachment", std::move(attachment));
        item.set("resources", std::move(resources));

        JsonValue frame = JsonValue::object();
        frame.set("initial_sha256", JsonValue::string(pass.initialSha256));
        frame.set("final_sha256", JsonValue::string(pass.finalSha256));
        frame.set("ppm", JsonValue::string(pass.framePpm));
        frame.set("pam", JsonValue::string(pass.framePam));
        frame.set("change_from_clear_ppm", JsonValue::string(pass.changeDiffPpm));
        frame.set("change_from_clear", diffStatsJson(pass.changedFromClear));
        item.set("framebuffer", std::move(frame));

        JsonValue draws = JsonValue::array();
        for (const auto& draw : pass.draws) {
            JsonValue drawJson = JsonValue::object();
            drawJson.set("sequence",
                         JsonValue::number(static_cast<double>(draw.sequence)));
            drawJson.set("id", JsonValue::string(draw.id));
            drawJson.set("vkCmdDrawIndexed",
                         JsonValue::string("index_count=" +
                                           std::to_string(draw.indexCount) +
                                           ",instance_count=" +
                                           std::to_string(draw.instanceCount) +
                                           ",first_index=" +
                                           std::to_string(draw.firstIndex)));
            drawJson.set("before_sha256", JsonValue::string(draw.beforeSha256));
            drawJson.set("after_sha256", JsonValue::string(draw.afterSha256));
            drawJson.set("framebuffer_change", diffStatsJson(draw.change));
            drawJson.set("saved_frame", JsonValue::string(draw.savedFramePath));
            drawJson.set("saved_frame_sha256",
                         JsonValue::string(draw.savedFrameSha256));
            drawJson.set("resources",
                         JsonValue::string("vertex_buffer,index_buffer,color_attachment"));
            draws.asArray().push_back(std::move(drawJson));
        }
        item.set("draw_calls", std::move(draws));

        JsonValue references = JsonValue::object();
        references.set("total",
                       JsonValue::number(static_cast<double>(pass.references.total)));
        references.set("failed",
                       JsonValue::number(static_cast<double>(pass.references.failed)));
        references.set("omitted",
                       JsonValue::number(static_cast<double>(pass.references.omitted)));
        JsonValue checks = JsonValue::array();
        for (const auto& check : pass.references.reported) {
            JsonValue checkJson = JsonValue::object();
            checkJson.set("pass", JsonValue::string(check.pass));
            checkJson.set("xy", array2(check.x, check.y));
            checkJson.set("expected", array4(check.expected));
            checkJson.set("actual", array4(check.actual));
            checkJson.set("tolerance", JsonValue::number(check.tolerance));
            checkJson.set("max_delta", JsonValue::number(check.maxDelta));
            checkJson.set("passed", JsonValue::boolean(check.passed));
            checks.asArray().push_back(std::move(checkJson));
        }
        references.set("checks", std::move(checks));
        item.set("reference_pixels", std::move(references));
        passes.asArray().push_back(std::move(item));
    }
    root.set("passes", std::move(passes));

    JsonValue edges = JsonValue::array();
    for (const auto& edge : evidence.edgeEvidence) {
        JsonValue item = JsonValue::object();
        item.set("id", JsonValue::string(edge.id));
        item.set("draw_a", JsonValue::string(edge.drawA));
        item.set("draw_b", JsonValue::string(edge.drawB));
        item.set("clip_a", array2(edge.a[0], edge.a[1]));
        item.set("clip_b", array2(edge.b[0], edge.b[1]));
        JsonValue samples = JsonValue::array();
        for (const auto& sample : edge.samples) {
            JsonValue sampleJson = JsonValue::object();
            sampleJson.set("pass", JsonValue::string(sample.pass));
            sampleJson.set("probe", JsonValue::string(sample.probe));
            sampleJson.set("xy", array2(static_cast<double>(sample.x),
                                        static_cast<double>(sample.y)));
            sampleJson.set("rgba", array4(sample.rgba));
            samples.asArray().push_back(std::move(sampleJson));
        }
        item.set("samples", std::move(samples));
        edges.asArray().push_back(std::move(item));
    }
    root.set("shared_edges", std::move(edges));

    if (evidence.boundary.present) {
        const auto& boundary = evidence.boundary;
        JsonValue value = JsonValue::object();
        value.set("parameter", JsonValue::string(boundary.parameter));
        value.set("threshold", JsonValue::number(boundary.threshold));
        value.set("before_pass", JsonValue::string(boundary.beforePass));
        value.set("after_pass", JsonValue::string(boundary.afterPass));
        value.set("before_value", JsonValue::string(boundary.beforeValue));
        value.set("after_value", JsonValue::string(boundary.afterValue));
        value.set("before_requested_value",
                  JsonValue::string(boundary.beforeRequestedValue));
        value.set("after_requested_value",
                  JsonValue::string(boundary.afterRequestedValue));
        value.set("before_image", JsonValue::string(boundary.beforeImage));
        value.set("after_image", JsonValue::string(boundary.afterImage));
        value.set("diff_image", JsonValue::string(boundary.diffImage));
        value.set("image_diff", diffStatsJson(boundary.diff));
        value.set("changed", JsonValue::boolean(boundary.diff.changedPixels > 0U));
        if (boundary.hasSamplePixel) {
            JsonValue sample = JsonValue::object();
            sample.set("xy", array2(boundary.sampleX, boundary.sampleY));
            sample.set("before", array4(boundary.beforeRgba));
            sample.set("after", array4(boundary.afterRgba));
            sample.set("max_delta", JsonValue::number(boundary.sampleMaxDelta));
            sample.set("changed",
                       JsonValue::boolean(boundary.sampleMaxDelta > 0U));
            value.set("sample_pixel", std::move(sample));
        }
        root.set("boundary", std::move(value));
    } else {
        root.set("boundary", JsonValue::null());
    }

    JsonValue artifacts = JsonValue::array();
    for (const auto& artifact : artifacts_) {
        JsonValue item = JsonValue::object();
        item.set("path", JsonValue::string(artifact.path));
        item.set("bytes", JsonValue::number(static_cast<double>(artifact.bytes)));
        item.set("sha256", JsonValue::string(artifact.sha256));
        artifacts.asArray().push_back(std::move(item));
    }
    root.set("artifacts", std::move(artifacts));

    std::ostringstream manifest;
    dumpJson(manifest, root, 0, 2);
    manifest.put('\n');
    const std::string manifestText = manifest.str();
    if (!writeFile(directory_ / "manifest.json", manifestText, error)) {
        return false;
    }

    std::ostringstream checksums;
    for (const auto& artifact : artifacts_) {
        checksums << artifact.sha256 << "  " << artifact.path << '\n';
    }
    checksums << hashFile(directory_ / "manifest.json") << "  manifest.json\n";
    return writeFile(directory_ / "evidence.sha256", checksums.str(), error);
}

}  // namespace rr

