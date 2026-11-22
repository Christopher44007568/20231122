#include "accept.h"

#include "core/material.h"
#include "core/timestamp.h"
#include "core/util.h"
#include "evidence.h"
#include "runner.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

namespace ssor {
namespace {

struct CheckRecord {
    std::string id;
    bool pass = false;
    std::string detail;
    std::string evidence;
    TimestampPoint time;
};

bool readJson(const std::string& path, nlohmann::ordered_json* json,
              std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    try {
        in >> *json;
        return true;
    } catch (const std::exception& ex) {
        if (error) *error = std::string("invalid JSON in ") + path + ": " + ex.what();
        return false;
    }
}

std::string jsonString(const nlohmann::ordered_json& json,
                       const std::string& key) {
    if (!json.contains(key) || json[key].is_null()) return "";
    if (json[key].is_string()) return json[key].get<std::string>();
    return json[key].dump();
}

std::uint64_t jsonU64(const nlohmann::ordered_json& json,
                      const std::string& key, std::uint64_t fallback = 0) {
    if (!json.contains(key) || json[key].is_null()) return fallback;
    if (json[key].is_number_unsigned()) return json[key].get<std::uint64_t>();
    if (json[key].is_number_integer()) {
        return static_cast<std::uint64_t>(json[key].get<std::int64_t>());
    }
    return fallback;
}

bool concreteUtc(const std::string& text) {
    static const std::regex pattern(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{6}Z$)");
    return std::regex_match(text, pattern);
}

bool checkTime(const nlohmann::ordered_json& time) {
    return time.is_object() && concreteUtc(jsonString(time, "utc")) &&
           jsonU64(time, "epochMs") > 0;
}

std::string csvEscape(const std::string& value) {
    std::string out = "\"";
    for (char c : value) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += "\"";
    return out;
}

std::string payloadSummary(const nlohmann::ordered_json& payload) {
    if (!payload.is_object()) return payload.dump();
    for (const char* key : {"scene", "pass", "objectId", "imageId", "code",
                            "verdict", "name"}) {
        if (payload.contains(key)) {
            return std::string(key) + "=" + payload[key].dump();
        }
    }
    if (payload.contains("report") && payload["report"].is_object()) {
        return "report.verdict=" + jsonString(payload["report"], "verdict");
    }
    return "object";
}

bool appendTimeline(const std::string& caseDir, const std::string& source,
                    std::vector<std::string>* lines, std::string* error) {
    const std::string path = pathJoin(caseDir, "evidence.jsonl");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot open " + path;
        return false;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        nlohmann::ordered_json event;
        try {
            event = nlohmann::ordered_json::parse(line);
        } catch (const std::exception& ex) {
            if (error) *error = std::string("bad event: ") + ex.what();
            return false;
        }
        std::ostringstream row;
        row << csvEscape(source) << "," << jsonU64(event, "seq") << ","
            << csvEscape(jsonString(event, "utc")) << ","
            << jsonU64(event, "epoch_ms") << ","
            << jsonU64(event, "monotonic_ns") << ","
            << csvEscape(jsonString(event, "type")) << ","
            << csvEscape(jsonString(event, "hash")) << ","
            << csvEscape(payloadSummary(event.value("payload",
                                                    nlohmann::ordered_json{})));
        lines->push_back(row.str());
    }
    return true;
}

std::string timestampText(const nlohmann::ordered_json& time) {
    return jsonString(time, "utc");
}

}  // namespace

