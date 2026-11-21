#include "core/workspace.h"

#include "core/obj_io.h"
#include "core/sha256.h"
#include "core/time_util.h"
#include "core/workspace_json.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <random>
#include <stdexcept>

namespace clipfix {
namespace {

using Json = nlohmann::json;

std::array<double, 3> readOffsetArray(const Json& value, const std::string& context) {
    if (!value.is_array() || value.size() != 3) {
        throw std::runtime_error(context + " must contain exactly three offsets");
    }
    return {
        value[0].get<double>(),
        value[1].get<double>(),
        value[2].get<double>(),
    };
}

Json offsetArrayJson(const std::array<double, 3>& values) {
    return Json::array({values[0], values[1], values[2]});
}

std::string canonicalJson(const Json& value) {
    return value.dump();
}

std::filesystem::path defaultMaterialRoot() {
    const char* fromEnvironment = std::getenv("CLIPFIX_RAW_DIR");
    if (fromEnvironment != nullptr && *fromEnvironment != '\0') {
        return fromEnvironment;
    }
    const std::filesystem::path sourceMaterial =
        std::filesystem::path(CLIPFIX_SOURCE_DIR) / "materials" / "raw";
    if (std::filesystem::is_directory(sourceMaterial)) {
        return sourceMaterial;
    }
    return std::filesystem::path("materials") / "raw";
}

void writeTextFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write: " + path.string());
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("failed while writing: " + path.string());
    }
}

}  // namespace

