#include "runner.h"

#include "core/scene.h"
#include "core/timestamp.h"
#include "core/util.h"
#include "evidence.h"
#include "render/renderer.h"

#include <sys/stat.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace ssor {
namespace {

std::string prefixSha256(const std::string& path, std::size_t bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return "";
    std::string data(bytes, '\0');
    in.read(&data[0], static_cast<std::streamsize>(bytes));
    data.resize(static_cast<std::size_t>(in.gcount()));
    return sha256Hex(data);
}

std::string relative(const std::string& root, const std::string& path) {
    return std::filesystem::relative(path, root).generic_string();
}

std::string modeString(const std::string& path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec) return "unknown";
    const auto perms = status.permissions();
    std::string mode;
    mode += (perms & std::filesystem::perms::owner_read) !=
                    std::filesystem::perms::none
                ? "r"
                : "-";
    mode += (perms & std::filesystem::perms::owner_write) !=
                    std::filesystem::perms::none
                ? "w"
                : "-";
    mode += (perms & std::filesystem::perms::owner_exec) !=
                    std::filesystem::perms::none
                ? "x"
                : "-";
    return mode;
}

nlohmann::ordered_json transitionToJson(const TransitionEvidence& transition) {
    return {
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
    };
}

nlohmann::ordered_json framebufferToJson(
    const FramebufferEvidence& framebuffer) {
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
    return {
        {"id", framebuffer.id},
        {"pass", framebuffer.pass},
        {"framebufferId", framebuffer.framebufferId},
        {"width", framebuffer.width},
        {"height", framebuffer.height},
        {"attachments", std::move(attachments)},
        {"time", timeToJson(framebuffer.time)},
    };
}

nlohmann::ordered_json timingToJson(const PassTiming& timing) {
    return {
        {"pass", timing.pass},
        {"gpuValid", timing.valid},
        {"timestampPeriodNs", timing.timestampPeriodNs},
        {"startTicks", timing.startTicks},
        {"endTicks", timing.endTicks},
        {"elapsedNs", timing.elapsedNs},
        {"cpuStart", timeToJson(timing.cpuStart)},
        {"cpuEnd", timeToJson(timing.cpuEnd)},
    };
}

void setFramePaths(const std::string& outDir, const std::string& scene,
                   SceneFrameEvidence& frame) {
    const std::string dir = "frames/" + scene;
    makeDirs(pathJoin(outDir, dir));
    frame.gbuffer.ppmPath = dir + "/normal_depth.ppm";
    frame.objectId.ppmPath = dir + "/object_id.pgm";
    frame.metrics.ppmPath = dir + "/metrics.ppm";
    frame.boundaryMask.ppmPath = dir + "/boundary.ppm";
    frame.finalColor.ppmPath = dir + "/final.ppm";
    writePpm(pathJoin(outDir, frame.gbuffer.ppmPath), frame.gbufferImage);
    writePpm(pathJoin(outDir, frame.metrics.ppmPath), frame.metricsImage);
    writePpm(pathJoin(outDir, frame.boundaryMask.ppmPath),
             frame.boundaryImage);
    writePpm(pathJoin(outDir, frame.finalColor.ppmPath), frame.finalImage);
}

}  // namespace

bool writeJsonFile(const std::string& path,
                   const nlohmann::ordered_json& json) {
    return writeText(path, json.dump(2) + "\n");
}

bool writeSha256Sums(const std::string& directory, std::string* error) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& entry :
         std::filesystem::recursive_directory_iterator(directory, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() == "SHA256SUMS") continue;
        files.push_back(entry.path());
    }
    if (ec) {
        if (error) *error = "cannot enumerate output directory";
        return false;
    }
    std::sort(files.begin(), files.end());
    std::ostringstream out;
    for (const auto& path : files) {
        bool ok = false;
        const std::string hash = sha256File(path.string(), &ok);
        if (!ok) {
            if (error) *error = "cannot hash " + path.string();
            return false;
        }
        out << hash << "  "
            << std::filesystem::relative(path, directory).generic_string()
            << "\n";
    }
    return writeText(pathJoin(directory, "SHA256SUMS"), out.str());
}

