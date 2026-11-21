#include "core/acceptance.h"

#include "core/obj_io.h"
#include "core/sha256.h"
#include "core/time_util.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace clipfix {
namespace {

using Json = nlohmann::json;

void writeText(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output << text;
    if (!output) {
        throw std::runtime_error("failed while writing " + path.string());
    }
}

std::vector<Json> readEvents(const std::filesystem::path& evidenceLog) {
    std::ifstream input(evidenceLog);
    std::vector<Json> events;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty()) {
            events.push_back(Json::parse(line));
        }
    }
    return events;
}

bool hasEvent(const std::vector<Json>& events, const std::string& type) {
    return std::any_of(events.begin(), events.end(), [&type](const Json& event) {
        return event.value("type", "") == type;
    });
}

bool concreteTimestamp(const std::string& value) {
    static const std::regex pattern(
        R"(^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\.\d{3}(Z|[+-]\d{4})$)");
    return std::regex_match(value, pattern);
}

bool validSha(const std::string& value) {
    static const std::regex pattern(R"(^[0-9a-f]{64}$)");
    return std::regex_match(value, pattern);
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\n") == std::string::npos) {
        return value;
    }
    std::string escaped = "\"";
    for (const char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += '"';
    return escaped;
}

Json offsetsJson(const std::array<double, 3>& values) {
    return Json::array({values[0], values[1], values[2]});
}

Json runSummary(const RunRecord& run) {
    Json samples = Json::array();
    for (const auto& vertex : run.keyVertices) {
        samples.push_back({
            {"vertex_index", vertex.vertexIndex},
            {"before_offset_mm", vertex.beforeOffsetMm},
            {"after_offset_mm", vertex.afterOffsetMm},
            {"delta_mm", vertex.deltaMm},
            {"time_utc", run.finishedUtc},
        });
    }
    return {
        {"run_id", run.runId},
        {"object_id", run.objectId},
        {"started_utc", run.startedUtc},
        {"finished_utc", run.finishedUtc},
        {"started_epoch_ms", run.startedEpochMs},
        {"finished_epoch_ms", run.finishedEpochMs},
        {"raw_offsets_mm", offsetsJson(run.rawOffsetsMm)},
        {"raw_offsets_text", Json::array({
             formatFixed(run.rawOffsetsMm[0]),
             formatFixed(run.rawOffsetsMm[1]),
             formatFixed(run.rawOffsetsMm[2]),
         })},
        {"applied_offsets_mm", offsetsJson(run.appliedOffsetsMm)},
        {"vertex_count", run.stats.vertexCount},
        {"affected_count", run.stats.affectedCount},
        {"max_abs_before_mm", run.stats.maxAbsBeforeMm},
        {"mean_abs_before_mm", run.stats.meanAbsBeforeMm},
        {"max_abs_after_mm", run.stats.maxAbsAfterMm},
        {"mean_abs_after_mm", run.stats.meanAbsAfterMm},
        {"max_delta_mm", run.stats.maxDeltaMm},
        {"output_obj", run.outputObjPath},
        {"output_obj_sha256", run.outputObjSha256},
        {"evidence_csv", run.evidenceCsvPath},
        {"evidence_csv_sha256", run.evidenceCsvSha256},
        {"key_vertices", samples},
    };
}