namespace detail {

Json vec3Json(const Vec3& value) {
    return Json::array({value.x, value.y, value.z});
}

Json statsJson(const RunStats& stats) {
    return {
        {"vertex_count", stats.vertexCount},
        {"affected_count", stats.affectedCount},
        {"max_abs_before_mm", stats.maxAbsBeforeMm},
        {"mean_abs_before_mm", stats.meanAbsBeforeMm},
        {"max_abs_after_mm", stats.maxAbsAfterMm},
        {"mean_abs_after_mm", stats.meanAbsAfterMm},
        {"max_delta_mm", stats.maxDeltaMm},
        {"mean_delta_mm", stats.meanDeltaMm},
    };
}

Json vertexJson(const VertexEvidence& vertex) {
    return {
        {"vertex_index", vertex.vertexIndex},
        {"curve_t", vertex.curveT},
        {"lateral_mm", vertex.lateralMm},
        {"before_offset_mm", vertex.beforeOffsetMm},
        {"after_offset_mm", vertex.afterOffsetMm},
        {"delta_mm", vertex.deltaMm},
    };
}

Json runJson(const RunRecord& run) {
    Json keyVertices = Json::array();
    for (const auto& vertex : run.keyVertices) {
        keyVertices.push_back(vertexJson(vertex));
    }
    return {
        {"run_id", run.runId},
        {"object_id", run.objectId},
        {"started_utc", run.startedUtc},
        {"finished_utc", run.finishedUtc},
        {"started_epoch_ms", run.startedEpochMs},
        {"finished_epoch_ms", run.finishedEpochMs},
        {"raw_offsets_mm", offsetArrayJson(run.rawOffsetsMm)},
        {"applied_offsets_mm", offsetArrayJson(run.appliedOffsetsMm)},
        {"band_mm", run.bandMm},
        {"width_mm", run.widthMm},
        {"stats", statsJson(run.stats)},
        {"source_obj_sha256", run.sourceSha256},
        {"output_obj_sha256", run.outputObjSha256},
        {"evidence_csv_sha256", run.evidenceCsvSha256},
        {"output_obj", run.outputObjPath},
        {"evidence_csv", run.evidenceCsvPath},
        {"key_vertices", keyVertices},
    };
}

Json compareJson(const CompareResult& compare) {
    return {
        {"object_id", compare.objectId},
        {"created_utc", compare.createdUtc},
        {"a_kind", compare.aKind},
        {"b_kind", compare.bKind},
        {"a_ref", compare.aRef},
        {"b_ref", compare.bRef},
        {"compared_vertices", compare.comparedVertices},
        {"changed_vertices", compare.changedVertices},
        {"max_delta_mm", compare.maxDeltaMm},
        {"mean_delta_mm", compare.meanDeltaMm},
        {"report_path", compare.reportPath},
    };
}

Json exportJson(const ExportRecord& exportRecord) {
    return {
        {"object_id", exportRecord.objectId},
        {"run_id", exportRecord.runId},
        {"exported_utc", exportRecord.exportedUtc},
        {"obj_path", exportRecord.objPath},
        {"report_path", exportRecord.reportPath},
        {"obj_sha256", exportRecord.objSha256},
    };
}

Json objectJson(const ObjectState& object) {
    Json runs = Json::array();
    for (const auto& run : object.runs) {
        runs.push_back(runJson(run));
    }
    Json value = {
        {"id", object.id},
        {"dirty", object.dirty},
        {"compare_ready", object.compareReady},
        {"has_compare", object.hasCompare},
        {"has_export", object.hasExport},
        {"runs", runs},
    };
    if (object.hasCompare) {
        value["last_compare"] = compareJson(object.lastCompare);
    }
    if (object.hasExport) {
        value["last_export"] = exportJson(object.lastExport);
    }
    return value;
}

Json sessionJson(const SessionState& state) {
    Json objects = Json::array();
    for (const auto& object : state.objects) {
        objects.push_back(objectJson(object));
    }
    return {
        {"format", "clipfix-session-v1"},
        {"revision", state.revision},
        {"suspended", state.suspended},
        {"selected_object", state.selectedObject},
        {"created_at", state.createdAt},
        {"updated_at", state.updatedAt},
        {"last_suspend_at", state.lastSuspendAt},
        {"last_resume_at", state.lastResumeAt},
        {"resume_count", state.resumeCount},
        {"current_offsets_mm", offsetArrayJson(state.currentOffsetsMm)},
        {"objects", objects},
    };
}

RunRecord runFromJson(const Json& value) {
    RunRecord run;
    run.runId = value.at("run_id").get<std::string>();
    run.objectId = value.at("object_id").get<std::string>();
    run.startedUtc = value.at("started_utc").get<std::string>();
    run.finishedUtc = value.at("finished_utc").get<std::string>();
    run.startedEpochMs = value.at("started_epoch_ms").get<long long>();
    run.finishedEpochMs = value.at("finished_epoch_ms").get<long long>();
    run.rawOffsetsMm = readOffsetArray(value.at("raw_offsets_mm"), "raw_offsets_mm");
    run.appliedOffsetsMm =
        readOffsetArray(value.at("applied_offsets_mm"), "applied_offsets_mm");
    run.bandMm = value.at("band_mm").get<double>();
    run.widthMm = value.at("width_mm").get<double>();
    const auto& stats = value.at("stats");
    run.stats.vertexCount = stats.at("vertex_count").get<std::size_t>();
    run.stats.affectedCount = stats.at("affected_count").get<std::size_t>();
    run.stats.maxAbsBeforeMm = stats.at("max_abs_before_mm").get<double>();
    run.stats.meanAbsBeforeMm = stats.at("mean_abs_before_mm").get<double>();
    run.stats.maxAbsAfterMm = stats.at("max_abs_after_mm").get<double>();
    run.stats.meanAbsAfterMm = stats.at("mean_abs_after_mm").get<double>();
    run.stats.maxDeltaMm = stats.at("max_delta_mm").get<double>();
    run.stats.meanDeltaMm = stats.at("mean_delta_mm").get<double>();
    run.sourceSha256 = value.at("source_obj_sha256").get<std::string>();
    run.outputObjSha256 = value.at("output_obj_sha256").get<std::string>();
    run.evidenceCsvSha256 = value.at("evidence_csv_sha256").get<std::string>();
    run.outputObjPath = value.at("output_obj").get<std::string>();
    run.evidenceCsvPath = value.at("evidence_csv").get<std::string>();
    for (const auto& vertexJson : value.value("key_vertices", Json::array())) {
        VertexEvidence vertex;
        vertex.vertexIndex = vertexJson.at("vertex_index").get<std::uint32_t>();
        vertex.curveT = vertexJson.at("curve_t").get<double>();
        vertex.lateralMm = vertexJson.at("lateral_mm").get<double>();
        vertex.beforeOffsetMm = vertexJson.at("before_offset_mm").get<double>();
        vertex.afterOffsetMm = vertexJson.at("after_offset_mm").get<double>();
        vertex.deltaMm = vertexJson.at("delta_mm").get<double>();
        run.keyVertices.push_back(vertex);
    }
    return run;
}

CompareResult compareFromJson(const Json& value) {
    CompareResult compare;
    compare.objectId = value.at("object_id").get<std::string>();
    compare.createdUtc = value.at("created_utc").get<std::string>();
    compare.aKind = value.at("a_kind").get<std::string>();
    compare.bKind = value.at("b_kind").get<std::string>();
    compare.aRef = value.at("a_ref").get<std::string>();
    compare.bRef = value.at("b_ref").get<std::string>();
    compare.comparedVertices = value.at("compared_vertices").get<std::size_t>();
    compare.changedVertices = value.at("changed_vertices").get<std::size_t>();
    compare.maxDeltaMm = value.at("max_delta_mm").get<double>();
    compare.meanDeltaMm = value.at("mean_delta_mm").get<double>();
    compare.reportPath = value.at("report_path").get<std::string>();
    return compare;
}

ExportRecord exportFromJson(const Json& value) {
    ExportRecord record;
    record.objectId = value.at("object_id").get<std::string>();
    record.runId = value.at("run_id").get<std::string>();
    record.exportedUtc = value.at("exported_utc").get<std::string>();
    record.objPath = value.at("obj_path").get<std::string>();
    record.reportPath = value.at("report_path").get<std::string>();
    record.objSha256 = value.at("obj_sha256").get<std::string>();
    return record;
}

ObjectState objectFromJson(const Json& value) {
    ObjectState object;
    object.id = value.at("id").get<std::string>();
    object.dirty = value.value("dirty", true);
    object.compareReady = value.value("compare_ready", false);
    object.hasCompare = value.value("has_compare", false);
    object.hasExport = value.value("has_export", false);
    for (const auto& run : value.value("runs", Json::array())) {
        object.runs.push_back(runFromJson(run));
    }
    if (object.hasCompare) {
        object.lastCompare = compareFromJson(value.at("last_compare"));
    }
    if (object.hasExport) {
        object.lastExport = exportFromJson(value.at("last_export"));
    }
    return object;
}

SessionState sessionFromJson(const Json& value) {
    if (value.value("format", "") != "clipfix-session-v1") {
        throw std::runtime_error("unsupported session format");
    }
    SessionState state;
    state.revision = value.at("revision").get<int>();
    state.suspended = value.value("suspended", false);
    state.selectedObject = value.value("selected_object", 0);
    state.createdAt = value.value("created_at", "");
    state.updatedAt = value.value("updated_at", "");
    state.lastSuspendAt = value.value("last_suspend_at", "");
    state.lastResumeAt = value.value("last_resume_at", "");
    state.resumeCount = value.value("resume_count", 0);
    state.currentOffsetsMm =
        readOffsetArray(value.at("current_offsets_mm"), "current_offsets_mm");
    for (const auto& object : value.value("objects", Json::array())) {
        state.objects.push_back(objectFromJson(object));
    }
    return state;
}

}  // namespace detail