RunResult runMaterial(const RunOptions& options) {
    RunResult result;
    result.materialPath = options.materialPath;
    result.outDir = options.outDir;
    if (result.outDir.empty()) {
        result.outDir = "artifacts/run-" + std::to_string(epochMillis());
    }
    if (!makeDirs(result.outDir)) {
        result.exitCode = 3;
        result.error = "cannot create output directory " + result.outDir;
        return result;
    }

    EvidenceChain chain;
    std::string error;
    if (!chain.open(pathJoin(result.outDir, "evidence.jsonl"), &error)) {
        result.exitCode = 3;
        result.error = error;
        return result;
    }
    const TimestampPoint runStart = nowTimestamp();
    chain.append("run_start",
                 {{"tool", "ssor_reconstruct"},
                  {"materialPath", options.materialPath},
                  {"outDir", result.outDir},
                  {"cwd", std::filesystem::current_path().string()},
                  {"start", timeToJson(runStart)}},
                 nullptr, &error);

    nlohmann::ordered_json original;
    const std::uint64_t inputSize = fileSize(options.materialPath);
    original["inputPath"] = options.materialPath;
    original["inputBytes"] = inputSize;
    original["inputSha256"] =
        inputSize <= ParseLimits{}.maxFileBytes
            ? sha256File(options.materialPath)
            : "";
    original["inputPrefixSha256"] = prefixSha256(options.materialPath, 64);
    original["prefixBytesRead"] = std::min<std::uint64_t>(inputSize, 64);
    original["copyCreated"] = false;
    original["bytesRead"] = 0;
    if (fileExists(options.materialPath)) {
        const std::string originalDir = pathJoin(result.outDir, "original");
        makeDirs(originalDir);
        const std::string copyPath =
            pathJoin(originalDir, pathBase(options.materialPath));
        if (inputSize <= ParseLimits{}.maxFileBytes) {
            if (inputSize == 0) {
                writeFile(copyPath, nullptr, 0);
                ::chmod(copyPath.c_str(), 0444);
            } else {
                copyFileReadOnly(options.materialPath, copyPath);
            }
            original["copyCreated"] = true;
            original["copyPath"] = relative(result.outDir, copyPath);
            original["copySha256"] = sha256File(copyPath);
            original["copyBytes"] = fileSize(copyPath);
            original["copyMode"] = modeString(copyPath);
            original["bytesRead"] = inputSize;
        }
        const std::string pristine =
            pathJoin("materials/original", pathBase(options.materialPath));
        if (fileExists(pristine)) {
            original["pristinePath"] = pristine;
            original["pristineSha256"] = sha256File(pristine);
            original["pristineMatch"] =
                inputSize <= ParseLimits{}.maxFileBytes &&
                sha256File(pristine) == sha256File(options.materialPath);
        }
        const std::string workspace = "materials/original/workspace.json";
        if (fileExists(workspace)) {
            const std::string dst = pathJoin(originalDir, "workspace.json");
            copyFileReadOnly(workspace, dst);
            original["workspace"] = {
                {"path", relative(result.outDir, dst)},
                {"bytes", fileSize(dst)},
                {"sha256", sha256File(dst)},
                {"mode", modeString(dst)},
            };
        }
        const std::string readme = "materials/original/README.seed.md";
        if (fileExists(readme)) {
            const std::string dst = pathJoin(originalDir, "README.seed.md");
            copyFileReadOnly(readme, dst);
            original["readme"] = {
                {"path", relative(result.outDir, dst)},
                {"bytes", fileSize(dst)},
                {"sha256", sha256File(dst)},
                {"mode", modeString(dst)},
            };
        }
    }
    chain.append("original_preserved", original, nullptr, &error);

    std::unique_ptr<Renderer> renderer;
    auto fail = [&](int exitCode, const std::string& code,
                    const std::string& message, std::uint64_t offset,
                    std::uint64_t observed, std::uint64_t limit,
                    std::uint64_t bytesRead) -> RunResult {
        const TimestampPoint end = nowTimestamp();
        nlohmann::ordered_json failure;
        failure["tool"] = "ssor_reconstruct";
        failure["status"] = "ERROR";
        failure["exitCode"] = exitCode;
        failure["start"] = timeToJson(runStart);
        failure["end"] = timeToJson(end);
        failure["input"] = original;
        failure["error"] = {
            {"code", code},
            {"message", message},
            {"rejectedAtOffset", offset},
            {"observed", observed},
            {"limit", limit},
            {"bytesRead", bytesRead},
            {"memoryAllocatedForInput", bytesRead},
        };
        if (renderer) {
            failure["environment"] =
                environmentToJson(environmentFromGpu(renderer->gpu()));
        }
        chain.append(exitCode == 6 ? "render_failed" : "material_rejected",
                     failure, nullptr, &error);
        chain.append("run_end",
                     {{"exitCode", exitCode},
                      {"end", timeToJson(end)},
                      {"status", "ERROR"}},
                     nullptr, &error);
        writeJsonFile(pathJoin(result.outDir, "failure.json"), failure);
        chain.writeHead(pathJoin(result.outDir, "chain_head.json"), &error);
        writeSha256Sums(result.outDir, &error);
        if (renderer) {
            renderer->shutdown();
            renderer.reset();
        }
        result.exitCode = exitCode;
        result.ok = false;
        result.error = message;
        result.report = failure;
        return result;
    };

    ParseLimits limits;
    ParseResult parsed = parseMaterialFile(options.materialPath, limits);
    if (!parsed.ok) {
        const std::uint64_t bytesRead =
            parsed.error == MaterialError::OversizedFile ? 0 : inputSize;
        return fail(exitCodeForMaterialError(parsed.error), parsed.code,
                    parsed.message, parsed.rejectedAtOffset, parsed.observed,
                    parsed.limit, bytesRead);
    }
    result.material =
        std::make_shared<RawMaterial>(std::move(parsed.material));
    nlohmann::ordered_json materialJson = materialToJson(*result.material);
    chain.append("material_parsed", materialJson, nullptr, &error);

    const std::vector<std::string> warnings =
        result.material->boundaryCount == 0
            ? std::vector<std::string>{"EMPTY_BOUNDARY_SET"}
            : std::vector<std::string>{};

    SceneBuild scenes = buildScenes(*result.material);
    renderer = std::make_unique<Renderer>();
    if (!renderer->init(*result.material, scenes, &error)) {
        return fail(6, "VULKAN_INIT_FAILED", error, 0, 0, 0, 0);
    }
    const EnvironmentInfo environment =
        environmentFromGpu(renderer->gpu());
    chain.append("boundary_seed",
                 {{"seedPixels", renderer->seedPixelCount()},
                  {"boundaryPixels", result.material->boundaryCount}},
                 nullptr, &error);
    chain.append("environment", environmentToJson(environment), nullptr,
                 &error);

    auto appendFrameEvents = [&](const SceneFrameEvidence& frame) {
        for (const DrawCallEvidence& draw : frame.draws) {
            chain.append("draw_call", drawToJson(draw, frame.scene), nullptr,
                         &error);
        }
        for (const TransitionEvidence& transition : frame.transitions) {
            chain.append("resource_transition", transitionToJson(transition),
                         nullptr, &error);
        }
        for (const FramebufferEvidence& framebuffer : frame.framebuffers) {
            chain.append("framebuffer", framebufferToJson(framebuffer), nullptr,
                         &error);
        }
        for (const PassTiming& timing : frame.passTimings) {
            chain.append("pass_timing", timingToJson(timing), nullptr, &error);
        }
        for (const ImageEvidence* image :
             {&frame.gbuffer, &frame.objectId, &frame.metrics,
              &frame.boundaryMask, &frame.boundarySeed, &frame.finalColor}) {
            chain.append("image_hash",
                         {{"scene", frame.scene},
                          {"name", image->name},
                          {"logicalId", image->logicalId},
                          {"format", image->format},
                          {"sha256", image->sha256},
                          {"bytes", image->bytes},
                          {"ppmPath", image->ppmPath}},
                         nullptr, &error);
        }
    };

    SceneFrameEvidence baseline;
    chain.append("scene_start",
                 {{"scene", "baseline"}, {"time", timeToJson(nowTimestamp())}},
                 nullptr, &error);
    if (!renderer->renderScene(scenes.baseline, baseline, &error)) {
        return fail(6, "RENDER_FAILED", error, 0, 0, 0, 0);
    }
    setFramePaths(result.outDir, "baseline", baseline);
    appendFrameEvents(baseline);
    chain.append("scene_end",
                 {{"scene", "baseline"},
                  {"end", timeToJson(baseline.end)},
                  {"drawCalls", baseline.draws.size()},
                  {"transitions", baseline.transitions.size()}},
                 nullptr, &error);

    SceneFrameEvidence repeat;
    chain.append("scene_start",
                 {{"scene", "repeat"}, {"time", timeToJson(nowTimestamp())}},
                 nullptr, &error);
    if (!renderer->renderScene(scenes.repeat, repeat, &error)) {
        return fail(6, "RENDER_FAILED", error, 0, 0, 0, 0);
    }
    setFramePaths(result.outDir, "repeat", repeat);
    appendFrameEvents(repeat);
    chain.append("scene_end",
                 {{"scene", "repeat"},
                  {"end", timeToJson(repeat.end)},
                  {"drawCalls", repeat.draws.size()},
                  {"transitions", repeat.transitions.size()}},
                 nullptr, &error);

    BoundaryReport boundary = evaluateBoundary(*result.material, baseline);
    CorrelationResult correlation =
        correlateScenes(*result.material, baseline, repeat);
    const std::string diffPpm = "diff/final_diff.ppm";
    const std::string boundaryPpm = "boundary/overlay.ppm";
    const std::string diffOverlayPpm = "diff/overlay.ppm";
    writePpm(pathJoin(result.outDir, diffPpm), correlation.diff.diffImage);
    writePpm(pathJoin(result.outDir, boundaryPpm), boundary.overlay);
    writePpm(pathJoin(result.outDir, diffOverlayPpm), correlation.diff.diffImage);

    const TimestampPoint finish = nowTimestamp();
    nlohmann::ordered_json runJson;
    runJson["tool"] = "ssor_reconstruct";
    runJson["formatVersion"] = 1;
    runJson["status"] = "OK";
    runJson["start"] = timeToJson(runStart);
    runJson["end"] = timeToJson(finish);
    runJson["material"] = materialJson;
    runJson["originalMaterial"] = original;
    runJson["environment"] = environmentToJson(environment);
    runJson["warnings"] = warnings;
    runJson["scenes"] = {frameToJson(baseline), frameToJson(repeat)};
    runJson["boundary"] = boundaryToJson(boundary);
    runJson["correlation"] = correlationToJson(correlation);
    runJson["diff"] = diffToJson(correlation.diff);
    runJson["outputs"] = {
        {"baselineFinal", baseline.finalColor.ppmPath},
        {"baselineMetrics", baseline.metrics.ppmPath},
        {"baselineBoundary", baseline.boundaryMask.ppmPath},
        {"repeatFinal", repeat.finalColor.ppmPath},
        {"repeatMetrics", repeat.metrics.ppmPath},
        {"repeatBoundary", repeat.boundaryMask.ppmPath},
        {"diff", diffPpm},
        {"diffOverlay", diffOverlayPpm},
        {"boundaryOverlay", boundaryPpm},
    };
    runJson["rawOffsets"] = materialJson["rawOffsets"];
    runJson["duplicateSourceOffset"] = {
        {"objectIndex", result.material->duplicateObjectIndex()},
        {"originalOffsetOffset",
         result.material->duplicateObjectIndex() >= 0
             ? result.material
                   ->objects[result.material->duplicateObjectIndex()]
                   .originalOffsetOffset
             : 0},
    };

    writeJsonFile(pathJoin(result.outDir, "run.json"), runJson);
    writeJsonFile(pathJoin(result.outDir, "boundary_report.json"),
                  boundaryToJson(boundary));
    writeJsonFile(pathJoin(result.outDir, "correlation.json"),
                  correlationToJson(correlation));
    nlohmann::ordered_json draws = nlohmann::ordered_json::array();
    for (const DrawCallEvidence& draw : baseline.draws) {
        draws.push_back(drawToJson(draw, "baseline"));
    }
    for (const DrawCallEvidence& draw : repeat.draws) {
        draws.push_back(drawToJson(draw, "repeat"));
    }
    writeJsonFile(pathJoin(result.outDir, "draw_calls.json"), draws);
    nlohmann::ordered_json resources;
    resources["baseline"] = frameToJson(baseline)["resourceTransitions"];
    resources["repeat"] = frameToJson(repeat)["resourceTransitions"];
    writeJsonFile(pathJoin(result.outDir, "resource_states.json"), resources);
    nlohmann::ordered_json framebuffers;
    framebuffers["baseline"] = frameToJson(baseline)["framebuffers"];
    framebuffers["repeat"] = frameToJson(repeat)["framebuffers"];
    writeJsonFile(pathJoin(result.outDir, "framebuffers.json"), framebuffers);

    chain.append("image_diff",
                 {{"diff", diffToJson(correlation.diff)},
                  {"ppmPath", diffPpm},
                  {"overlayPath", diffOverlayPpm}},
                 nullptr, &error);
    chain.append("boundary_probes",
                 {{"report", boundaryToJson(boundary)},
                  {"overlayPath", boundaryPpm}},
                 nullptr, &error);
    chain.append("correlation", correlationToJson(correlation), nullptr,
                 &error);
    chain.append("run_end",
                 {{"exitCode", 0},
                  {"end", timeToJson(finish)},
                  {"status", "OK"},
                  {"warnings", warnings}},
                 nullptr, &error);
    chain.writeHead(pathJoin(result.outDir, "chain_head.json"), &error);
    writeSha256Sums(result.outDir, &error);

    renderer->shutdown();
    renderer.reset();
    result.exitCode = 0;
    result.ok = true;
    result.report = runJson;
    result.error.clear();
    return result;
}

}  // namespace ssor