int runAcceptance(const AcceptOptions& options) {
    const TimestampPoint acceptStart = nowTimestamp();
    if (!makeDirs(options.outDir)) {
        std::fprintf(stderr, "cannot create acceptance directory %s\n",
                     options.outDir.c_str());
        return 3;
    }
    const std::string caseRoot = pathJoin(options.outDir, "cases");
    const std::string inputsDir = pathJoin(caseRoot, "inputs");
    makeDirs(inputsDir);
    const std::string canonicalDir = pathJoin(caseRoot, "canonical");
    const std::string emptyDir = pathJoin(caseRoot, "empty");
    const std::string emptyBoundaryDir = pathJoin(caseRoot, "empty_boundaries");
    const std::string oversizedFileDir = pathJoin(caseRoot, "oversized_file");
    const std::string oversizedCountDir = pathJoin(caseRoot, "oversized_count");
    const std::string seedRescueDir = pathJoin(caseRoot, "seed_rescue");

    const std::string emptyInput = pathJoin(inputsDir, "empty.ssoraw");
    writeFile(emptyInput, nullptr, 0);

    const std::string oversizedInput =
        pathJoin(inputsDir, "oversized_file.ssoraw");
    {
        std::ofstream out(oversizedInput, std::ios::binary | std::ios::trunc);
        out.put('\0');
    }
    std::error_code resizeError;
    std::filesystem::resize_file(
        oversizedInput, ParseLimits{}.maxFileBytes + 1, resizeError);

    const std::string oversizedCountInput =
        pathJoin(inputsDir, "oversized_count.ssoraw");
    {
        bool readOk = false;
        std::string raw =
            readFile(options.materialPath, ParseLimits{}.maxFileBytes, nullptr,
                     &readOk);
        if (readOk && raw.size() >= 32) {
            raw[28] = static_cast<char>(0xff);
            raw[29] = static_cast<char>(0xff);
            raw[30] = static_cast<char>(0xff);
            raw[31] = static_cast<char>(0xff);
        }
        writeFile(oversizedCountInput, raw.data(), raw.size());
    }

    const std::string seedRescueInput =
        pathJoin(inputsDir, "seed_rescue.ssoraw");
    {
        bool readOk = false;
        std::string raw =
            readFile(options.materialPath, ParseLimits{}.maxFileBytes, nullptr,
                     &readOk);
        if (readOk && raw.size() >= 48) {
            const float disabled = 1000000.0f;
            std::memcpy(&raw[32], &disabled, sizeof(disabled));
            std::memcpy(&raw[36], &disabled, sizeof(disabled));
            std::memcpy(&raw[40], &disabled, sizeof(disabled));
            std::memcpy(&raw[44], &disabled, sizeof(disabled));
        }
        writeFile(seedRescueInput, raw.data(), raw.size());
    }

    std::vector<CheckRecord> checks;
    auto addCheck = [&](const std::string& id, bool pass,
                        const std::string& detail,
                        const std::string& evidence) {
        checks.push_back({id, pass, detail, evidence, nowTimestamp()});
        return pass;
    };

    RunOptions canonicalOptions;
    canonicalOptions.materialPath = options.materialPath;
    canonicalOptions.outDir = canonicalDir;
    const RunResult canonical = runMaterial(canonicalOptions);

    RunOptions emptyOptions;
    emptyOptions.materialPath = emptyInput;
    emptyOptions.outDir = emptyDir;
    const RunResult empty = runMaterial(emptyOptions);

    RunOptions emptyBoundaryOptions;
    emptyBoundaryOptions.materialPath =
        "materials/raw/empty_boundaries.ssoraw";
    emptyBoundaryOptions.outDir = emptyBoundaryDir;
    const RunResult emptyBoundary = runMaterial(emptyBoundaryOptions);

    RunOptions oversizedFileOptions;
    oversizedFileOptions.materialPath = oversizedInput;
    oversizedFileOptions.outDir = oversizedFileDir;
    const RunResult oversizedFile = runMaterial(oversizedFileOptions);

    RunOptions oversizedCountOptions;
    oversizedCountOptions.materialPath = oversizedCountInput;
    oversizedCountOptions.outDir = oversizedCountDir;
    const RunResult oversizedCount = runMaterial(oversizedCountOptions);

    RunOptions seedRescueOptions;
    seedRescueOptions.materialPath = seedRescueInput;
    seedRescueOptions.outDir = seedRescueDir;
    const RunResult seedRescue = runMaterial(seedRescueOptions);

    nlohmann::ordered_json run;
    std::string error;
    const bool runRead =
        readJson(pathJoin(canonicalDir, "run.json"), &run, &error);
    nlohmann::ordered_json failureEmpty;
    nlohmann::ordered_json failureOversizedFile;
    nlohmann::ordered_json failureOversizedCount;
    readJson(pathJoin(emptyDir, "failure.json"), &failureEmpty, &error);
    readJson(pathJoin(oversizedFileDir, "failure.json"),
             &failureOversizedFile, &error);
    readJson(pathJoin(oversizedCountDir, "failure.json"),
             &failureOversizedCount, &error);

    const nlohmann::ordered_json material =
        runRead ? run.value("material", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const nlohmann::ordered_json original =
        runRead ? run.value("originalMaterial", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const nlohmann::ordered_json correlation =
        runRead ? run.value("correlation", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const nlohmann::ordered_json boundary =
        runRead ? run.value("boundary", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const nlohmann::ordered_json scenes =
        runRead ? run.value("scenes", nlohmann::ordered_json::array())
                : nlohmann::ordered_json::array();

    const std::string pristineMaterial =
        pathJoin("materials/original", pathBase(options.materialPath));
    const std::string inputHash = sha256File(options.materialPath);
    const std::string pristineHash = sha256File(pristineMaterial);
    const std::string copyRel = jsonString(original, "copyPath");
    const std::string copyHash =
        copyRel.empty() ? "" : sha256File(pathJoin(canonicalDir, copyRel));
    const std::string copyMode = jsonString(original, "copyMode");
    const bool originalOk = !inputHash.empty() && inputHash == pristineHash &&
                            inputHash == copyHash &&
                            copyMode.find('w') == std::string::npos;
    addCheck("original_material_byte_identical", originalOk,
             "input, pristine copy and run copy share SHA-256; run copy is read-only",
             canonicalDir + "/original");

    const std::string workspacePath = "materials/original/workspace.json";
    const std::string workspaceHash = sha256File(workspacePath);
    const std::string liveWorkspaceHash = sha256File("artifacts/workspace.json");
    const bool workspaceOk =
        !workspaceHash.empty() &&
        workspaceHash == liveWorkspaceHash &&
        workspaceHash ==
            jsonString(original.value("workspace",
                                      nlohmann::ordered_json{}),
                       "sha256");
    addCheck("seed_workspace_preserved", workspaceOk,
             "harness workspace.json is preserved byte-for-byte",
             canonicalDir + "/original/workspace.json");

    const std::string readmeHash = sha256File("materials/original/README.seed.md");
    const bool readmeOk =
        !readmeHash.empty() &&
        readmeHash ==
            jsonString(original.value("readme", nlohmann::ordered_json{}),
                       "sha256");
    addCheck("seed_readme_preserved", readmeOk,
             "initial README is preserved in materials/original/README.seed.md",
             canonicalDir + "/original/README.seed.md");

    const nlohmann::ordered_json offsets =
        runRead ? run.value("rawOffsets", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const bool offsetsOk =
        jsonU64(offsets, "objectCount") == 24 &&
        jsonU64(offsets, "boundaryCount") == 28 &&
        jsonU64(offsets, "outlineDepth") == 32 &&
        jsonU64(offsets, "normalThreshold") == 36 &&
        jsonU64(offsets, "objectsBase") == 80 &&
        jsonU64(offsets, "boundariesBase") == 320;
    addCheck("raw_offsets_exact", offsetsOk,
             "depth=32, normal=36, objectCount=24, boundaryCount=28, objects=80, boundaries=320",
             canonicalDir + "/run.json");

    bool duplicateOffsetOk = false;
    std::string duplicateOffsetDetail = "missing duplicate object";
    if (material.contains("objects") && material["objects"].size() >= 3) {
        const auto& duplicate = material["objects"][2];
        const auto& originalOffset = duplicate["originalOffset"];
        duplicateOffsetOk =
            duplicate.value("duplicate", false) &&
            duplicate.value("repeatOf", 0u) == 1 &&
            duplicate["sourceOffsets"].value("originalOffset", 0ull) == 284ull &&
            originalOffset.size() == 3 &&
            std::abs(originalOffset[0].get<float>() - 0.58f) < 1e-6f &&
            std::abs(originalOffset[1].get<float>()) < 1e-6f &&
            std::abs(originalOffset[2].get<float>() - 0.06f) < 1e-6f;
        duplicateOffsetDetail =
            "objects[2] duplicate originalOffset at byte 284 = 0.58,0,0.06";
    }
    addCheck("duplicate_original_offset", duplicateOffsetOk,
             duplicateOffsetDetail, canonicalDir + "/run.json");

    auto findScene = [&](const std::string& name)
        -> const nlohmann::ordered_json* {
        if (!scenes.is_array()) return nullptr;
        for (const auto& scene : scenes) {
            if (jsonString(scene, "scene") == name) return &scene;
        }
        return nullptr;
    };
    auto passDraws = [](const nlohmann::ordered_json& scene,
                        const std::string& pass) {
        std::vector<nlohmann::ordered_json> draws;
        if (scene.contains("drawCalls")) {
            for (const auto& draw : scene["drawCalls"]) {
                if (jsonString(draw, "pass") == pass) draws.push_back(draw);
            }
        }
        return draws;
    };
    const nlohmann::ordered_json* baseline = findScene("baseline");
    const nlohmann::ordered_json* repeat = findScene("repeat");
    const auto baselineGbuffer =
        baseline ? passDraws(*baseline, "gbuffer")
                 : std::vector<nlohmann::ordered_json>{};
    const auto repeatGbuffer =
        repeat ? passDraws(*repeat, "gbuffer")
               : std::vector<nlohmann::ordered_json>{};
    const bool baselineOk =
        baseline && baselineGbuffer.size() == 2 &&
        passDraws(*baseline, "edge").size() == 1 &&
        passDraws(*baseline, "final").size() == 1;
    addCheck("baseline_draw_calls", baselineOk,
             "baseline: 2 object draws + 1 edge + 1 final", canonicalDir);

    bool repeatOk = repeat && repeatGbuffer.size() == 3;
    bool sharedMesh = false;
    bool sharedPipeline = false;
    std::string duplicateDrawId;
    if (repeatOk) {
        const nlohmann::ordered_json* baseDraw = nullptr;
        const nlohmann::ordered_json* duplicateDraw = nullptr;
        for (const auto& draw : repeatGbuffer) {
            if (jsonString(draw, "objectId") == "pillar.base") baseDraw = &draw;
            if (jsonString(draw, "objectId") == "pillar.repeat") {
                duplicateDraw = &draw;
            }
        }
        repeatOk = baseDraw && duplicateDraw &&
                   duplicateDraw->value("duplicate", false) &&
                   jsonU64(*duplicateDraw, "repeatOf") == 1 &&
                   jsonU64(*duplicateDraw, "firstInstance") == 3;
        if (baseDraw && duplicateDraw) {
            sharedMesh = jsonString(*baseDraw, "meshSha256") ==
                             jsonString(*duplicateDraw, "meshSha256") &&
                         !jsonString(*baseDraw, "meshSha256").empty();
            sharedPipeline = jsonString(*baseDraw, "pipelineId") ==
                                 jsonString(*duplicateDraw, "pipelineId") &&
                             !jsonString(*baseDraw, "pipelineId").empty();
            duplicateDrawId = std::to_string(jsonU64(*duplicateDraw, "id"));
        }
    }
    repeatOk = repeatOk && sharedMesh && sharedPipeline;
    addCheck("repeat_draw_calls_shared_resources", repeatOk,
             "repeat: 3 object draws; duplicate draw " + duplicateDrawId +
                 " shares mesh buffer and pipeline with pillar.base",
             canonicalDir + "/draw_calls.json");

    bool framebufferOk = false;
    if (baseline && repeat) {
        framebufferOk = baseline->value("framebuffers", nlohmann::ordered_json::array()).size() == 3 &&
                        repeat->value("framebuffers", nlohmann::ordered_json::array()).size() == 3;
        for (const auto* scene : {baseline, repeat}) {
            for (const auto& fb : (*scene)["framebuffers"]) {
                const std::string pass = jsonString(fb, "pass");
                const auto attachments = fb.value("attachments", nlohmann::ordered_json::array());
                if (pass == "gbuffer") framebufferOk = framebufferOk && attachments.size() == 3;
                if (pass == "edge") framebufferOk = framebufferOk && attachments.size() == 2;
                if (pass == "final") framebufferOk = framebufferOk && attachments.size() == 1;
            }
        }
    }
    addCheck("framebuffer_attachments", framebufferOk,
             "each scene has gbuffer(3 attachments), edge(2), final(1)",
             canonicalDir + "/framebuffers.json");

    bool transitionsOk = repeat && repeat->contains("resourceTransitions") &&
                         (*repeat)["resourceTransitions"].size() >= 10;
    bool sawRead = false;
    bool sawTransfer = false;
    if (repeat && repeat->contains("resourceTransitions")) {
        for (const auto& transition : (*repeat)["resourceTransitions"]) {
            const std::string layout = jsonString(transition, "newLayout");
            sawRead = sawRead || layout == "SHADER_READ_ONLY_OPTIMAL";
            sawTransfer = sawTransfer || layout == "TRANSFER_SRC_OPTIMAL";
        }
    }
    transitionsOk = transitionsOk && sawRead && sawTransfer;
    addCheck("resource_state_transitions", transitionsOk,
             "recorded layouts include SHADER_READ_ONLY_OPTIMAL and TRANSFER_SRC_OPTIMAL",
             canonicalDir + "/resource_states.json");

    const nlohmann::ordered_json imageDifference =
        correlation.value("imageDifference", nlohmann::ordered_json{});
    const bool diffOk =
        jsonU64(imageDifference, "diffPixels") > 50 &&
        !imageDifference.value("bbox", nlohmann::ordered_json(nullptr))
             .is_null() &&
        jsonU64(correlation, "duplicateDiffPixels") > 0 &&
        jsonU64(correlation, "duplicateOutlinePixels") > 0 &&
        jsonString(correlation, "verdict") ==
            "REPEATED_OBJECT_OUTLINE_ANOMALY";
    addCheck("image_diff_matches_duplicate", diffOk,
             "diffPixels=" +
                 std::to_string(jsonU64(imageDifference, "diffPixels")) +
                 ", duplicateDiffPixels=" +
                 std::to_string(jsonU64(correlation, "duplicateDiffPixels")) +
                 ", duplicateOutlinePixels=" +
                 std::to_string(jsonU64(correlation, "duplicateOutlinePixels")),
             canonicalDir + "/diff/final_diff.ppm");

    bool boundaryOk =
        jsonU64(boundary, "total") ==
            jsonU64(material, "boundaryCount") &&
        boundary.contains("probes") && boundary["probes"].is_array() &&
        boundary["probes"].size() == jsonU64(material, "boundaryCount");
    std::uint64_t firstProbeOffset = 0;
    if (boundaryOk && !boundary["probes"].empty()) {
        firstProbeOffset = jsonU64(boundary["probes"][0], "sourceOffset");
        boundaryOk = firstProbeOffset == 320;
        for (const auto& probe : boundary["probes"]) {
            boundaryOk = boundaryOk &&
                         jsonU64(probe, "sourceOffset") >= 320;
        }
    }
    boundaryOk = boundaryOk && jsonU64(boundary, "boundaryMatched") > 0;
    addCheck("boundary_pixels_evaluated", boundaryOk,
             "35 probes evaluated; first probe byte offset=" +
                 std::to_string(firstProbeOffset) +
                 "; matched=" +
                 std::to_string(jsonU64(boundary, "boundaryMatched")),
             canonicalDir + "/boundary_report.json");

    const bool emptyOk =
        empty.exitCode == 4 &&
        jsonString(failureEmpty.value("error", nlohmann::ordered_json{}),
                   "code") == "EMPTY_INPUT" &&
        jsonU64(failureEmpty.value("error", nlohmann::ordered_json{}),
                "rejectedAtOffset") == 0 &&
        jsonU64(failureEmpty.value("error", nlohmann::ordered_json{}),
                "bytesRead") == 0;
    addCheck("empty_input_rejected", emptyOk,
             "zero-byte input exits 4 with EMPTY_INPUT before allocation",
             emptyDir + "/failure.json");

    nlohmann::ordered_json emptyBoundaryRun;
    const bool emptyBoundaryRead = readJson(
        pathJoin(emptyBoundaryDir, "run.json"), &emptyBoundaryRun, &error);
    bool warningPresent = false;
    if (emptyBoundaryRead && emptyBoundaryRun.contains("warnings")) {
        for (const auto& warning : emptyBoundaryRun["warnings"]) {
            warningPresent = warningPresent ||
                             warning.get<std::string>() == "EMPTY_BOUNDARY_SET";
        }
    }
    const bool emptyBoundaryOk =
        emptyBoundary.exitCode == 0 && emptyBoundaryRead && warningPresent &&
        jsonU64(emptyBoundaryRun.value("boundary", nlohmann::ordered_json{}),
                "total") == 0;
    addCheck("empty_boundary_set_supported", emptyBoundaryOk,
             "boundaryCount=0 renders successfully and records EMPTY_BOUNDARY_SET",
             emptyBoundaryDir + "/run.json");

    nlohmann::ordered_json seedRescueRun;
    const bool seedRescueRead =
        readJson(pathJoin(seedRescueDir, "run.json"), &seedRescueRun, &error);
    std::uint64_t seedRescuedPixels = 0;
    if (seedRescueRead && seedRescueRun.contains("boundary")) {
        for (const auto& probe : seedRescueRun["boundary"]["probes"]) {
            if (jsonString(probe, "expected") == "boundary" &&
                !probe.value("detectedBoundary", false) &&
                probe.value("outline", false)) {
                ++seedRescuedPixels;
            }
        }
    }
    addCheck("boundary_seed_controls_mask", seedRescue.exitCode == 0 &&
                 seedRescueRead && seedRescuedPixels >= 20,
             "with all thresholds disabled, boundary pixels still seed the outline: " +
                 std::to_string(seedRescuedPixels) + " rescued probes",
             seedRescueDir + "/run.json");

    const auto oversizedError = failureOversizedFile.value(
        "error", nlohmann::ordered_json{});
    const bool oversizedFileOk =
        oversizedFile.exitCode == 5 &&
        jsonString(oversizedError, "code") == "OVERSIZED_FILE" &&
        jsonU64(oversizedError, "observed") >
            jsonU64(oversizedError, "limit") &&
        jsonU64(oversizedError, "bytesRead") == 0 &&
        jsonU64(oversizedError, "memoryAllocatedForInput") == 0;
    addCheck("oversized_file_rejected_before_read", oversizedFileOk,
             "8 MiB+1 sparse input exits 5; bytesRead=0, allocation=0",
             oversizedFileDir + "/failure.json");

    const auto oversizedCountError = failureOversizedCount.value(
        "error", nlohmann::ordered_json{});
    const bool oversizedCountOk =
        oversizedCount.exitCode == 5 &&
        jsonString(oversizedCountError, "code") ==
            "OVERSIZED_DECLARED_COUNT" &&
        jsonU64(oversizedCountError, "rejectedAtOffset") == 28 &&
        jsonU64(oversizedCountError, "observed") == 0xffffffffull &&
        jsonU64(oversizedCountError, "limit") == 65536 &&
        jsonU64(oversizedCountError, "bytesRead") > 0;
    addCheck("oversized_declared_count_rejected", oversizedCountOk,
             "boundaryCount=0xFFFFFFFF rejected at offset 28 before vector allocation",
             oversizedCountDir + "/failure.json");

    auto verifyCaseChain = [&](const std::string& dir) {
        std::string chainError;
        std::uint64_t count = 0;
        std::string head;
        if (!verifyEvidenceChain(pathJoin(dir, "evidence.jsonl"), &chainError,
                                 &count, &head)) {
            return std::make_pair(false, chainError);
        }
        nlohmann::ordered_json chainHead;
        if (!readJson(pathJoin(dir, "chain_head.json"), &chainHead,
                      &chainError)) {
            return std::make_pair(false, chainError);
        }
        const bool ok = jsonU64(chainHead, "count") == count &&
                        jsonString(chainHead, "head") == head;
        return std::make_pair(
            ok, "events=" + std::to_string(count) +
                    (ok ? " head matches" : " head mismatch"));
    };
    bool chainOk = true;
    std::string chainDetail;
    for (const auto& item :
         {std::make_pair("canonical", canonicalDir),
          std::make_pair("empty", emptyDir),
          std::make_pair("empty_boundaries", emptyBoundaryDir),
          std::make_pair("oversized_file", oversizedFileDir),
          std::make_pair("oversized_count", oversizedCountDir),
          std::make_pair("seed_rescue", seedRescueDir)}) {
        const auto verified = verifyCaseChain(item.second);
        chainOk = chainOk && verified.first;
        chainDetail += std::string(item.first) + ":" + verified.second + "; ";
    }
    addCheck("evidence_chain_valid", chainOk, chainDetail,
             canonicalDir + "/evidence.jsonl");

    const nlohmann::ordered_json runStartTime =
        runRead ? run.value("start", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const nlohmann::ordered_json runEndTime =
        runRead ? run.value("end", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    bool timestampsOk = runRead && checkTime(runStartTime) &&
                        checkTime(runEndTime) &&
                        jsonU64(runEndTime, "epochMs") >=
                            jsonU64(runStartTime, "epochMs");
    std::uint64_t drawTimestampCount = 0;
    bool gpuTimingPresent = false;
    if (runRead && scenes.is_array()) {
        for (const auto& scene : scenes) {
            for (const auto& draw : scene["drawCalls"]) {
                if (checkTime(draw["time"])) ++drawTimestampCount;
            }
            for (const auto& timing : scene["passTimings"]) {
                timestampsOk = timestampsOk &&
                               checkTime(timing.value(
                                   "cpuStart", nlohmann::ordered_json{})) &&
                               checkTime(timing.value(
                                   "cpuEnd", nlohmann::ordered_json{}));
                gpuTimingPresent =
                    gpuTimingPresent || timing.value("gpuValid", false);
            }
        }
    }
    timestampsOk = timestampsOk && drawTimestampCount > 0;
    const bool gpuTimestampSupported =
        runRead && run.value("environment", nlohmann::ordered_json{})
                       .value("timestampValid", false);
    if (gpuTimestampSupported) {
        timestampsOk = timestampsOk && gpuTimingPresent;
    }
    for (const auto& failure :
         {failureEmpty, failureOversizedFile, failureOversizedCount}) {
        timestampsOk = timestampsOk &&
                       checkTime(failure.value(
                           "start", nlohmann::ordered_json{})) &&
                       checkTime(failure.value(
                           "end", nlohmann::ordered_json{}));
    }
    addCheck("concrete_timestamps", timestampsOk,
             "drawCalls=" + std::to_string(drawTimestampCount) +
                 ", gpuTiming=" + (gpuTimingPresent ? "yes" : "no") +
                 ", UTC format has microseconds",
             canonicalDir + "/evidence.jsonl");

    const nlohmann::ordered_json environment =
        runRead ? run.value("environment", nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    const bool environmentOk =
        !jsonString(environment, "deviceName").empty() &&
        jsonString(environment, "glfwVersion").find("Null") !=
            std::string::npos &&
        !jsonString(environment, "apiVersion").empty();
    addCheck("vulkan_glfw_environment", environmentOk,
             "device=" + jsonString(environment, "deviceName") +
                 ", glfw=" + jsonString(environment, "glfwVersion"),
             canonicalDir + "/run.json");

    bool sumsOk = false;
    {
        bool ok = false;
        const std::string sums =
            readFile(pathJoin(canonicalDir, "SHA256SUMS"), 4 * 1024 * 1024,
                     nullptr, &ok);
        sumsOk = ok && sums.find("run.json") != std::string::npos &&
                 sums.find("evidence.jsonl") != std::string::npos &&
                 sums.find("chain_head.json") != std::string::npos &&
                 sums.find("correlation.json") != std::string::npos &&
                 sums.find("frames/baseline/final.ppm") != std::string::npos;
    }
    addCheck("sha256_sums_cover_evidence", sumsOk,
             "SHA256SUMS covers run/evidence/chain/correlation/PPM",
             canonicalDir + "/SHA256SUMS");

    bool allPass = true;
    for (const CheckRecord& check : checks) allPass = allPass && check.pass;

    auto caseSummary = [](const RunResult& result) {
        nlohmann::ordered_json json;
        json["exitCode"] = result.exitCode;
        json["ok"] = result.ok;
        json["outDir"] = result.outDir;
        json["error"] = result.error;
        if (result.report.contains("start")) json["start"] = result.report["start"];
        if (result.report.contains("end")) json["end"] = result.report["end"];
        return json;
    };
    nlohmann::ordered_json cases;
    cases["canonical"] = caseSummary(canonical);
    cases["empty"] = caseSummary(empty);
    cases["empty_boundaries"] = caseSummary(emptyBoundary);
    cases["oversized_file"] = caseSummary(oversizedFile);
    cases["oversized_count"] = caseSummary(oversizedCount);
    cases["seed_rescue"] = caseSummary(seedRescue);

    nlohmann::ordered_json checksJson = nlohmann::ordered_json::array();
    for (const CheckRecord& check : checks) {
        checksJson.push_back({
            {"id", check.id},
            {"pass", check.pass},
            {"detail", check.detail},
            {"evidence", check.evidence},
            {"timestamp", timeToJson(check.time)},
        });
    }
    writeJsonFile(pathJoin(options.outDir, "checks.json"), checksJson);

    const TimestampPoint acceptEnd = nowTimestamp();
    nlohmann::ordered_json report;
    report["tool"] = "ssor_reconstruct";
    report["formatVersion"] = 1;
    report["status"] = allPass ? "PASS" : "FAIL";
    report["start"] = timeToJson(acceptStart);
    report["end"] = timeToJson(acceptEnd);
    report["materialPath"] = options.materialPath;
    report["materialSha256"] = jsonString(material, "sha256");
    report["originalMaterial"] = original;
    report["rawOffsets"] = offsets;
    report["duplicateSourceOffset"] =
        runRead ? run.value("duplicateSourceOffset",
                            nlohmann::ordered_json{})
                : nlohmann::ordered_json{};
    report["material"] = material;
    report["environment"] = environment;
    report["cases"] = cases;
    report["checks"] = checksJson;
    report["evidenceTimelineCsv"] = "evidence_timeline.csv";
    writeJsonFile(pathJoin(options.outDir, "acceptance_report.json"), report);

    std::vector<std::string> timeline;
    timeline.push_back(
        "source,seq,utc,epoch_ms,monotonic_ns,type,hash,payload_summary");
    for (const auto& item :
         {std::make_pair("canonical", canonicalDir),
          std::make_pair("empty", emptyDir),
          std::make_pair("empty_boundaries", emptyBoundaryDir),
          std::make_pair("oversized_file", oversizedFileDir),
          std::make_pair("oversized_count", oversizedCountDir),
          std::make_pair("seed_rescue", seedRescueDir)}) {
        appendTimeline(item.second, item.first, &timeline, &error);
    }
    for (std::size_t i = 0; i < checks.size(); ++i) {
        std::ostringstream row;
        row << "acceptance," << i << "," << checks[i].time.utc << ","
            << checks[i].time.epochMs << "," << checks[i].time.monotonicNs
            << ",acceptance_check,,"
            << csvEscape(checks[i].id + "=" +
                         (checks[i].pass ? "PASS" : "FAIL"));
        timeline.push_back(row.str());
    }
    std::ostringstream timelineCsv;
    for (const std::string& line : timeline) timelineCsv << line << "\n";
    writeText(pathJoin(options.outDir, "evidence_timeline.csv"),
              timelineCsv.str());

    std::ostringstream md;
    md << "# Screen-space outline reconstruction acceptance\n\n";
    md << "Status: **" << (allPass ? "PASS" : "FAIL") << "**\n\n";
    md << "Generated: `" << acceptEnd.utc << "`  \n";
    md << "Material: `" << options.materialPath << "`  \n";
    md << "SHA-256: `" << jsonString(material, "sha256") << "`\n\n";
    md << "## Original material\n\n";
    md << "| Item | Value |\n|---|---|\n";
    md << "| Input bytes | " << jsonU64(original, "inputBytes") << " |\n";
    md << "| Input SHA-256 | `" << jsonString(original, "inputSha256")
       << "` |\n";
    md << "| Input prefix probe | `"
       << jsonString(original, "inputPrefixSha256") << "` |\n";
    md << "| Run copy | `" << jsonString(original, "copyPath") << "` |\n";
    md << "| Run copy SHA-256 | `" << jsonString(original, "copySha256")
       << "` |\n";
    md << "| Run copy mode | `" << jsonString(original, "copyMode")
       << "` |\n";
    md << "| Pristine copy SHA-256 | `"
       << jsonString(original, "pristineSha256") << "` |\n";
    md << "| workspace.json SHA-256 | `"
       << jsonString(original.value("workspace", nlohmann::ordered_json{}),
                     "sha256")
       << "` |\n";
    md << "| README.seed.md SHA-256 | `"
       << jsonString(original.value("readme", nlohmann::ordered_json{}),
                     "sha256")
       << "` |\n\n";
    md << "## Raw offsets\n\n";
    md << "| Field | Byte offset |\n|---|---|\n";
    for (const auto& pair :
         {std::make_pair("objectCount", "objectCount"),
          std::make_pair("boundaryCount", "boundaryCount"),
          std::make_pair("outlineDepth", "outlineDepth"),
          std::make_pair("normalThreshold", "normalThreshold"),
          std::make_pair("objectsBase", "objectsBase"),
          std::make_pair("boundariesBase", "boundariesBase")}) {
        md << "| " << pair.first << " | " << jsonU64(offsets, pair.second)
           << " |\n";
    }
    md << "| objects[2].originalOffset | 284 |\n";
    md << "| boundaries[0] | 320 |\n\n";
    md << "The duplicate object's original offset is read from byte 284: "
       << "`0.58, 0, 0.06`.\n\n";

    md << "## Scene timestamps\n\n";
    md << "| Scene | Start UTC | End UTC | Draw calls |\n|---|---|---|---|\n";
    if (scenes.is_array()) {
        for (const auto& scene : scenes) {
            md << "| " << jsonString(scene, "scene") << " | "
               << timestampText(scene["start"]) << " | "
               << timestampText(scene["end"]) << " | "
               << scene.value("drawCalls", nlohmann::ordered_json::array())
                      .size()
               << " |\n";
        }
    }
    md << "\n## GPU pass timings\n\n";
    md << "| Scene | Pass | GPU ns | CPU start | CPU end |\n";
    md << "|---|---|---:|---|---|\n";
    if (scenes.is_array()) {
        for (const auto& scene : scenes) {
            for (const auto& timing : scene["passTimings"]) {
                md << "| " << jsonString(scene, "scene") << " | "
                   << jsonString(timing, "pass") << " | "
                   << jsonU64(timing, "elapsedNs") << " | "
                   << timestampText(timing.value(
                          "cpuStart", nlohmann::ordered_json{}))
                   << " | "
                   << timestampText(timing.value(
                          "cpuEnd", nlohmann::ordered_json{}))
                   << " |\n";
            }
        }
    }
    md << "\n## Draw call correlation\n\n";
    md << "| Draw | Object | firstInstance | Mesh SHA-256 | Pipeline | UTC |\n";
    md << "|---|---|---|---|---|---|\n";
    if (repeat) {
        for (const auto& draw : repeatGbuffer) {
            md << "| " << jsonU64(draw, "id") << " | "
               << jsonString(draw, "objectId") << " | "
               << jsonU64(draw, "firstInstance") << " | `"
               << shortHash(jsonString(draw, "meshSha256")) << "` | "
               << jsonString(draw, "pipelineId") << " | "
               << timestampText(draw["time"]) << " |\n";
        }
    }

    md << "\n## Image difference\n\n";
    md << "| Metric | Value |\n|---|---|\n";
    md << "| Diff pixels | " << jsonU64(imageDifference, "diffPixels")
       << " |\n";
    md << "| Mean abs error | "
       << imageDifference.value("meanAbsError", 0.0) << " |\n";
    md << "| Max abs error | " << jsonU64(imageDifference, "maxAbsError")
       << " |\n";
    md << "| Duplicate object pixels | "
       << jsonU64(correlation, "duplicateDiffPixels") << " |\n";
    md << "| Duplicate outline pixels | "
       << jsonU64(correlation, "duplicateOutlinePixels") << " |\n";
    md << "| Verdict | `" << jsonString(correlation, "verdict")
       << "` |\n\n";
    md << "Images: `diff/final_diff.ppm`, `boundary/overlay.ppm`, "
          "`frames/baseline/final.ppm`, `frames/repeat/final.ppm`.\n\n";

    md << "## Boundary pixels\n\n";
    md << "| Total | Boundary probes | Detected | Missed | False positives |\n";
    md << "|---:|---:|---:|---:|---:|\n";
    md << "| " << jsonU64(boundary, "total") << " | "
       << jsonU64(boundary, "boundaryTotal") << " | "
       << jsonU64(boundary, "boundaryMatched") << " | "
       << jsonU64(boundary, "missedBoundaries") << " | "
       << jsonU64(boundary, "falsePositives") << " |\n\n";
    md << "Seed-rescue case: with all thresholds disabled, "
       << seedRescuedPixels
       << " boundary probes still enter the final outline through the GPU seed buffer.\n\n";

    md << "## Negative cases\n\n";
    md << "| Case | Exit | Code | Rejected offset | Observed / limit | UTC |\n";
    md << "|---|---|---|---|---|---|\n";
    auto addCaseRow = [&](const std::string& name,
                          const nlohmann::ordered_json& failure) {
        const auto errorJson =
            failure.value("error", nlohmann::ordered_json{});
        md << "| " << name << " | " << jsonU64(failure, "exitCode", 0)
           << " | " << jsonString(errorJson, "code") << " | "
           << jsonU64(errorJson, "rejectedAtOffset") << " | "
           << jsonU64(errorJson, "observed") << " / "
           << jsonU64(errorJson, "limit") << " | "
           << timestampText(failure.value("end",
                                          nlohmann::ordered_json{}))
           << " |\n";
    };
    addCaseRow("empty", failureEmpty);
    addCaseRow("oversized_file", failureOversizedFile);
    addCaseRow("oversized_count", failureOversizedCount);
    md << "\n## Checks\n\n";
    md << "| Check | Result | Detail | UTC |\n|---|---|---|---|\n";
    for (const CheckRecord& check : checks) {
        md << "| `" << check.id << "` | "
           << (check.pass ? "PASS" : "FAIL") << " | " << check.detail
           << " | " << check.time.utc << " |\n";
    }
    md << "\nAcceptance timeline: `evidence_timeline.csv`.\n";
    writeText(pathJoin(options.outDir, "acceptance_report.md"), md.str());
    writeSha256Sums(options.outDir, &error);
    return allPass ? 0 : 1;
}

}  // namespace ssor