Workspace Workspace::open(const WorkspaceOptions& options) {
    Workspace workspace;
    workspace.root_ = std::filesystem::absolute(options.root).lexically_normal();
    if (std::filesystem::exists(workspace.root_ / "session.json")) {
        workspace.loadExisting(options);
    } else {
        workspace.initialize(options);
    }
    return workspace;
}

void Workspace::initialize(const WorkspaceOptions& options) {
    rawCopyRoot_ = root_ / "original_material";
    const auto materialRoot =
        options.materialRoot.empty() ? defaultMaterialRoot() : options.materialRoot;
    sourceRoot_ = std::filesystem::absolute(materialRoot).lexically_normal();
    if (!std::filesystem::is_directory(materialRoot)) {
        throw std::runtime_error(
            "raw material directory not found: " + materialRoot.string());
    }

    RawMaterialBundle sourceMaterial = loadRawMaterial(materialRoot);
    std::string rawError;
    if (!verifyRawMaterial(sourceMaterial, &rawError)) {
        throw std::runtime_error("raw material verification failed: " + rawError);
    }

    std::filesystem::create_directories(root_);
    std::filesystem::create_directories(root_ / "runs");
    std::filesystem::create_directories(root_ / "evidence");
    std::filesystem::create_directories(root_ / "comparisons");
    std::filesystem::create_directories(root_ / "exports");

    if (!std::filesystem::exists(rawCopyRoot_ / "MANIFEST.json")) {
        copyRawMaterial(sourceMaterial, rawCopyRoot_);
    }
    protectRawCopy();
    material_ = loadRawMaterial(rawCopyRoot_);
    if (!verifyRawMaterial(material_, &rawError)) {
        throw std::runtime_error("workspace raw copy verification failed: " + rawError);
    }
    if (material_.aggregateSha256 != sourceMaterial.aggregateSha256) {
        throw std::runtime_error("workspace raw copy does not match source material");
    }

    const Timestamp now = nowTimestamp();
    state_ = SessionState{};
    state_.createdAt = now.utc;
    state_.updatedAt = now.utc;
    state_.selectedObject = 0;
    for (std::size_t i = 0; i < material_.scene.anchors.size(); ++i) {
        state_.currentOffsetsMm[i] = material_.scene.anchors[i].rawOffsetMm;
    }
    for (const auto& rawObject : material_.scene.objects) {
        ObjectState object;
        object.id = rawObject.id;
        object.dirty = true;
        state_.objects.push_back(object);
    }
    save();

    Json objectIds = Json::array();
    for (const auto& object : material_.scene.objects) {
        objectIds.push_back(object.id);
    }
    appendEvent(
        "workspace_created",
        {
            {"workspace", root_.string()},
            {"raw_copy", rawCopyRoot_.string()},
            {"raw_aggregate_sha256", material_.aggregateSha256},
            {"raw_manifest_sha256", material_.manifestSha256},
            {"anchor_raw_offsets_mm", offsetArrayJson(rawOffsets())},
            {"object_ids", objectIds},
        });
}

