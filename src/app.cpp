#include "app.hpp"

#include "evidence.hpp"
#include "glfw_session.hpp"
#include "json.hpp"
#include "pipeline.hpp"
#include "state.hpp"
#include "vegetation.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace vegd {
namespace {

struct Options {
    std::string mode = "full";
    std::string output = "artifacts/result_full.json";
    std::string statePath = "artifacts/checkpoint.vstate";
    std::string evidencePath = "artifacts/evidence.jsonl";
    std::string compareA;
    std::string compareB;
    uint64_t budgetBytes = kDefaultBudgetBytes;
    uint32_t pauseAt = 28;
    bool forceCpu = false;
    bool resetEvidence = false;
    bool interactive = false;
    bool showHelp = false;
};

void printUsage() {
    std::cout
        << "procedural vegetation distribution (C++17 / Vulkan / GLFW)\n"
        << "usage: vegdist --mode full|pause|resume|compare [options]\n"
        << "  --out PATH          result JSON path\n"
        << "  --state PATH        checkpoint path used by pause/resume\n"
        << "  --evidence PATH     JSONL evidence path\n"
        << "  --reset-evidence    truncate evidence before this run\n"
        << "  --budget-kb N       simulated GPU instance budget\n"
        << "  --pause-at N        stop after tile N-1 (default 28)\n"
        << "  --force-cpu         exercise the degradation path\n"
        << "  --ui                open a GLFW status window\n"
        << "  --a PATH --b PATH   inputs for compare mode\n";
}

bool parseU64(const std::string& text, uint64_t& value) {
    try {
        size_t consumed = 0;
        const unsigned long long parsed = std::stoull(text, &consumed, 0);
        if (consumed != text.size()) {
            return false;
        }
        value = parsed;
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool parseOptions(int argc, char** argv, Options& options, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        auto requireValue = [&](std::string& destination) {
            if (i + 1 >= argc) {
                error = "missing value for " + argument;
                return false;
            }
            destination = argv[++i];
            return true;
        };
        if (argument == "--mode") {
            if (!requireValue(options.mode)) {
                return false;
            }
        } else if (argument == "--out") {
            if (!requireValue(options.output)) {
                return false;
            }
        } else if (argument == "--state") {
            if (!requireValue(options.statePath)) {
                return false;
            }
        } else if (argument == "--evidence") {
            if (!requireValue(options.evidencePath)) {
                return false;
            }
        } else if (argument == "--a") {
            if (!requireValue(options.compareA)) {
                return false;
            }
        } else if (argument == "--b") {
            if (!requireValue(options.compareB)) {
                return false;
            }
        } else if (argument == "--budget-kb") {
            std::string value;
            if (!requireValue(value)) {
                return false;
            }
            uint64_t kilobytes = 0;
            if (!parseU64(value, kilobytes)) {
                error = "invalid --budget-kb value";
                return false;
            }
            options.budgetBytes = kilobytes * 1024ull;
        } else if (argument == "--pause-at") {
            std::string value;
            if (!requireValue(value)) {
                return false;
            }
            uint64_t parsed = 0;
            if (!parseU64(value, parsed) || parsed > kTileCount) {
                error = "invalid --pause-at value";
                return false;
            }
            options.pauseAt = static_cast<uint32_t>(parsed);
        } else if (argument == "--force-cpu") {
            options.forceCpu = true;
        } else if (argument == "--reset-evidence") {
            options.resetEvidence = true;
        } else if (argument == "--ui") {
            options.interactive = true;
        } else if (argument == "--help" || argument == "-h") {
            options.showHelp = true;
        } else {
            error = "unknown argument: " + argument;
            return false;
        }
    }
    return true;
}

bool emit(const Options& options, const std::string& event, const Json& fields,
          std::string& error) {
    return appendEvidence(options.evidencePath, event, fields, error);
}

Json stateFields(const StateIoResult& state, const std::string& path) {
    Json out = Json::object();
    out["path"] = path;
    out["bytes"] = state.bytes;
    out["checksum"] = hex64(state.checksum);
    return out;
}

Json samplePair(const BoundarySample& before, const BoundarySample& after) {
    Json samples = Json::object();
    samples["beforeBoundary"] = sampleJson(before);
    samples["afterBoundary"] = sampleJson(after);
    return samples;
}

Json resultDocument(const std::string& tag, bool resumed,
                    const TileProcessor& processor,
                    const std::vector<TileData>& tiles,
                    const BoundarySample& before,
                    const BoundarySample& after,
                    const std::string& startedAt, const Params& params,
                    uint64_t budgetBytes, const Json& extraState) {
    Json result = Json::object();
    result["backend"] = processor.backendName();
    result["bufferHash"] = hex64(hashAggregate(tiles));
    result["counts"] = Json::array();
    uint64_t total = 0;
    for (const TileData& tile : tiles) {
        result["counts"].push_back(tile.count);
        total += tile.count;
    }
    result["degradations"] = degradationsJson(processor.degradations());
    result["device"] = processor.deviceName();
    result["effectiveCapacity"] = processor.capacity();
    result["finishedAt"] = utcNow();
    result["magic"] = "vegres1";
    result["missingSegment"] =
        Json::array({params.missingStart, params.missingEnd});
    result["paramHash"] = hex64(params.hash());
    result["resourceBudgetBytes"] = budgetBytes;
    result["resumed"] = resumed;
    result["samples"] = samplePair(before, after);
    result["startedAt"] = startedAt;
    result["tag"] = tag;
    result["tilesTotal"] = kTileCount;
    result["totalAccepted"] = total;
    if (!extraState.is_null() && !extraState.empty()) {
        result["state"] = extraState;
    }
    return result;
}

}  // namespace
}  // namespace vegd

