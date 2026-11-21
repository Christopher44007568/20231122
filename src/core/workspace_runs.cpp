#include "core/workspace.h"

#include "core/clip_surface.h"
#include "core/obj_io.h"
#include "core/sha256.h"
#include "core/time_util.h"
#include "core/workspace_json.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <stdexcept>

namespace clipfix {
namespace {

using Json = nlohmann::json;

std::vector<std::string> runHeader(const RunRecord& run) {
    return {
        "run_id=" + run.runId,
        "object_id=" + run.objectId,
        "started_utc=" + run.startedUtc,
        "finished_utc=" + run.finishedUtc,
        "raw_offsets_mm=" + formatFixed(run.rawOffsetsMm[0]) + "," +
            formatFixed(run.rawOffsetsMm[1]) + "," + formatFixed(run.rawOffsetsMm[2]),
        "applied_offsets_mm=" + formatFixed(run.appliedOffsetsMm[0]) + "," +
            formatFixed(run.appliedOffsetsMm[1]) + "," +
            formatFixed(run.appliedOffsetsMm[2]),
        "band_mm=" + formatFixed(run.bandMm),
        "width_mm=" + formatFixed(run.widthMm),
        "source_sha256=" + run.sourceSha256,
    };
}

void writeEvidenceCsv(
    const std::filesystem::path& target,
    const RunRecord& run,
    const std::vector<VertexEvidence>& affected) {
    std::ofstream output(target);
    if (!output) {
        throw std::runtime_error("cannot write evidence CSV: " + target.string());
    }
    output << "run_finished_utc,run_id,object_id,vertex_index,curve_t,lateral_mm,"
              "before_offset_mm,after_offset_mm,delta_mm,raw_a_mm,raw_b_mm,raw_c_mm,"
              "applied_a_mm,applied_b_mm,applied_c_mm\n";
    output << std::fixed << std::setprecision(9);
    for (const auto& vertex : affected) {
        output << run.finishedUtc << ',' << run.runId << ',' << run.objectId << ','
               << vertex.vertexIndex << ',' << vertex.curveT << ',' << vertex.lateralMm
               << ',' << vertex.beforeOffsetMm << ',' << vertex.afterOffsetMm << ','
               << vertex.deltaMm << ',' << run.rawOffsetsMm[0] << ','
               << run.rawOffsetsMm[1] << ',' << run.rawOffsetsMm[2] << ','
               << run.appliedOffsetsMm[0] << ',' << run.appliedOffsetsMm[1] << ','
               << run.appliedOffsetsMm[2] << '\n';
    }
    if (!output) {
        throw std::runtime_error("failed while writing evidence CSV");
    }
}

Json keyVertexJson(const VertexEvidence& vertex) {
    return {
        {"vertex_index", vertex.vertexIndex},
        {"curve_t", vertex.curveT},
        {"lateral_mm", vertex.lateralMm},
        {"before_offset_mm", vertex.beforeOffsetMm},
        {"after_offset_mm", vertex.afterOffsetMm},
        {"delta_mm", vertex.deltaMm},
    };
}

}  // namespace

RunRecord Workspace::runObject(const std::string& objectId) {
    const RawObject* rawObject = findRawObject(objectId);
    ObjectState* objectState = findObjectState(objectId);
    if (rawObject == nullptr || objectState == nullptr) {
        throw std::runtime_error("unknown render object: " + objectId);
    }

    std::string rawError;
    if (!verifyRawMaterial(material_, &rawError)) {
        throw std::runtime_error("raw material changed; run refused: " + rawError);
    }

    const Timestamp started = nowTimestamp();
    RunRecord run;
    run.runId = nextRunId(objectId);
    run.objectId = objectId;
    run.startedUtc = started.utc;
    run.startedEpochMs = started.epochMs;
    run.rawOffsetsMm = rawOffsets();
    run.appliedOffsetsMm = state_.currentOffsetsMm;
    run.sourceSha256 = rawObject->rawFileSha256;

    appendEvent(
        "object_run_started",
        {
            {"run_id", run.runId},
            {"object_id", objectId},
            {"started_utc", run.startedUtc},
            {"started_epoch_ms", run.startedEpochMs},
            {"raw_offsets_mm", Json(run.rawOffsetsMm)},
            {"applied_offsets_mm", Json(run.appliedOffsetsMm)},
            {"source_obj_sha256", run.sourceSha256},
        });

    ClipSettings settings;
    const ClipSurface surface(
        anchorPositions(),
        anchorNormals(),
        run.appliedOffsetsMm,
        settings);
    ClipRunResult clipped = surface.correct(rawObject->mesh);
    const Timestamp finished = nowTimestamp();
    run.finishedUtc = finished.utc;
    run.finishedEpochMs = finished.epochMs;
    run.bandMm = settings.bandMm;
    run.widthMm = settings.widthMm;
    run.stats = clipped.stats;
    run.keyVertices = clipped.keyVertices;

    const auto objectRunDir = root_ / "runs" / objectId;
    std::filesystem::create_directories(objectRunDir);
    const auto objPath = objectRunDir / (run.runId + ".obj");
    const auto csvPath = objectRunDir / (run.runId + ".offsets.csv");
    const auto metaPath = objectRunDir / (run.runId + ".json");

    writeObj(objPath, clipped.corrected, runHeader(run));
    writeEvidenceCsv(csvPath, run, clipped.affected);
    run.outputObjPath = relative(objPath);
    run.evidenceCsvPath = relative(csvPath);
    run.outputObjSha256 = Sha256::fileHex(objPath);
    run.evidenceCsvSha256 = Sha256::fileHex(csvPath);

    const Json meta = detail::runJson(run);
    writeJsonAtomic(metaPath, meta);

    Json keyVertices = Json::array();
    for (const auto& vertex : run.keyVertices) {
        keyVertices.push_back(keyVertexJson(vertex));
    }
    appendEvent(
        "object_run_finished",
        {
            {"run_id", run.runId},
            {"object_id", objectId},
            {"started_utc", run.startedUtc},
            {"finished_utc", run.finishedUtc},
            {"finished_epoch_ms", run.finishedEpochMs},
            {"raw_offsets_mm", Json(run.rawOffsetsMm)},
            {"applied_offsets_mm", Json(run.appliedOffsetsMm)},
            {"band_mm", run.bandMm},
            {"width_mm", run.widthMm},
            {"vertex_count", run.stats.vertexCount},
            {"affected_count", run.stats.affectedCount},
            {"max_abs_before_mm", run.stats.maxAbsBeforeMm},
            {"max_abs_after_mm", run.stats.maxAbsAfterMm},
            {"max_delta_mm", run.stats.maxDeltaMm},
            {"source_obj_sha256", run.sourceSha256},
            {"output_obj_sha256", run.outputObjSha256},
            {"evidence_csv_sha256", run.evidenceCsvSha256},
            {"output_obj", run.outputObjPath},
            {"evidence_csv", run.evidenceCsvPath},
            {"key_vertices", keyVertices},
        });

    objectState->runs.push_back(run);
    if (objectState->runs.size() > 20) {
        objectState->runs.erase(objectState->runs.begin());
    }
    objectState->dirty = false;
    objectState->compareReady = false;
    save();
    return run;
}

std::vector<RunRecord> Workspace::runAllObjects() {
    std::vector<RunRecord> results;
    results.reserve(material_.scene.objects.size());
    for (const auto& object : material_.scene.objects) {
        results.push_back(runObject(object.id));
    }
    return results;
}

CompareResult Workspace::compareObject(
    const std::string& objectId,
    bool previousRun) {
    const RawObject* rawObject = findRawObject(objectId);
    ObjectState* objectState = findObjectState(objectId);
    if (rawObject == nullptr || objectState == nullptr) {
        throw std::runtime_error("unknown render object: " + objectId);
    }
    if (objectState->runs.empty()) {
        throw std::runtime_error("object must be run before comparison: " + objectId);
    }

    const RunRecord& latest = objectState->runs.back();
    const RunRecord* previous = nullptr;
    if (previousRun) {
        if (objectState->runs.size() < 2) {
            throw std::runtime_error("two runs are required for previous-run comparison");
        }
        previous = &objectState->runs[objectState->runs.size() - 2];
    }

    const Mesh a = previousRun ? loadRunMesh(objectId, previous->runId) : rawObject->mesh;
    const Mesh b = loadRunMesh(objectId, latest.runId);
    if (a.positions.size() != b.positions.size()) {
        throw std::runtime_error("cannot compare meshes with different vertex counts");
    }

    CompareResult result;
    result.objectId = objectId;
    result.aKind = previousRun ? "run" : "original";
    result.bKind = "run";
    result.aRef = previousRun ? previous->runId : "original_material";
    result.bRef = latest.runId;
    result.comparedVertices = a.positions.size();

    const Timestamp created = nowTimestamp();
    result.createdUtc = created.utc;
    std::vector<Json> samples;
    double sum = 0.0;
    for (std::size_t i = 0; i < a.positions.size(); ++i) {
        const double delta = (b.positions[i] - a.positions[i]).length();
        sum += delta;
        result.maxDeltaMm = std::max(result.maxDeltaMm, delta);
        if (delta > 1.0e-9) {
            ++result.changedVertices;
            if (samples.size() < 24) {
                samples.push_back({
                    {"vertex_index", i},
                    {"delta_mm", delta},
                });
            }
        }
    }
    result.meanDeltaMm = result.comparedVertices == 0
        ? 0.0
        : sum / static_cast<double>(result.comparedVertices);

    const auto reportPath = root_ / "comparisons" /
        (objectId + "_compare_" + filenameStamp(created) + ".json");
    result.reportPath = relative(reportPath);
    writeJsonAtomic(
        reportPath,
        {
            {"format", "clipfix-comparison-v1"},
            {"created_utc", created.utc},
            {"created_local", created.local},
            {"created_epoch_ms", created.epochMs},
            {"object_id", objectId},
            {"a_kind", result.aKind},
            {"a_ref", result.aRef},
            {"a_finished_utc", previousRun ? previous->finishedUtc : rawObject->relativeObjPath},
            {"b_kind", result.bKind},
            {"b_ref", result.bRef},
            {"b_finished_utc", latest.finishedUtc},
            {"raw_offsets_mm", Json(latest.rawOffsetsMm)},
            {"applied_offsets_mm", Json(latest.appliedOffsetsMm)},
            {"compared_vertices", result.comparedVertices},
            {"changed_vertices", result.changedVertices},
            {"max_delta_mm", result.maxDeltaMm},
            {"mean_delta_mm", result.meanDeltaMm},
            {"largest_vertex_deltas", samples},
        });

    appendEvent(
        "object_compared",
        {
            {"object_id", objectId},
            {"created_utc", created.utc},
            {"created_epoch_ms", created.epochMs},
            {"a_kind", result.aKind},
            {"a_ref", result.aRef},
            {"b_kind", result.bKind},
            {"b_ref", result.bRef},
            {"raw_offsets_mm", Json(latest.rawOffsetsMm)},
            {"applied_offsets_mm", Json(latest.appliedOffsetsMm)},
            {"changed_vertices", result.changedVertices},
            {"max_delta_mm", result.maxDeltaMm},
            {"mean_delta_mm", result.meanDeltaMm},
            {"report_path", result.reportPath},
        });

    objectState->lastCompare = result;
    objectState->hasCompare = true;
    objectState->compareReady = true;
    save();
    return result;
}

ExportRecord Workspace::exportObject(const std::string& objectId) {
    const RawObject* rawObject = findRawObject(objectId);
    ObjectState* objectState = findObjectState(objectId);
    if (rawObject == nullptr || objectState == nullptr) {
        throw std::runtime_error("unknown render object: " + objectId);
    }
    if (objectState->runs.empty()) {
        throw std::runtime_error("object must be run before export: " + objectId);
    }
    const RunRecord& run = objectState->runs.back();
    const Mesh mesh = loadRunMesh(objectId, run.runId);
    const Timestamp exported = nowTimestamp();

    ExportRecord record;
    record.objectId = objectId;
    record.runId = run.runId;
    record.exportedUtc = exported.utc;
    const auto exportDir = root_ / "exports";
    std::filesystem::create_directories(exportDir);
    const auto objPath = exportDir / (objectId + "_" + run.runId + ".obj");
    const auto reportPath = exportDir / (objectId + "_" + run.runId + ".json");

    std::vector<std::string> header = runHeader(run);
    header.push_back("exported_utc=" + exported.utc);
    header.push_back("source_material_sha256=" + material_.aggregateSha256);
    writeObj(objPath, mesh, header);
    record.objPath = relative(objPath);
    record.reportPath = relative(reportPath);
    record.objSha256 = Sha256::fileHex(objPath);

    Json compareValue = Json::object();
    if (objectState->hasCompare) {
        compareValue = detail::compareJson(objectState->lastCompare);
    }
    writeJsonAtomic(
        reportPath,
        {
            {"format", "clipfix-export-v1"},
            {"exported_utc", exported.utc},
            {"exported_local", exported.local},
            {"exported_epoch_ms", exported.epochMs},
            {"object_id", objectId},
            {"object_name", rawObject->name},
            {"run", detail::runJson(run)},
            {"raw_material_aggregate_sha256", material_.aggregateSha256},
            {"raw_offsets_mm", Json(run.rawOffsetsMm)},
            {"applied_offsets_mm", Json(run.appliedOffsetsMm)},
            {"comparison", compareValue},
            {"obj_path", record.objPath},
            {"obj_sha256", record.objSha256},
        });

    appendEvent(
        "object_exported",
        {
            {"object_id", objectId},
            {"run_id", run.runId},
            {"exported_utc", exported.utc},
            {"exported_epoch_ms", exported.epochMs},
            {"raw_offsets_mm", Json(run.rawOffsetsMm)},
            {"applied_offsets_mm", Json(run.appliedOffsetsMm)},
            {"obj_path", record.objPath},
            {"obj_sha256", record.objSha256},
            {"report_path", record.reportPath},
        });

    objectState->lastExport = record;
    objectState->hasExport = true;
    save();
    return record;
}

}  // namespace clipfix