void Workspace::loadExisting(const WorkspaceOptions& options) {
    sourceRoot_ = options.materialRoot.empty()
        ? std::filesystem::absolute(defaultMaterialRoot()).lexically_normal()
        : std::filesystem::absolute(options.materialRoot).lexically_normal();
    rawCopyRoot_ = root_ / "original_material";
    std::string rawError;
    material_ = loadRawMaterial(rawCopyRoot_);
    if (!verifyRawMaterial(material_, &rawError)) {
        throw std::runtime_error("workspace raw copy verification failed: " + rawError);
    }

    std::ifstream input(root_ / "session.json");
    if (!input) {
        throw std::runtime_error("cannot read session.json");
    }
    Json value;
    input >> value;
    state_ = detail::sessionFromJson(value);
    if (state_.currentOffsetsMm.size() != 3) {
        throw std::runtime_error("session has invalid anchor offsets");
    }

    for (const auto& rawObject : material_.scene.objects) {
        if (findObjectState(rawObject.id) == nullptr) {
            ObjectState object;
            object.id = rawObject.id;
            object.dirty = true;
            state_.objects.push_back(object);
        }
    }
    if (state_.selectedObject < 0 ||
        state_.selectedObject >= static_cast<int>(state_.objects.size())) {
        state_.selectedObject = 0;
    }
    loadEvidenceChain();
    if (state_.suspended) {
        const Timestamp now = nowTimestamp();
        state_.suspended = false;
        state_.lastResumeAt = now.utc;
        ++state_.resumeCount;
        appendEvent(
            "session_resumed",
            {
                {"resume_utc", now.utc},
                {"resume_epoch_ms", now.epochMs},
                {"session_revision", state_.revision},
                {"last_suspend_at", state_.lastSuspendAt},
                {"resume_count", state_.resumeCount},
            });
        save();
    }
}

void Workspace::protectRawCopy() {
    std::error_code error;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(rawCopyRoot_)) {
        if (entry.is_regular_file()) {
            std::filesystem::permissions(
                entry.path(),
                std::filesystem::perms::owner_read | std::filesystem::perms::group_read |
                    std::filesystem::perms::others_read,
                std::filesystem::perm_options::replace,
                error);
        }
    }
}

void Workspace::writeJsonAtomic(
    const std::filesystem::path& target,
    const Json& value) const {
    std::filesystem::create_directories(target.parent_path());
    const auto temporary = target.string() + ".tmp";
    writeTextFile(temporary, value.dump(2) + "\n");
    std::filesystem::rename(temporary, target);
}

void Workspace::save() {
    state_.updatedAt = nowTimestamp().utc;
    ++state_.revision;
    writeJsonAtomic(root_ / "session.json", detail::sessionJson(state_));
}