std::string renderMarkdown(const Json& report) {
    std::ostringstream out;
    out << "# 曲面裁剪平面 - 验收报告\n\n";
    out << "- 结论: **" << report.at("result").get<std::string>() << "**\n";
    out << "- 生成时间: `" << report.at("generated_utc").get<std::string>() << "`\n";
    out << "- 本地时间: `" << report.at("generated_local").get<std::string>() << "`\n";
    out << "- 原始材料聚合 SHA-256: `"
        << report.at("raw_material").at("aggregate_sha256").get<std::string>() << "`\n";
    out << "- 原始材料未修改: **"
        << (report.at("raw_material").at("source_matches_copy").get<bool>() ? "是" : "否")
        << "**\n\n";

    out << "## 三段锚点与原始偏移\n\n";
    out << "| 锚点 | 位置 (mm) | 原始偏移 | 当前应用偏移 | 证据时间 |\n";
    out << "|---|---|---|---|---|\n";
    for (const auto& anchor : report.at("anchors")) {
        out << "| " << anchor.at("name").get<std::string>() << " | ("
            << anchor.at("position_text").get<std::string>() << ") | `"
            << anchor.at("raw_offset_text").get<std::string>() << " mm` | `"
            << anchor.at("applied_offset_text").get<std::string>() << " mm` | `"
            << anchor.at("evidence_time_utc").get<std::string>() << "` |\n";
    }

    out << "\n## 对象运行证据\n\n";
    out << "| 对象 | Run | 开始 (UTC) | 结束 (UTC) | 原始偏移 (mm) | 应用偏移 (mm) "
           "| 受影响点 | 最大修正 | 输出 SHA-256 |\n";
    out << "|---|---|---|---|---|---|---|---|---|\n";
    for (const auto& object : report.at("objects")) {
        for (const auto& run : object.at("runs")) {
            std::string raw;
            for (const auto& value : run.at("raw_offsets_text")) {
                raw += value.get<std::string>() + " ";
            }
            out << "| " << object.at("id").get<std::string>() << " | `"
                << run.at("run_id").get<std::string>() << "` | `"
                << run.at("started_utc").get<std::string>() << "` | `"
                << run.at("finished_utc").get<std::string>() << "` | `" << raw
                << "` | `" << run.at("applied_offsets_mm").dump() << "` | "
                << run.at("affected_count").get<std::size_t>() << " | `"
                << formatMm(run.at("max_delta_mm").get<double>()) << "` | `"
                << run.at("output_obj_sha256").get<std::string>() << "` |\n";
        }
    }

    out << "\n## 对比与导出\n\n";
    out << "| 对象 | 对比基准 | 对比时间 | 变化点 | 最大差异 | 导出文件 |\n";
    out << "|---|---|---|---|---|---|\n";
    for (const auto& object : report.at("objects")) {
        if (!object.contains("compare") || !object.contains("export")) {
            continue;
        }
        const auto& compare = object.at("compare");
        const auto& exportRecord = object.at("export");
        out << "| " << object.at("id").get<std::string>() << " | "
            << compare.at("a_kind").get<std::string>() << " -> "
            << compare.at("b_kind").get<std::string>() << " | `"
            << compare.at("created_utc").get<std::string>() << "` | "
            << compare.at("changed_vertices").get<std::size_t>() << " | `"
            << formatMm(compare.at("max_delta_mm").get<double>()) << "` | `"
            << exportRecord.at("obj_path").get<std::string>() << "` |\n";
    }

    out << "\n## 原始偏移抽样\n\n";
    out << "| 对象 | Run 时间 | 顶点 | 修正前偏移 | 修正后偏移 | 位移 |\n";
    out << "|---|---|---|---|---|---|\n";
    for (const auto& object : report.at("objects")) {
        if (object.at("runs").empty()) {
            continue;
        }
        const auto& run = object.at("runs").back();
        for (const auto& vertex : run.at("key_vertices")) {
            out << "| " << object.at("id").get<std::string>() << " | `"
                << vertex.at("time_utc").get<std::string>() << "` | "
                << vertex.at("vertex_index").get<std::uint32_t>() << " | `"
                << formatMm(vertex.at("before_offset_mm").get<double>()) << "` | `"
                << formatMm(vertex.at("after_offset_mm").get<double>()) << "` | `"
                << formatMm(vertex.at("delta_mm").get<double>()) << "` |\n";
        }
    }

    out << "\n## 检查项\n\n";
    out << "| 检查 | 结果 | 说明 |\n";
    out << "|---|---|---|\n";
    for (const auto& check : report.at("checks")) {
        out << "| " << check.at("name").get<std::string>() << " | "
            << (check.at("pass").get<bool>() ? "PASS" : "FAIL") << " | "
            << check.value("detail", "") << " |\n";
    }
    out << "\n## 证据文件\n\n";
    out << "- 证据链: `" << report.at("evidence_log").get<std::string>() << "`\n";
    out << "- 时间线: `" << report.at("timeline_csv").get<std::string>() << "`\n";
    out << "- 会话: `" << report.at("session_file").get<std::string>() << "`\n";
    return out.str();
}

}  // namespace