namespace vegd {
namespace {

bool beginEvidence(const Options& options, std::string& error) {
    return !options.resetEvidence || resetEvidence(options.evidencePath, error);
}

bool initializeGlfw(const Options& options, GlfwSession& glfw,
                    std::string& error) {
    std::string glfwError;
    const bool ok = glfw.initialize(options.interactive, glfwError);
    Json fields = Json::object();
    fields["ok"] = ok;
    fields["interactive"] = options.interactive;
    fields["version"] = glfw.version();
    if (!ok) {
        fields["error"] = glfwError;
    }
    std::string evidenceError;
    emit(options, "glfw-init", fields, evidenceError);
    if (!ok) {
        error = glfwError;
        return false;
    }
    if (options.interactive) {
        if (!glfw.createWindow(glfwError)) {
            error = glfwError;
            return false;
        }
        glfw.setTitle("Procedural Vegetation - preparing Vulkan");
    }
    return true;
}

bool initializeProcessor(const Options& options, TileProcessor& processor,
                         GlfwSession* glfw, std::string& error) {
    if (!processor.initialize(error)) {
        return false;
    }
    Json backend = Json::object();
    backend["backend"] = processor.backendName();
    backend["device"] = processor.deviceName();
    backend["capacity"] = processor.capacity();
    backend["degradations"] = degradationsJson(processor.degradations());
    std::string evidenceError;
    emit(options, "backend", backend, evidenceError);
    if (glfw != nullptr && glfw->interactive()) {
        glfw->setTitle("Procedural Vegetation - " + processor.backendName() +
                       " / " + processor.deviceName());
    }
    return true;
}

bool processRange(TileProcessor& processor, uint32_t startTile,
                  uint32_t stopTile,
                  std::vector<TileData>& tiles, GlfwSession* glfw,
                  std::string& error) {
    for (uint32_t tile = startTile; tile < stopTile; ++tile) {
        TileData generated;
        if (!processor.generate(tile, generated, error)) {
            return false;
        }
        tiles[tile] = std::move(generated);
        if (glfw != nullptr && glfw->interactive()) {
            glfw->setTitle("Procedural Vegetation - tile " +
                           std::to_string(tile) + "/" +
                           std::to_string(kTileCount) + " - " +
                           processor.backendName());
            glfw->poll();
            glfw->waitEvents(0.012);
            if (glfw->shouldClose()) {
                return true;
            }
        }
    }
    return true;
}

Json paramsFields(const Params& params) {
    Json fields = Json::object();
    fields["seed"] = hex64(params.seed);
    fields["densityMilli"] = params.densityMilli;
    fields["jitterMilli"] = params.jitterMilli;
    fields["altitudeMilli"] = params.altitudeMilli;
    fields["slopeBiasMilli"] = params.slopeBiasMilli;
    fields["windMilli"] = params.windMilli;
    fields["boundaryBoostMilli"] = params.boundaryBoostMilli;
    fields["maxInstancesPerTile"] = params.maxInstancesPerTile;
    fields["missingSegment"] =
        Json::array({params.missingStart, params.missingEnd});
    fields["paramHash"] = hex64(params.hash());
    return fields;
}

Json resourceFields(const ResourcePlan& plan) {
    Json fields = Json::object();
    fields["budgetBytes"] = plan.budgetBytes;
    fields["capacity"] = plan.capacity;
    fields["degradations"] = degradationsJson(plan.degradations);
    return fields;
}

void reportBackend(const Options& options, const TileProcessor& processor) {
    Json fields = Json::object();
    fields["backend"] = processor.backendName();
    fields["device"] = processor.deviceName();
    fields["capacity"] = processor.capacity();
    fields["degradations"] = degradationsJson(processor.degradations());
    std::string error;
    emit(options, "backend-ready", fields, error);
}

bool writeResult(const Options& options, const Json& result, std::string& error) {
    if (!writeJsonFile(options.output, result, error)) {
        return false;
    }
    Json fields = Json::object();
    fields["path"] = options.output;
    fields["bufferHash"] = result["bufferHash"];
    fields["totalAccepted"] = result["totalAccepted"];
    fields["backend"] = result["backend"];
    std::string evidenceError;
    emit(options, "result-written", fields, evidenceError);
    return true;
}

int runFull(const Options& options) {
    std::string error;
    if (!beginEvidence(options, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const Params params = Params::extreme();
    const ResourcePlan plan = planResources(params, options.budgetBytes);
    const std::string startedAt = utcNow();
    emit(options, "session-start",
         Json{{"mode", "full"}, {"startedAt", startedAt}}, error);
    emit(options, "params", paramsFields(params), error);
    emit(options, "resource-plan", resourceFields(plan), error);
    emit(options, "missing-segment",
         Json{{"start", params.missingStart}, {"end", params.missingEnd}},
         error);

    GlfwSession glfw;
    if (!initializeGlfw(options, glfw, error)) {
        std::cerr << "GLFW initialization failed: " << error << '\n';
        return 1;
    }
    TileProcessor processor(params, plan, options.forceCpu);
    if (!initializeProcessor(options, processor, &glfw, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    reportBackend(options, processor);

    std::vector<TileData> tiles(kTileCount);
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        tiles[tile].tile = tile;
    }
    if (!processRange(processor, 0, kTileCount, tiles, &glfw, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const BoundarySample before =
        sampleBoundary(params, tiles, params.missingStart - 1);
    const BoundarySample after = sampleBoundary(params, tiles, params.missingEnd);
    emit(options, "boundary-samples",
         Json{{"before", sampleJson(before)}, {"after", sampleJson(after)}},
         error);
    const Json result =
        resultDocument("full", false, processor, tiles, before, after, startedAt,
                       params, options.budgetBytes, Json::object());
    if (!writeResult(options, result, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "full backend=" << processor.backendName()
              << " total=" << result["totalAccepted"]
              << " hash=" << result["bufferHash"] << '\n';
    return 0;
}

Checkpoint buildCheckpoint(const Params& params, const ResourcePlan& plan,
                           const std::vector<TileData>& tiles,
                           uint32_t nextTile) {
    Checkpoint checkpoint =
        makeEmptyCheckpoint(params, plan.capacity, plan.budgetBytes);
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        if (tiles[tile].completed || tiles[tile].count > 0) {
            applyTile(checkpoint, tiles[tile]);
        }
    }
    checkpoint.nextTile = nextTile;
    checkpoint.aggregateHash = hashAggregate(tiles);
    return checkpoint;
}

int runPause(const Options& options) {
    std::string error;
    if (!beginEvidence(options, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const Params params = Params::extreme();
    const ResourcePlan plan = planResources(params, options.budgetBytes);
    const std::string startedAt = utcNow();
    emit(options, "session-start",
         Json{{"mode", "pause"}, {"startedAt", startedAt}}, error);
    emit(options, "params", paramsFields(params), error);
    emit(options, "resource-plan", resourceFields(plan), error);
    emit(options, "missing-segment",
         Json{{"start", params.missingStart}, {"end", params.missingEnd}},
         error);
    emit(options, "pause-request",
         Json{{"pauseAt", options.pauseAt},
              {"boundaryTile", params.missingStart - 1}},
         error);

    GlfwSession glfw;
    if (!initializeGlfw(options, glfw, error)) {
        std::cerr << "GLFW initialization failed: " << error << '\n';
        return 1;
    }
    TileProcessor processor(params, plan, options.forceCpu);
    if (!initializeProcessor(options, processor, &glfw, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    reportBackend(options, processor);

    std::vector<TileData> tiles(kTileCount);
    for (uint32_t tile = 0; tile < kTileCount; ++tile) {
        tiles[tile].tile = tile;
    }
    const uint32_t stop = std::min(options.pauseAt, kTileCount);
    if (!processRange(processor, 0, stop, tiles, &glfw, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    uint32_t nextTile = stop;
    if (nextTile >= params.missingStart && nextTile < params.missingEnd) {
        nextTile = params.missingEnd;
    }
    const Checkpoint checkpoint =
        buildCheckpoint(params, plan, tiles, nextTile);
    const StateIoResult saved = saveCheckpoint(options.statePath, checkpoint);
    if (!saved.ok) {
        std::cerr << saved.error << '\n';
        return 1;
    }
    const BoundarySample before =
        sampleBoundary(params, tiles, params.missingStart - 1);
    Json state = stateFields(saved, options.statePath);
    state["nextTile"] = checkpoint.nextTile;
    state["completedTiles"] =
        static_cast<uint32_t>(__builtin_popcountll(checkpoint.completedMask));
    state["beforeBoundary"] = sampleJson(before);
    emit(options, "pause", state, error);
    emit(options, "state-saved", state, error);

    const Json result =
        resultDocument("paused", false, processor, tiles, before,
                       BoundarySample{}, startedAt, params, plan.budgetBytes,
                       state);
    if (!writeResult(options, result, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "paused at nextTile=" << checkpoint.nextTile
              << " state=" << options.statePath
              << " checksum=" << hex64(saved.checksum) << '\n';
    return 0;
}

int runResume(const Options& options) {
    std::string error;
    if (!beginEvidence(options, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const std::string startedAt = utcNow();
    emit(options, "session-start",
         Json{{"mode", "resume"}, {"startedAt", startedAt}}, error);
    const StateLoadResult loaded = loadCheckpoint(options.statePath);
    if (!loaded.ok) {
        std::cerr << loaded.error << '\n';
        return 1;
    }
    const Checkpoint& checkpoint = loaded.checkpoint;
    const Params params = checkpoint.params;
    const uint32_t loadedCompleted =
        static_cast<uint32_t>(__builtin_popcountll(checkpoint.completedMask));
    Json loadFields = stateFields(
        StateIoResult{true, loaded.bytes, loaded.checksum, {}}, options.statePath);
    loadFields["nextTile"] = checkpoint.nextTile;
    loadFields["completedTiles"] = loadedCompleted;
    loadFields["effectiveCapacity"] = checkpoint.effectiveCapacity;
    loadFields["paramHash"] = hex64(params.hash());
    emit(options, "params", paramsFields(params), error);
    emit(options, "resume", loadFields, error);
    emit(options, "state-loaded", loadFields, error);

    ResourcePlan plan = planResources(params, checkpoint.budgetBytes);
    if (plan.capacity != checkpoint.effectiveCapacity) {
        plan.degradations.push_back(
            "checkpoint-capacity-restored:" +
            std::to_string(checkpoint.effectiveCapacity));
        plan.capacity = checkpoint.effectiveCapacity;
    }
    emit(options, "resource-plan", resourceFields(plan), error);

    GlfwSession glfw;
    if (!initializeGlfw(options, glfw, error)) {
        std::cerr << "GLFW initialization failed: " << error << '\n';
        return 1;
    }
    TileProcessor processor(params, plan, options.forceCpu);
    if (!initializeProcessor(options, processor, &glfw, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    reportBackend(options, processor);

    std::vector<TileData> tiles = checkpointTiles(checkpoint);
    const BoundarySample beforeAtPause =
        sampleBoundary(params, tiles, params.missingStart - 1);
    const uint32_t startTile = checkpoint.nextTile;
    if (!processRange(processor, startTile, kTileCount, tiles, &glfw,
                      error)) {
        std::cerr << error << '\n';
        return 1;
    }
    const BoundarySample before =
        sampleBoundary(params, tiles, params.missingStart - 1);
    const BoundarySample after = sampleBoundary(params, tiles, params.missingEnd);
    const bool beforeStable =
        before.valid == beforeAtPause.valid &&
        before.acceptedCount == beforeAtPause.acceptedCount &&
        before.tileHash == beforeAtPause.tileHash &&
        before.firstPos.x == beforeAtPause.firstPos.x &&
        before.firstPos.y == beforeAtPause.firstPos.y &&
        before.firstPos.z == beforeAtPause.firstPos.z;
    Json boundary = Json::object();
    boundary["beforeAtPause"] = sampleJson(beforeAtPause);
    boundary["beforeFinal"] = sampleJson(before);
    boundary["afterFinal"] = sampleJson(after);
    boundary["beforeStable"] = beforeStable;
    emit(options, "resume-boundary", boundary, error);

    Json state = loadFields;
    state["beforeStable"] = beforeStable;
    state["beforeAtPause"] = sampleJson(beforeAtPause);
    state["startTile"] = startTile;
    const Json result =
        resultDocument("resumed", true, processor, tiles, before, after,
                       startedAt, params, checkpoint.budgetBytes, state);
    if (!writeResult(options, result, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    std::cout << "resumed from tile=" << startTile
              << " backend=" << processor.backendName()
              << " total=" << result["totalAccepted"]
              << " hash=" << result["bufferHash"] << '\n';
    return 0;
}

Json boundaryDelta(const Json& a, const Json& b) {
    Json delta = Json::object();
    const Json beforeA = a.value("samples", Json::object())
                             .value("beforeBoundary", Json::object());
    const Json afterA = a.value("samples", Json::object())
                            .value("afterBoundary", Json::object());
    const Json beforeB = b.value("samples", Json::object())
                             .value("beforeBoundary", Json::object());
    const Json afterB = b.value("samples", Json::object())
                            .value("afterBoundary", Json::object());
    const Json beforeAtPause =
        b.value("state", Json::object()).value("beforeAtPause", beforeB);
    delta["beforeAtPause"] = beforeAtPause;
    delta["beforeFinal"] = beforeB;
    delta["afterFinal"] = afterB;
    delta["beforeChanged"] = beforeA != beforeB;
    delta["afterChanged"] = afterA != afterB;
    return delta;
}

int runCompare(const Options& options) {
    std::string error;
    if (!beginEvidence(options, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    if (options.compareA.empty() || options.compareB.empty()) {
        std::cerr << "compare mode requires --a and --b\n";
        return 1;
    }
    Json a;
    Json b;
    if (!readJsonFile(options.compareA, a, error) ||
        !readJsonFile(options.compareB, b, error)) {
        std::cerr << error << '\n';
        return 1;
    }

    Json diffs = Json::array();
    const char* scalarFields[] = {"bufferHash", "paramHash", "totalAccepted",
                                  "effectiveCapacity"};
    for (const char* field : scalarFields) {
        if (a.value(field, Json()) != b.value(field, Json())) {
            diffs.push_back(std::string(field) + ": " +
                            a.value(field, Json()).dump() + " != " +
                            b.value(field, Json()).dump());
        }
    }
    if (a.value("missingSegment", Json()) != b.value("missingSegment", Json())) {
        diffs.push_back("missingSegment differs");
    }

    Json countDiffs = Json::array();
    const Json countsA = a.value("counts", Json::array());
    const Json countsB = b.value("counts", Json::array());
    if (countsA.size() != countsB.size()) {
        diffs.push_back("counts length differs");
    } else {
        for (size_t tile = 0; tile < countsA.size(); ++tile) {
            if (countsA[tile] != countsB[tile]) {
                countDiffs.push_back(
                    Json{{"tile", tile},
                         {"a", countsA[tile]},
                         {"b", countsB[tile]}});
            }
        }
    }
    if (!countDiffs.empty()) {
        diffs.push_back("per-tile counts differ: " +
                        std::to_string(countDiffs.size()) + " tiles");
    }
    Json delta = boundaryDelta(a, b);
    if (delta["beforeChanged"].get<bool>()) {
        diffs.push_back("before-boundary sample changed");
    }
    if (delta["afterChanged"].get<bool>()) {
        diffs.push_back("after-boundary sample changed");
    }
    if (a.value("samples", Json()) != b.value("samples", Json()) &&
        !delta["beforeChanged"].get<bool>() &&
        !delta["afterChanged"].get<bool>()) {
        diffs.push_back("samples object differs");
    }

    Json result = Json::object();
    result["a"] = a.value("tag", options.compareA);
    result["b"] = b.value("tag", options.compareB);
    result["backendA"] = a.value("backend", "unknown");
    result["backendB"] = b.value("backend", "unknown");
    result["boundaryDelta"] = delta;
    result["bufferHashA"] = a.value("bufferHash", "");
    result["bufferHashB"] = b.value("bufferHash", "");
    result["countDiffs"] = countDiffs;
    result["degradationsA"] = a.value("degradations", Json::array());
    result["degradationsB"] = b.value("degradations", Json::array());
    result["diffs"] = diffs;
    result["samplesA"] = a.value("samples", Json::object());
    result["samplesB"] = b.value("samples", Json::object());
    result["verdict"] = diffs.empty() ? "identical" : "different";
    result["provenanceChanged"] =
        a.value("backend", "") != b.value("backend", "") ||
        a.value("device", "") != b.value("device", "");
    if (!writeJsonFile(options.output, result, error)) {
        std::cerr << error << '\n';
        return 1;
    }
    Json evidence = Json::object();
    evidence["verdict"] = result["verdict"];
    evidence["bufferHashA"] = result["bufferHashA"];
    evidence["bufferHashB"] = result["bufferHashB"];
    evidence["provenanceChanged"] = result["provenanceChanged"];
    evidence["diffs"] = diffs;
    emit(options, "compare", evidence, error);
    std::cout << "compare verdict=" << result["verdict"]
              << " diffs=" << diffs.size() << '\n';
    return diffs.empty() ? 0 : 2;
}

}  // namespace
}  // namespace vegd

namespace vegd {

int runApplication(int argc, char** argv) {
    Options options;
    std::string error;
    if (!parseOptions(argc, argv, options, error)) {
        std::cerr << error << "\n\n";
        printUsage();
        return 1;
    }
    if (options.showHelp) {
        printUsage();
        return 0;
    }
    if (options.mode == "ui") {
        options.mode = "full";
        options.interactive = true;
    }
    if (options.mode != "full" && options.mode != "pause" &&
        options.mode != "resume" && options.mode != "compare") {
        std::cerr << "unknown mode: " << options.mode << '\n';
        return 1;
    }
    if (options.mode == "pause" &&
        options.output == "artifacts/result_full.json") {
        options.output = "artifacts/result_paused.json";
    } else if (options.mode == "resume" &&
               options.output == "artifacts/result_full.json") {
        options.output = "artifacts/result_resumed.json";
    } else if (options.mode == "compare" &&
               options.output == "artifacts/result_full.json") {
        options.output = "artifacts/compare_full_resumed.json";
    }
    if (options.mode == "full") {
        return runFull(options);
    }
    if (options.mode == "pause") {
        return runPause(options);
    }
    if (options.mode == "resume") {
        return runResume(options);
    }
    return runCompare(options);
}

}  // namespace vegd