Json Workspace::buildSessionJson() const {
    return detail::sessionJson(state_);
}

void Workspace::loadEvidenceChain() {
    const auto logPath = root_ / "evidence" / "evidence.jsonl";
    if (!std::filesystem::exists(logPath)) {
        evidenceSequence_ = 0;
        lastEvidenceHash_ = "GENESIS";
        return;
    }
    std::string error;
    std::size_t count = 0;
    if (!verifyEvidenceChain(logPath, &count, &error)) {
        throw std::runtime_error("evidence chain is invalid: " + error);
    }
    evidenceSequence_ = count;
    std::ifstream input(logPath);
    std::string line;
    std::string lastLine;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            lastLine = line;
        }
    }
    if (!lastLine.empty()) {
        const Json event = Json::parse(lastLine);
        lastEvidenceHash_ = event.at("event_hash").get<std::string>();
    }
}

bool verifyEvidenceChain(
    const std::filesystem::path& evidenceLog,
    std::size_t* eventCount,
    std::string* error) {
    try {
        std::ifstream input(evidenceLog);
        if (!input) {
            throw std::runtime_error("cannot read evidence log");
        }
        std::size_t expectedSequence = 1;
        std::string previousHash = "GENESIS";
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }
            Json event = Json::parse(line);
            const auto sequence = event.at("seq").get<std::size_t>();
            if (sequence != expectedSequence) {
                throw std::runtime_error(
                    "evidence sequence gap at " + std::to_string(expectedSequence));
            }
            if (event.at("prev_hash").get<std::string>() != previousHash) {
                throw std::runtime_error(
                    "evidence previous hash mismatch at " + std::to_string(sequence));
            }
            const std::string recordedHash = event.at("event_hash").get<std::string>();
            event.erase("event_hash");
            const std::string computedHash = Sha256::hex(canonicalJson(event));
            if (recordedHash != computedHash) {
                throw std::runtime_error(
                    "evidence content hash mismatch at " + std::to_string(sequence));
            }
            previousHash = recordedHash;
            ++expectedSequence;
        }
        if (eventCount != nullptr) {
            *eventCount = expectedSequence - 1;
        }
        return true;
    } catch (const std::exception& ex) {
        if (error != nullptr) {
            *error = ex.what();
        }
        return false;
    }
}

void Workspace::appendEvent(const std::string& type, const Json& details) {
    const Timestamp now = nowTimestamp();
    Json event = {
        {"seq", ++evidenceSequence_},
        {"type", type},
        {"time_utc", now.utc},
        {"time_local", now.local},
        {"epoch_ms", now.epochMs},
        {"details", details},
        {"prev_hash", lastEvidenceHash_},
    };
    const std::string eventHash = Sha256::hex(canonicalJson(event));
    event["event_hash"] = eventHash;
    lastEvidenceHash_ = eventHash;

    std::filesystem::create_directories(root_ / "evidence");
    const auto logPath = root_ / "evidence" / "evidence.jsonl";
    std::ofstream output(logPath, std::ios::app);
    if (!output) {
        throw std::runtime_error("cannot append evidence log");
    }
    output << event.dump() << '\n';
    output.flush();
    if (!output) {
        throw std::runtime_error("failed while appending evidence log");
    }
    writeJsonAtomic(
        root_ / "evidence" / "chain_head.json",
        {
            {"event_count", evidenceSequence_},
            {"last_event_hash", eventHash},
            {"updated_utc", now.utc},
        });
}

void Workspace::suspend(const std::string& reason) {
    const Timestamp now = nowTimestamp();
    state_.suspended = true;
    state_.lastSuspendAt = now.utc;
    save();

    Json runState = Json::object();
    for (const auto& object : state_.objects) {
        runState[object.id] = object.runs.empty() ? "" : object.runs.back().runId;
    }
    appendEvent(
        "session_suspended",
        {
            {"reason", reason},
            {"suspend_utc", now.utc},
            {"suspend_epoch_ms", now.epochMs},
            {"session_revision", state_.revision},
            {"selected_object", state_.selectedObject},
            {"latest_runs", runState},
        });
}