AcceptanceReport runAcceptance(
    Workspace& workspace,
    const AcceptanceOptions& options) {
    AcceptanceReport report;
    const Timestamp generated = nowTimestamp();
    const int requiredRuns = std::max(1, options.requiredRunsPerObject);
    report.data = {
        {"format", "clipfix-acceptance-v1"},
        {"generated_utc", generated.utc},
        {"generated_local", generated.local},
        {"generated_epoch_ms", generated.epochMs},
        {"workspace", workspace.root().string()},
    };

    std::string fillError;
    if (options.fillMissingEvidence) {
        try {
            for (const auto& rawObject : workspace.material().scene.objects) {
                const auto* object = workspace.findObjectState(rawObject.id);
                if (object == nullptr || object->runs.empty()) {
                    workspace.runObject(rawObject.id);
                }
            }
            workspace.suspend("acceptance_mid_exit_probe");
            WorkspaceOptions reopenOptions{
                workspace.root(),
                workspace.sourceRoot(),
            };
            workspace = Workspace::open(reopenOptions);
            for (const auto& rawObject : workspace.material().scene.objects) {
                workspace.runObject(rawObject.id);
                const auto* object = workspace.findObjectState(rawObject.id);
                while (object != nullptr &&
                       static_cast<int>(object->runs.size()) < requiredRuns) {
                    workspace.runObject(rawObject.id);
                    object = workspace.findObjectState(rawObject.id);
                }
                workspace.compareObject(rawObject.id, false);
                workspace.exportObject(rawObject.id);
            }
        } catch (const std::exception& ex) {
            fillError = ex.what();
        }
    }

    Json checks = Json::array();
    std::vector<std::string> failures;
    auto addCheck = [&checks, &failures](
                        const std::string& name,
                        bool pass,
                        const std::string& detail) {
        checks.push_back({
            {"name", name},
            {"pass", pass},
            {"detail", detail},
        });
        if (!pass) {
            failures.push_back(name + ": " + detail);
        }
    };
    addCheck(
        "evidence_fill_completed",
        fillError.empty(),
        fillError.empty()
            ? (options.fillMissingEvidence ? "runs, compare and export completed"
                                           : "not requested")
            : fillError);

    RawMaterialBundle sourceBundle;
    bool sourceLoaded = false;
    std::string sourceError;
    try {
        sourceBundle = loadRawMaterial(workspace.sourceRoot());
        sourceLoaded = verifyRawMaterial(sourceBundle, &sourceError);
    } catch (const std::exception& ex) {
        sourceError = ex.what();
    }
    RawMaterialBundle copyBundle = workspace.material();
    std::string copyError;
    const bool copyLoaded = verifyRawMaterial(copyBundle, &copyError);
    const bool sourceMatchesCopy =
        sourceLoaded && copyLoaded &&
        sourceBundle.aggregateSha256 == copyBundle.aggregateSha256;
    addCheck(
        "raw_material_unmodified",
        sourceMatchesCopy,
        sourceMatchesCopy ? "source and workspace copy match"
                          : ("source=" + sourceError + " copy=" + copyError));
    addCheck(
        "three_anchors_present",
        workspace.material().scene.anchors.size() == 3,
        "anchor_count=" + std::to_string(workspace.material().scene.anchors.size()));
    addCheck(
        "three_render_objects_present",
        workspace.material().scene.objects.size() == 3,
        "object_count=" + std::to_string(workspace.material().scene.objects.size()));

    Json rawFiles = Json::array();
    for (const auto& file : copyBundle.files) {
        const auto fullPath = workspace.rawCopyRoot() / file.relativePath;
        bool fileOk = std::filesystem::is_regular_file(fullPath);
        if (fileOk) {
            fileOk = Sha256::fileHex(fullPath) == file.sha256;
        }
        rawFiles.push_back({
            {"path", file.relativePath},
            {"sha256", file.sha256},
            {"bytes", file.bytes},
            {"verified", fileOk},
        });
        if (!fileOk) {
            addCheck(
                "raw_file_" + file.relativePath,
                false,
                "workspace copy checksum mismatch");
        }
    }

    const auto rawOffsets = workspace.rawOffsets();
    const auto currentOffsets = workspace.state().currentOffsetsMm;
    const auto* firstObject = workspace.material().scene.objects.empty()
        ? nullptr
        : &workspace.material().scene.objects.front();
    std::string anchorEvidenceTime = generated.utc;
    if (firstObject != nullptr) {
        const auto* run = workspace.latestRun(firstObject->id);
        if (run != nullptr) {
            anchorEvidenceTime = run->finishedUtc;
        }
    }

    Json anchors = Json::array();
    std::array<std::string, 3> expectedRawText{
        formatFixed(rawOffsets[0]),
        formatFixed(rawOffsets[1]),
        formatFixed(rawOffsets[2]),
    };
    for (std::size_t i = 0; i < 3 && i < copyBundle.scene.anchors.size(); ++i) {
        if (!copyBundle.scene.anchors[i].rawOffsetText.empty()) {
            expectedRawText[i] = copyBundle.scene.anchors[i].rawOffsetText;
        }
    }
    for (std::size_t i = 0; i < copyBundle.scene.anchors.size(); ++i) {
        const auto& anchor = copyBundle.scene.anchors[i];
        const std::string positionText =
            formatFixed(anchor.position.x) + ", " + formatFixed(anchor.position.y) +
            ", " + formatFixed(anchor.position.z);
        anchors.push_back({
            {"id", anchor.id},
            {"name", anchor.name},
            {"position_mm", Json::array({anchor.position.x, anchor.position.y, anchor.position.z})},
            {"position_text", positionText},
            {"normal", Json::array({anchor.normal.x, anchor.normal.y, anchor.normal.z})},
            {"raw_offset_mm", rawOffsets[i]},
            {"raw_offset_text", expectedRawText[i]},
            {"applied_offset_mm", currentOffsets[i]},
            {"applied_offset_text", formatFixed(currentOffsets[i])},
            {"evidence_time_utc", anchorEvidenceTime},
        });
    }
    addCheck(
        "raw_offsets_exact",
        std::abs(std::stod(expectedRawText[0]) - rawOffsets[0]) < 1.0e-9 &&
            std::abs(std::stod(expectedRawText[1]) - rawOffsets[1]) < 1.0e-9 &&
            std::abs(std::stod(expectedRawText[2]) - rawOffsets[2]) < 1.0e-9,
        "A=" + expectedRawText[0] + " B=" + expectedRawText[1] +
            " C=" + expectedRawText[2]);

    Json objects = Json::array();
    for (const auto& rawObject : workspace.material().scene.objects) {
        const auto* object = workspace.findObjectState(rawObject.id);
        Json objectValue = {
            {"id", rawObject.id},
            {"name", rawObject.name},
            {"obj_path", rawObject.relativeObjPath},
            {"source_sha256", rawObject.rawFileSha256},
            {"run_count", object == nullptr ? 0 : object->runs.size()},
        };
        Json objectChecks = Json::array();
        auto objectCheck = [&objectChecks](
                               const std::string& name,
                               bool pass,
                               const std::string& detail) {
            objectChecks.push_back({{"name", name}, {"pass", pass}, {"detail", detail}});
            return pass;
        };
        bool objectPass = true;

        if (object != nullptr) {
            const int runCount = static_cast<int>(object->runs.size());
            objectPass &= objectCheck(
                "at_least_two_independent_runs",
                runCount >= requiredRuns,
                "run_count=" + std::to_string(runCount) +
                    " required=" + std::to_string(requiredRuns));

            Json runs = Json::array();
            const std::size_t firstRun =
                object->runs.size() > 2 ? object->runs.size() - 2 : 0;
            std::vector<std::string> seenRunIds;
            for (std::size_t i = firstRun; i < object->runs.size(); ++i) {
                const auto& run = object->runs[i];
                runs.push_back(runSummary(run));
                const bool uniqueId =
                    std::find(seenRunIds.begin(), seenRunIds.end(), run.runId) ==
                    seenRunIds.end();
                seenRunIds.push_back(run.runId);
                const bool timeOk =
                    concreteTimestamp(run.startedUtc) &&
                    concreteTimestamp(run.finishedUtc) &&
                    run.finishedEpochMs >= run.startedEpochMs;
                const bool timesDiffer = run.startedEpochMs != run.finishedEpochMs;
                const bool offsetsOk =
                    std::abs(run.rawOffsetsMm[0] - rawOffsets[0]) < 1.0e-12 &&
                    std::abs(run.rawOffsetsMm[1] - rawOffsets[1]) < 1.0e-12 &&
                    std::abs(run.rawOffsetsMm[2] - rawOffsets[2]) < 1.0e-12;
                bool objOk = false;
                bool csvOk = false;
                try {
                    objOk = std::filesystem::is_regular_file(
                                workspace.path(run.outputObjPath)) &&
                        Sha256::fileHex(workspace.path(run.outputObjPath)) ==
                            run.outputObjSha256;
                    csvOk = std::filesystem::is_regular_file(
                                workspace.path(run.evidenceCsvPath)) &&
                        Sha256::fileHex(workspace.path(run.evidenceCsvPath)) ==
                            run.evidenceCsvSha256;
                } catch (const std::exception&) {
                    objOk = false;
                    csvOk = false;
                }
                const bool evidenceOk =
                    run.stats.affectedCount > 0 && !run.keyVertices.empty();
                objectPass &= objectCheck(
                    "run_" + run.runId + "_evidence",
                    uniqueId && timeOk && timesDiffer && offsetsOk && objOk && csvOk &&
                        evidenceOk && validSha(run.outputObjSha256) &&
                        validSha(run.evidenceCsvSha256),
                    "unique=" + std::to_string(uniqueId) +
                        " timestamps=" + std::to_string(timeOk) +
                        " nonzero_duration=" + std::to_string(timesDiffer) +
                        " raw_offsets=" + std::to_string(offsetsOk) +
                        " obj=" + std::to_string(objOk) +
                        " csv=" + std::to_string(csvOk) +
                        " affected=" + std::to_string(run.stats.affectedCount));
            }
            objectValue["runs"] = runs;

            if (object->hasCompare) {
                objectValue["compare"] = {
                    {"created_utc", object->lastCompare.createdUtc},
                    {"a_kind", object->lastCompare.aKind},
                    {"a_ref", object->lastCompare.aRef},
                    {"b_kind", object->lastCompare.bKind},
                    {"b_ref", object->lastCompare.bRef},
                    {"compared_vertices", object->lastCompare.comparedVertices},
                    {"changed_vertices", object->lastCompare.changedVertices},
                    {"max_delta_mm", object->lastCompare.maxDeltaMm},
                    {"mean_delta_mm", object->lastCompare.meanDeltaMm},
                    {"report_path", object->lastCompare.reportPath},
                };
                bool reportExists = false;
                try {
                    reportExists = std::filesystem::is_regular_file(
                        workspace.path(object->lastCompare.reportPath));
                } catch (const std::exception&) {
                    reportExists = false;
                }
                objectPass &= objectCheck(
                    "compare_report_present",
                    reportExists && concreteTimestamp(object->lastCompare.createdUtc) &&
                        object->lastCompare.changedVertices > 0,
                    "report=" + std::to_string(reportExists) +
                        " changed=" + std::to_string(object->lastCompare.changedVertices));
            } else {
                objectPass &= objectCheck(
                    "compare_report_present",
                    false,
                    "missing comparison");
            }

            if (object->hasExport) {
                bool exportOk = false;
                try {
                    exportOk = std::filesystem::is_regular_file(
                                   workspace.path(object->lastExport.objPath)) &&
                        Sha256::fileHex(workspace.path(object->lastExport.objPath)) ==
                            object->lastExport.objSha256;
                } catch (const std::exception&) {
                    exportOk = false;
                }
                objectValue["export"] = {
                    {"exported_utc", object->lastExport.exportedUtc},
                    {"run_id", object->lastExport.runId},
                    {"obj_path", object->lastExport.objPath},
                    {"obj_sha256", object->lastExport.objSha256},
                    {"report_path", object->lastExport.reportPath},
                };
                objectPass &= objectCheck(
                    "export_present",
                    exportOk && concreteTimestamp(object->lastExport.exportedUtc),
                    "obj_sha256=" + object->lastExport.objSha256);
            } else {
                objectPass &= objectCheck("export_present", false, "missing export");
            }
        } else {
            objectPass = false;
            objectCheck("object_state_present", false, "missing session state");
        }
        objectValue["checks"] = objectChecks;
        objectValue["pass"] = objectPass;
        objects.push_back(objectValue);
        addCheck(
            "object_" + rawObject.id + "_rerun_compare_export",
            objectPass,
            objectPass ? "all object checks passed" : "see object checks");
    }

    const VerificationResult verification = workspace.verify();
    addCheck(
        "evidence_chain_valid",
        verification.evidenceChainOk,
        verification.evidenceError.empty()
            ? ("events=" + std::to_string(verification.evidenceEventCount))
            : verification.evidenceError);

    const auto evidenceLog = workspace.root() / "evidence" / "evidence.jsonl";
    std::vector<Json> events;
    try {
        events = readEvents(evidenceLog);
    } catch (const std::exception& ex) {
        addCheck("evidence_events_readable", false, ex.what());
    }
    const bool hasSuspend = hasEvent(events, "session_suspended");
    const bool hasResume = hasEvent(events, "session_resumed");
    addCheck(
        "mid_exit_and_reentry_evidenced",
        hasSuspend && hasResume,
        "suspend_event=" + std::to_string(hasSuspend) +
            " resume_event=" + std::to_string(hasResume) +
            " resume_count=" + std::to_string(workspace.state().resumeCount));

    const auto reportDir = std::filesystem::absolute(options.reportDirectory);
    const auto timelinePath = reportDir / "evidence_timeline.csv";
    std::ostringstream timeline;
    timeline << "time_utc,type,object_id,run_id,raw_offsets_mm,applied_offsets_mm,"
                "artifact,sha256\n";
    for (const auto& event : events) {
        const auto& details = event.value("details", Json::object());
        const std::string rawOffsetsText =
            details.contains("raw_offsets_mm") ? details.at("raw_offsets_mm").dump() : "";
        const std::string appliedOffsetsText =
            details.contains("applied_offsets_mm")
            ? details.at("applied_offsets_mm").dump()
            : "";
        std::string artifact;
        for (const char* key : {"obj_path", "report_path", "output_obj", "evidence_csv"}) {
            if (details.contains(key)) {
                artifact = details.at(key).get<std::string>();
                break;
            }
        }
        std::string sha;
        for (const char* key : {"obj_sha256", "output_obj_sha256", "evidence_csv_sha256"}) {
            if (details.contains(key)) {
                sha = details.at(key).get<std::string>();
                break;
            }
        }
        timeline << csvEscape(event.value("time_utc", "")) << ','
                 << csvEscape(event.value("type", "")) << ','
                 << csvEscape(details.value("object_id", "")) << ','
                 << csvEscape(details.value("run_id", "")) << ','
                 << csvEscape(rawOffsetsText) << ','
                 << csvEscape(appliedOffsetsText) << ','
                 << csvEscape(artifact) << ',' << csvEscape(sha) << '\n';
    }

    const bool pass = failures.empty();
    report.pass = pass;
    report.failures = failures;
    report.data["checks"] = checks;
    report.data["failures"] = failures;
    report.data["result"] = pass ? "PASS" : "FAIL";
    report.data["raw_material"] = {
        {"source_root", workspace.sourceRoot().string()},
        {"copy_root", workspace.rawCopyRoot().string()},
        {"source_verified", sourceLoaded},
        {"copy_verified", copyLoaded},
        {"source_error", sourceError},
        {"copy_error", copyError},
        {"source_matches_copy", sourceMatchesCopy},
        {"aggregate_sha256", copyBundle.aggregateSha256},
        {"manifest_sha256", copyBundle.manifestSha256},
        {"files", rawFiles},
    };
    report.data["anchors"] = anchors;
    report.data["objects"] = objects;
    report.data["session"] = {
        {"revision", workspace.state().revision},
        {"suspended", workspace.state().suspended},
        {"selected_object", workspace.state().selectedObject},
        {"last_suspend_at", workspace.state().lastSuspendAt},
        {"last_resume_at", workspace.state().lastResumeAt},
        {"resume_count", workspace.state().resumeCount},
    };
    report.data["evidence_event_count"] = events.size();
    report.data["evidence_log"] = evidenceLog.string();
    report.data["timeline_csv"] = timelinePath.string();
    report.data["session_file"] = (workspace.root() / "session.json").string();

    std::filesystem::create_directories(reportDir);
    report.jsonPath = reportDir / "acceptance_report.json";
    report.markdownPath = reportDir / "acceptance_report.md";
    report.timelineCsvPath = timelinePath;
    writeText(report.jsonPath, report.data.dump(2) + "\n");
    writeText(report.markdownPath, renderMarkdown(report.data));
    writeText(report.timelineCsvPath, timeline.str());

    workspace.appendEvent(
        "acceptance_written",
        {
            {"result", report.data.at("result")},
            {"report_json", report.jsonPath.string()},
            {"report_markdown", report.markdownPath.string()},
            {"timeline_csv", report.timelineCsvPath.string()},
            {"raw_aggregate_sha256", copyBundle.aggregateSha256},
            {"failure_count", failures.size()},
        });
    return report;
}

}  // namespace clipfix