void Workspace::setCurrentOffset(std::size_t anchorIndex, double valueMm) {
    if (anchorIndex >= 3) {
        throw std::runtime_error("anchor index must be 0, 1, or 2");
    }
    if (!std::isfinite(valueMm)) {
        throw std::runtime_error("anchor offset must be finite");
    }
    state_.currentOffsetsMm[anchorIndex] = valueMm;
    for (auto& object : state_.objects) {
        object.dirty = true;
    }
}

void Workspace::setCurrentOffsets(const std::array<double, 3>& valuesMm) {
    for (const double value : valuesMm) {
        if (!std::isfinite(value)) {
            throw std::runtime_error("anchor offset must be finite");
        }
    }
    state_.currentOffsetsMm = valuesMm;
    for (auto& object : state_.objects) {
        object.dirty = true;
    }
}

void Workspace::resetCurrentOffsetsToRaw() {
    setCurrentOffsets(rawOffsets());
}

std::filesystem::path Workspace::path(const std::string& relativePath) const {
    return root_ / relativePath;
}

std::string Workspace::relative(const std::filesystem::path& target) const {
    return std::filesystem::relative(target, root_).generic_string();
}

std::array<Vec3, 3> Workspace::anchorPositions() const {
    return {
        material_.scene.anchors[0].position,
        material_.scene.anchors[1].position,
        material_.scene.anchors[2].position,
    };
}

std::array<Vec3, 3> Workspace::anchorNormals() const {
    return {
        material_.scene.anchors[0].normal,
        material_.scene.anchors[1].normal,
        material_.scene.anchors[2].normal,
    };
}

std::array<double, 3> Workspace::rawOffsets() const {
    return {
        material_.scene.anchors[0].rawOffsetMm,
        material_.scene.anchors[1].rawOffsetMm,
        material_.scene.anchors[2].rawOffsetMm,
    };
}

const RawObject* Workspace::findRawObject(const std::string& objectId) const {
    for (const auto& object : material_.scene.objects) {
        if (object.id == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const ObjectState* Workspace::findObjectState(const std::string& objectId) const {
    for (const auto& object : state_.objects) {
        if (object.id == objectId) {
            return &object;
        }
    }
    return nullptr;
}

ObjectState* Workspace::findObjectState(const std::string& objectId) {
    for (auto& object : state_.objects) {
        if (object.id == objectId) {
            return &object;
        }
    }
    return nullptr;
}

const RunRecord* Workspace::latestRun(const std::string& objectId) const {
    const auto* object = findObjectState(objectId);
    if (object == nullptr || object->runs.empty()) {
        return nullptr;
    }
    return &object->runs.back();
}

const RunRecord* Workspace::findRun(
    const std::string& objectId,
    const std::string& runId) const {
    const auto* object = findObjectState(objectId);
    if (object == nullptr) {
        return nullptr;
    }
    for (const auto& run : object->runs) {
        if (run.runId == runId) {
            return &run;
        }
    }
    return nullptr;
}

Mesh Workspace::loadRunMesh(const std::string& objectId, const std::string& runId) const {
    const auto* run = findRun(objectId, runId);
    if (run == nullptr) {
        throw std::runtime_error("run not found: " + objectId + "/" + runId);
    }
    return loadObj(path(run->outputObjPath));
}

Mesh Workspace::loadLatestRunMesh(const std::string& objectId) const {
    const auto* run = latestRun(objectId);
    if (run == nullptr) {
        throw std::runtime_error("object has not been run: " + objectId);
    }
    return loadObj(path(run->outputObjPath));
}

std::string Workspace::nextRunId(const std::string& objectId) const {
    static std::atomic<std::uint64_t> counter{0};
    static std::random_device random;
    const Timestamp now = nowTimestamp();
    const unsigned suffix = random();
    return "run_" + objectId + "_" + filenameStamp(now) + "_" +
           std::to_string(++counter) + "_" + std::to_string(suffix % 10000);
}

VerificationResult Workspace::verify() {
    VerificationResult result;
    std::string error;
    result.rawMaterialOk = verifyRawMaterial(material_, &error);
    result.rawError = error;
    result.aggregateSha256 = material_.aggregateSha256;
    result.evidenceChainOk = verifyEvidenceChain(
        root_ / "evidence" / "evidence.jsonl",
        &result.evidenceEventCount,
        &result.evidenceError);
    result.sessionLoaded = std::filesystem::exists(root_ / "session.json");
    return result;
}

}  // namespace clipfix
