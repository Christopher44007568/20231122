#include "experiment.hpp"

#include "evidence.hpp"
#include "format.hpp"
#include "journal.hpp"
#include "renderer.hpp"
#include "selection.hpp"
#include "vk_context.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <numeric>
#include <random>
#include <set>
#include <sstream>

namespace inst {
namespace {

struct LoadedBatch {
    std::vector<Submission> submissions;
    std::vector<InputObject> objects;
    std::vector<std::string> evidenceFiles;
    uint32_t loadedObjects = 0;
    uint32_t skippedObjects = 0;
};

std::string objectRecordHash(const InputObject& object) {
    std::string payload;
    for (const Submission& record : object.records) {
        payload += submissionSignature(record);
        payload.push_back('\n');
    }
    return hex64(fnv1a64(payload));
}

std::string submissionSetHash(const std::vector<Submission>& submissions) {
    std::string payload;
    for (const Submission& record : submissions) {
        payload += submissionSignature(record);
        payload.push_back('\n');
    }
    return hex64(fnv1a64(payload));
}

std::string ingestState(const LoadedBatch& batch) {
    return joinJson({
        field("loaded_objects", batch.loadedObjects),
        field("skipped_objects", batch.skippedObjects),
        field("submissions", static_cast<uint64_t>(batch.submissions.size())),
        field("submission_hash", jsonString(submissionSetHash(batch.submissions))),
    });
}

std::string selectionState(const SelectionResult& result) {
    return joinJson({
        field("policy", jsonString(result.policyName)),
        field("order", jsonString(result.orderName)),
        field("selected", static_cast<uint64_t>(result.selected.size())),
        field("selection_hash", jsonString(result.selectionHash)),
        field("draw_order_hash", jsonString(result.drawOrderHash)),
        field("duplicates", result.duplicateSuppressed),
    });
}

std::vector<Submission> makeOrder(const std::vector<Submission>& canonical,
                                  const std::string& order,
                                  uint32_t seed) {
    std::vector<Submission> submissions = canonical;
    if (order == "reversed") {
        std::reverse(submissions.begin(), submissions.end());
    } else if (order == "shuffled") {
        std::mt19937 generator(seed);
        std::shuffle(submissions.begin(), submissions.end(), generator);
    }
    return submissions;
}

std::string drawArrayJson(const std::vector<DrawRecord>& draws) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < draws.size(); ++i) {
        if (i) out << ',';
        const DrawRecord& draw = draws[i];
        out << joinJson({
            field("index", draw.index),
            field("model", draw.model),
            field("model_name", jsonString(draw.modelName)),
            field("index_count", draw.indexCount),
            field("instance_count", draw.instanceCount),
            field("first_index", draw.firstIndex),
            field("first_instance", draw.firstInstance),
            field("vertex_buffer", jsonString(draw.vertexBuffer)),
            field("index_buffer", jsonString(draw.indexBuffer)),
            field("instance_buffer", jsonString(draw.instanceBuffer)),
        });
    }
    out << ']';
    return out.str();
}

std::string transitionArrayJson(const std::vector<TransitionRecord>& records) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < records.size(); ++i) {
        if (i) out << ',';
        const TransitionRecord& record = records[i];
        out << joinJson({
            field("resource", jsonString(record.resource)),
            field("old_layout", jsonString(record.oldLayout)),
            field("new_layout", jsonString(record.newLayout)),
            field("stage", jsonString(record.stage)),
            field("access", jsonString(record.access)),
        });
    }
    out << ']';
    return out.str();
}

std::string selectedArrayJson(const std::vector<SelectedInstance>& selected) {
    std::ostringstream out;
    out << '[';
    for (size_t i = 0; i < selected.size(); ++i) {
        if (i) out << ',';
        const SelectedInstance& item = selected[i];
        out << joinJson({
            field("object_id", item.objectId),
            field("revision", item.revision),
            field("model", item.model),
            field("model_name", jsonString(modelName(item.model))),
            field("x", jsonNumber(item.x)),
            field("y", jsonNumber(item.y)),
            field("scale", jsonNumber(item.scale)),
            field("source", jsonString(item.sourcePath)),
        });
    }
    out << ']';
    return out.str();
}

std::string snapshotJson(const RenderSnapshot& snapshot) {
    const uint64_t instanceCount = std::accumulate(
        snapshot.draws.begin(), snapshot.draws.end(), uint64_t{0},
        [](uint64_t total, const DrawRecord& draw) {
            return total + draw.instanceCount;
        });
    std::ostringstream out;
    out << joinJson({
        field("label", jsonString(snapshot.label)),
        field("policy", jsonString(snapshot.policy)),
        field("order", jsonString(snapshot.order)),
        field("selection_hash", jsonString(snapshot.selectionHash)),
        field("draw_order_hash", jsonString(snapshot.drawOrderHash)),
        field("device", jsonString(snapshot.deviceName)),
        field("width", snapshot.width),
        field("height", snapshot.height),
        field("image_hash", jsonString(snapshot.imageHash)),
        field("image", jsonString(snapshot.imagePath)),
        field("draw_calls", static_cast<uint64_t>(snapshot.draws.size())),
        field("instances", instanceCount),
        field("selected", selectedArrayJson(snapshot.instances)),
        field("draws", drawArrayJson(snapshot.draws)),
        field("transitions", transitionArrayJson(snapshot.transitions)),
    });
    return out.str();
}

void copyEvidenceInputs(const fs::path& sourceDir,
                        const fs::path& targetDir,
                        std::vector<std::string>& names) {
    std::error_code error;
    fs::remove_all(targetDir, error);
    fs::create_directories(targetDir, error);
    for (const auto& entry : fs::directory_iterator(sourceDir)) {
        if (!entry.is_regular_file()) continue;
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    for (const std::string& name : names) {
        fs::copy_file(sourceDir / name, targetDir / name,
                      fs::copy_options::overwrite_existing, error);
        if (error) {
            throw std::runtime_error("cannot preserve input " + name + ": " +
                                     error.message());
        }
    }
}

class TempCleanup {
public:
    explicit TempCleanup(fs::path path) : path_(std::move(path)) {}
    ~TempCleanup() {
        std::error_code error;
        fs::remove_all(path_, error);
    }
    bool removeNow() {
        std::error_code error;
        fs::remove_all(path_, error);
        return !error && !fs::exists(path_);
    }

private:
    fs::path path_;
};

} // namespace

int runExperiment(const ExperimentOptions& options) {
    const fs::path inputDir = fs::absolute(options.inputDir);
    const fs::path outputDir = fs::absolute(options.outputDir);
    if (!fs::is_directory(inputDir)) {
        std::cerr << "[experiment] input directory not found: "
                  << inputDir.string() << '\n';
        return 2;
    }
    fs::create_directories(outputDir);
    const fs::path tempDir = outputDir / ".tmp";
    const fs::path evidenceInputDir = outputDir / "inputs";
    const fs::path imageDir = outputDir / "images";
    const fs::path selectionDir = outputDir / "selections";
    std::error_code error;
    fs::remove_all(tempDir, error);
    fs::create_directories(tempDir, error);
    fs::create_directories(imageDir, error);
    fs::create_directories(selectionDir, error);
    TempCleanup cleanup(tempDir);

    std::cout << "[experiment] input=" << inputDir.string()
              << " output=" << outputDir.string() << '\n';

    std::vector<std::string> candidateNames;
    copyEvidenceInputs(inputDir, evidenceInputDir, candidateNames);

    const fs::path tracePath = outputDir / "trace.jsonl";
    Journal journal(tracePath);
    LoadedBatch batch;
    batch.evidenceFiles = candidateNames;
    uint64_t nextSequence = 0;
    uint64_t parseErrors = 0;
    bool sawSkipped = false;
    bool loadedAfterSkip = false;

    journal.writeStep(
        "run_begin",
        joinJson({
            field("input_dir", jsonString(inputDir.string())),
            field("evidence_input_dir", jsonString(evidenceInputDir.string())),
            field("output_dir", jsonString(outputDir.string())),
            field("shuffle_seed", static_cast<uint64_t>(options.shuffleSeed)),
        }),
        "{}",
        joinJson({
            field("status", jsonString("scan_started")),
            field("candidate_files", static_cast<uint64_t>(candidateNames.size())),
        }),
        ingestState(batch), true, "",
        joinJson({
            field("original_input_dir", jsonString(inputDir.string())),
            field("evidence_input_dir", jsonString(evidenceInputDir.string())),
            field("shuffle_seed", static_cast<uint64_t>(options.shuffleSeed)),
        }));

    for (const std::string& name : candidateNames) {
        const fs::path source = evidenceInputDir / name;
        const std::string before = ingestState(batch);
        std::string stateAfter;
        try {
            InputObject object = loadInputObject(source, nextSequence);
            const std::string recordHash = objectRecordHash(object);
            batch.submissions.insert(batch.submissions.end(),
                                     object.records.begin(),
                                     object.records.end());
            batch.objects.push_back(object);
            ++batch.loadedObjects;
            if (sawSkipped && batch.loadedObjects > 0) loadedAfterSkip = true;
            stateAfter = ingestState(batch);
            journal.writeStep(
                "object_load",
                joinJson({
                    field("source", jsonString(source.string())),
                    field("extension", jsonString(object.extension)),
                    field("content_hash", jsonString(object.contentHash)),
                    field("format", jsonString(revisionName(object.revision))),
                }),
                before,
                joinJson({
                    field("status", jsonString("ok")),
                    field("format", jsonString(revisionName(object.revision))),
                    field("batch_id", object.batchId),
                    field("record_count",
                          static_cast<uint64_t>(object.records.size())),
                    field("record_hash", jsonString(recordHash)),
                }),
                stateAfter, true, "",
                joinJson({
                    field("evidence_source", jsonString(source.string())),
                    field("record_hash", jsonString(recordHash)),
                    field("record_count",
                          static_cast<uint64_t>(object.records.size())),
                    field("content_hash", jsonString(object.contentHash)),
                }));
        } catch (const InputFormatError& exception) {
            ++parseErrors;
            ++batch.skippedObjects;
            sawSkipped = true;
            stateAfter = ingestState(batch);
            journal.writeStep(
                "object_load",
                joinJson({
                    field("source", jsonString(source.string())),
                    field("extension", jsonString(source.extension())),
                }),
                before,
                joinJson({
                    field("status", jsonString("isolated_error")),
                    field("error_kind", jsonString("InputFormatError")),
                }),
                stateAfter, false, exception.what(),
                joinJson({
                    field("evidence_source", jsonString(source.string())),
                    field("error_kind", jsonString("InputFormatError")),
                    field("continued", true),
                }));
            std::cerr << "[experiment] skip " << name << ": "
                      << exception.what() << '\n';
        } catch (const std::exception& exception) {
            ++parseErrors;
            ++batch.skippedObjects;
            sawSkipped = true;
            stateAfter = ingestState(batch);
            journal.writeStep(
                "object_load",
                joinJson({
                    field("source", jsonString(source.string())),
                    field("extension", jsonString(source.extension())),
                }),
                before,
                joinJson({
                    field("status", jsonString("isolated_error")),
                    field("error_kind", jsonString("std_exception")),
                }),
                stateAfter, false, exception.what(),
                joinJson({
                    field("evidence_source", jsonString(source.string())),
                    field("error_kind", jsonString("std_exception")),
                    field("continued", true),
                }));
            std::cerr << "[experiment] skip " << name << ": "
                      << exception.what() << '\n';
        }
    }

    const bool exceptionIsolated = sawSkipped && loadedAfterSkip;
    std::cout << "[experiment] loaded " << batch.loadedObjects
              << " objects, isolated " << batch.skippedObjects
              << " errors, " << batch.submissions.size()
              << " submissions\n";

    const std::vector<std::string> orders = {
        "canonical", "reversed", "shuffled"};
    std::map<std::string, SelectionResult> selections;
    uint32_t orderStep = 0;
    for (const std::string& order : orders) {
        const std::vector<Submission> ordered =
            makeOrder(batch.submissions, order, options.shuffleSeed);
        for (SelectionPolicy policy : {SelectionPolicy::LegacyV1,
                                       SelectionPolicy::StableV2}) {
            const SelectionResult result =
                resolveSelection(ordered, policy, order);
            const std::string key =
                result.policyName + "|" + result.orderName;
            selections.emplace(key, result);
            journal.writeStep(
                "order_selection",
                joinJson({
                    field("order", jsonString(order)),
                    field("policy", jsonString(result.policyName)),
                    field("submissions",
                          static_cast<uint64_t>(ordered.size())),
                }),
                joinJson({
                    field("matrix_position", orderStep),
                    field("submission_hash",
                          jsonString(submissionSetHash(ordered))),
                }),
                selectionResultJson(result, true),
                selectionState(result), true, "",
                joinJson({
                    field("policy", jsonString(result.policyName)),
                    field("order", jsonString(result.orderName)),
                    field("selection_hash", jsonString(result.selectionHash)),
                    field("draw_order_hash",
                          jsonString(result.drawOrderHash)),
                    field("duplicate_suppressed", result.duplicateSuppressed),
                    field("selected",
                          static_cast<uint64_t>(result.selected.size())),
                }));
            ++orderStep;
        }
    }

    const SelectionResult& legacyCanonical =
        selections.at("legacy-v1|canonical");
    const SelectionResult& stableCanonical =
        selections.at("stable-v2|canonical");
    bool stableOrderIndependent = true;
    bool legacyOrderSensitive = false;
    for (const std::string& order : orders) {
        if (selections.at("stable-v2|" + order).selectionHash !=
            stableCanonical.selectionHash) {
            stableOrderIndependent = false;
        }
        if (selections.at("legacy-v1|" + order).selectionHash !=
            legacyCanonical.selectionHash) {
            legacyOrderSensitive = true;
        }
    }

    bool duplicateSuppressed = true;
    for (SelectionPolicy policy : {SelectionPolicy::LegacyV1,
                                   SelectionPolicy::StableV2}) {
        const SelectionResult& baseline =
            selections.at(std::string(selectionPolicyName(policy)) +
                          "|canonical");
        std::vector<Submission> retried = batch.submissions;
        Submission duplicate = retried.front();
        duplicate.sourceSequence = 900000 + retried.size();
        retried.push_back(duplicate);
        const SelectionResult afterRetry =
            resolveSelection(retried, policy, "duplicate_probe");
        const bool unchanged =
            afterRetry.selectionHash == baseline.selectionHash;
        const bool suppressed =
            afterRetry.duplicateSuppressed > baseline.duplicateSuppressed;
        duplicateSuppressed = duplicateSuppressed && unchanged && suppressed;
        journal.writeStep(
            "duplicate_submit",
            joinJson({
                field("policy", jsonString(selectionPolicyName(policy))),
                field("object_id", duplicate.objectId),
                field("revision", duplicate.revision),
                field("attempt", 2),
            }),
            selectionState(baseline),
            joinJson({
                field("status", jsonString(
                    unchanged && suppressed ? "duplicate_suppressed"
                                            : "duplicate_changed_state")),
                field("selection_hash_unchanged", unchanged),
                field("duplicate_counter_delta",
                      afterRetry.duplicateSuppressed -
                          baseline.duplicateSuppressed),
            }),
            selectionState(afterRetry), unchanged && suppressed, "",
            joinJson({
                field("policy", jsonString(selectionPolicyName(policy))),
                field("before_hash", jsonString(baseline.selectionHash)),
                field("after_hash", jsonString(afterRetry.selectionHash)),
                field("duplicate_suppressed", suppressed),
            }));
    }

    {
        std::ostringstream matrix;
        matrix << "{\n"
               << "  \"generated_at\":" << jsonString(nowIso8601()) << ",\n"
               << "  \"shuffle_seed\":" << options.shuffleSeed << ",\n"
               << "  \"orders\":{\n";
        for (size_t orderIndex = 0; orderIndex < orders.size(); ++orderIndex) {
            const std::string& order = orders[orderIndex];
            matrix << "    " << jsonString(order) << ":{"
                   << "\"legacy_hash\":"
                   << jsonString(selections.at("legacy-v1|" + order).selectionHash)
                   << ",\"stable_hash\":"
                   << jsonString(selections.at("stable-v2|" + order).selectionHash)
                   << ",\"legacy_draw_order_hash\":"
                   << jsonString(selections.at("legacy-v1|" + order).drawOrderHash)
                   << ",\"stable_draw_order_hash\":"
                   << jsonString(selections.at("stable-v2|" + order).drawOrderHash)
                   << "}" << (orderIndex + 1 < orders.size() ? "," : "")
                   << "\n";
        }
        matrix << "  },\n"
               << "  \"checks\":{"
               << "\"stable_order_independent\":" << jsonBool(stableOrderIndependent)
               << ",\"legacy_order_sensitive\":" << jsonBool(legacyOrderSensitive)
               << ",\"duplicate_suppressed\":" << jsonBool(duplicateSuppressed)
               << ",\"exception_isolated\":" << jsonBool(exceptionIsolated)
               << "}\n}\n";
        writeTextFile(outputDir / "order_matrix.json", matrix.str());
    }
    journal.writeStep(
        "order_validation",
        joinJson({
            field("orders", static_cast<uint64_t>(orders.size())),
            field("shuffle_seed", static_cast<uint64_t>(options.shuffleSeed)),
        }),
        "{}",
        joinJson({
            field("stable_order_independent", stableOrderIndependent),
            field("legacy_order_sensitive", legacyOrderSensitive),
            field("duplicate_suppressed", duplicateSuppressed),
            field("exception_isolated", exceptionIsolated),
            field("parse_errors", parseErrors),
        }),
        "{}",
        stableOrderIndependent && legacyOrderSensitive &&
            duplicateSuppressed && exceptionIsolated,
        "", "{}");

    {
        std::ofstream staged(tempDir / "pending_selection.txt");
        staged << "legacy=" << legacyCanonical.selectionHash << '\n'
               << "stable=" << stableCanonical.selectionHash << '\n';
    }
    journal.writeStep(
        "snapshot_prepare",
        joinJson({
            field("legacy_hash", jsonString(legacyCanonical.selectionHash)),
            field("stable_hash", jsonString(stableCanonical.selectionHash)),
        }),
        "{}",
        joinJson({
            field("temporary_state",
                  jsonString((tempDir / "pending_selection.txt").string())),
        }),
        "{}", true, "", "{}");

    RenderSnapshot legacySnapshot;
    RenderSnapshot stableSnapshot;
    ImageDelta imageDelta;
    bool snapshotsDivergent = false;
    try {
        VulkanContext context(640, 400);
        journal.writeStep(
            "vulkan_init",
            joinJson({
                field("api", jsonString("Vulkan 1.0")),
                field("window", jsonString("GLFW null platform")),
                field("width", 640),
                field("height", 400),
            }),
            "{}",
            joinJson({
                field("device", jsonString(context.deviceName())),
                field("offscreen", true),
            }),
            "{}", true, "",
            joinJson({
                field("device", jsonString(context.deviceName())),
                field("width", 640),
                field("height", 400),
            }));

#ifndef INSTANCE_SHADER_DIR
#define INSTANCE_SHADER_DIR "shaders"
#endif
        const fs::path shaderDir = INSTANCE_SHADER_DIR;
        InstanceRenderer renderer(context,
                                  shaderDir / "instance.vert.spv",
                                  shaderDir / "instance.frag.spv");
        legacySnapshot = renderer.render(legacyCanonical, "legacy-v1");
        stableSnapshot = renderer.render(stableCanonical, "stable-v2");
        legacySnapshot.imagePath =
            (imageDir / "legacy_v1.ppm").string();
        stableSnapshot.imagePath =
            (imageDir / "current_v2.ppm").string();
        writePpm(legacySnapshot.imagePath, legacySnapshot.rgba,
                 legacySnapshot.width, legacySnapshot.height);
        writePpm(stableSnapshot.imagePath, stableSnapshot.rgba,
                 stableSnapshot.width, stableSnapshot.height);
        const fs::path diffPath = imageDir / "diff.ppm";
        writeDiffPpm(diffPath, legacySnapshot.rgba, stableSnapshot.rgba,
                     legacySnapshot.width, legacySnapshot.height);
        imageDelta = compareImages(legacySnapshot.rgba, stableSnapshot.rgba,
                                   legacySnapshot.width, legacySnapshot.height);
        snapshotsDivergent = imageDelta.differingPixels > 0;

        for (const RenderSnapshot& snapshot :
             {legacySnapshot, stableSnapshot}) {
            const uint64_t instanceCount = std::accumulate(
                snapshot.draws.begin(), snapshot.draws.end(), uint64_t{0},
                [](uint64_t total, const DrawRecord& draw) {
                    return total + draw.instanceCount;
                });
            journal.writeStep(
                "snapshot_render",
                joinJson({
                    field("label", jsonString(snapshot.label)),
                    field("selection_hash",
                          jsonString(snapshot.selectionHash)),
                    field("image", jsonString(snapshot.imagePath)),
                }),
                selectionState(snapshot.label == "legacy-v1"
                                   ? legacyCanonical : stableCanonical),
                snapshotJson(snapshot),
                "{}", true, "",
                joinJson({
                    field("label", jsonString(snapshot.label)),
                    field("selection_hash",
                          jsonString(snapshot.selectionHash)),
                    field("image_hash", jsonString(snapshot.imageHash)),
                    field("draw_calls",
                          static_cast<uint64_t>(snapshot.draws.size())),
                    field("instances", instanceCount),
                    field("transitions",
                          static_cast<uint64_t>(snapshot.transitions.size())),
                }));
        }
        journal.writeStep(
            "snapshot_compare",
            joinJson({
                field("legacy_image",
                      jsonString(legacySnapshot.imagePath)),
                field("stable_image",
                      jsonString(stableSnapshot.imagePath)),
            }),
            "{}",
            joinJson({
                field("differing_pixels", imageDelta.differingPixels),
                field("total_pixels", imageDelta.totalPixels),
                field("differing_ratio", jsonNumber(imageDelta.differingRatio)),
                field("mean_abs_error", jsonNumber(imageDelta.meanAbsError)),
                field("max_channel_error", imageDelta.maxChannelError),
                field("verdict", jsonString(
                    snapshotsDivergent ? "EXPECTED_DIVERGENT"
                                       : "UNEXPECTED_EQUAL")),
            }),
            "{}", snapshotsDivergent, "",
            joinJson({
                field("differing_pixels", imageDelta.differingPixels),
                field("snapshots_divergent", snapshotsDivergent),
            }));
    } catch (const std::exception& exception) {
        journal.writeStep(
            "render_failure",
            joinJson({field("stage", jsonString("offscreen_instance_render"))}),
            "{}",
            joinJson({field("status", jsonString("fatal_render_error"))}),
            "{}", false, exception.what(), "{}");
        std::cerr << "[experiment] render failure: " << exception.what()
                  << '\n';
    }

    writeTextFile(
        selectionDir / "legacy_v1.json",
        selectionResultJson(legacyCanonical, true) + "\n");
    writeTextFile(
        selectionDir / "stable_v2.json",
        selectionResultJson(stableCanonical, true) + "\n");
    writeTextFile(
        outputDir / "snapshots.json",
        "{\n  \"generated_at\":" + jsonString(nowIso8601()) + ",\n"
        "  \"snapshots\":[" + snapshotJson(legacySnapshot) + ","
        + snapshotJson(stableSnapshot) + "]\n}\n");
    writeTextFile(
        outputDir / "correlation.json",
        "{\n"
        "  \"generated_at\":" + jsonString(nowIso8601()) + ",\n"
        "  \"control\":\"legacy-v1 versus stable-v2\",\n"
        "  \"image_delta\":{"
        "\"differing_pixels\":" + std::to_string(imageDelta.differingPixels) +
        ",\"total_pixels\":" + std::to_string(imageDelta.totalPixels) +
        ",\"differing_ratio\":" + jsonNumber(imageDelta.differingRatio) +
        ",\"mean_abs_error\":" + jsonNumber(imageDelta.meanAbsError) +
        ",\"max_channel_error\":" +
        std::to_string(imageDelta.maxChannelError) + "},\n"
        "  \"evidence\":{\"legacy_image\":" +
        jsonString(legacySnapshot.imagePath) + ",\"stable_image\":" +
        jsonString(stableSnapshot.imagePath) + ",\"diff_image\":" +
        jsonString((imageDir / "diff.ppm").string()) + "},\n"
        "  \"verdict\":" +
        jsonString(snapshotsDivergent ? "EXPECTED_DIVERGENT"
                                      : "UNEXPECTED_EQUAL") + "\n"
        "}\n");

    const bool cleanupOk = cleanup.removeNow();
    journal.writeStep(
        "cleanup",
        joinJson({field("temporary_directory",
                        jsonString(tempDir.string()))}),
        "{}",
        joinJson({
            field("removed", cleanupOk),
            field("exists_after", fs::exists(tempDir)),
        }),
        "{}", cleanupOk, cleanupOk ? "" : "temporary directory still exists",
        joinJson({
            field("temporary_directory",
                  jsonString(tempDir.string())),
            field("removed", cleanupOk),
        }));

    const bool preliminaryPass =
        exceptionIsolated && stableOrderIndependent &&
        legacyOrderSensitive && duplicateSuppressed &&
        snapshotsDivergent && cleanupOk;
    journal.writeStep(
        "run_end",
        "{}",
        "{}",
        joinJson({
            field("loaded_objects", batch.loadedObjects),
            field("skipped_objects", batch.skippedObjects),
            field("accepted_records", batch.submissions.size()),
            field("legacy_selected",
                  static_cast<uint64_t>(legacyCanonical.selected.size())),
            field("stable_selected",
                  static_cast<uint64_t>(stableCanonical.selected.size())),
        }),
        "{}", preliminaryPass, "",
        joinJson({
            field("final_legacy_hash",
                  jsonString(legacyCanonical.selectionHash)),
            field("final_stable_hash",
                  jsonString(stableCanonical.selectionHash)),
            field("loaded_objects", batch.loadedObjects),
            field("skipped_objects", batch.skippedObjects),
            field("accepted_records",
                  static_cast<uint64_t>(batch.submissions.size())),
        }));

    fs::create_directories(outputDir / "replay", error);
    const fs::path replayPath = outputDir / "replay" / "report.json";
    int replayMismatches = 0;
    try {
        replayMismatches = replayTrace(tracePath, replayPath);
    } catch (const std::exception& exception) {
        replayMismatches = -1;
        std::cerr << "[experiment] replay failure: " << exception.what()
                  << '\n';
    }
    journal.writeStep(
        "replay_verify",
        joinJson({field("trace", jsonString(tracePath.string())),
                  field("report", jsonString(replayPath.string()))}),
        "{}",
        joinJson({
            field("mismatches", replayMismatches),
            field("report_written", fs::exists(replayPath)),
        }),
        "{}", replayMismatches == 0, "",
        joinJson({
            field("mismatches", replayMismatches),
            field("report", jsonString(replayPath.string())),
        }));

    const ExperimentSummary summary{
        preliminaryPass && replayMismatches == 0,
        stableOrderIndependent,
        legacyOrderSensitive,
        duplicateSuppressed,
        snapshotsDivergent,
        exceptionIsolated,
        cleanupOk,
        batch.loadedObjects,
        batch.skippedObjects,
        static_cast<uint64_t>(batch.submissions.size()),
        static_cast<uint32_t>(legacyCanonical.selected.size()),
        static_cast<uint32_t>(stableCanonical.selected.size()),
        legacyCanonical.selectionHash,
        stableCanonical.selectionHash,
        imageDelta.differingPixels,
    };

    std::ostringstream manifest;
    manifest << "{\n"
             << "  \"generated_at\":" << jsonString(nowIso8601()) << ",\n"
             << "  \"pass\":" << jsonBool(summary.pass) << ",\n"
             << "  \"input\":{\"directory\":" << jsonString(inputDir.string())
             << ",\"preserved_at\":" << jsonString(evidenceInputDir.string())
             << ",\"files\":" << candidateNames.size() << "},\n"
             << "  \"objects\":{\"loaded\":" << summary.loadedObjects
             << ",\"skipped\":" << summary.skippedObjects << "},\n"
             << "  \"selection\":{\"records\":" << summary.acceptedRecords
             << ",\"legacy_selected\":" << summary.selectedLegacy
             << ",\"stable_selected\":" << summary.selectedStable
             << ",\"legacy_hash\":" << jsonString(summary.legacyHash)
             << ",\"stable_hash\":" << jsonString(summary.stableHash) << "},\n"
             << "  \"checks\":{"
             << "\"stable_order_independent\":" << jsonBool(summary.orderIndependent)
             << ",\"legacy_order_sensitive\":" << jsonBool(summary.legacyOrderSensitive)
             << ",\"duplicate_suppressed\":" << jsonBool(summary.duplicateSuppressed)
             << ",\"snapshots_divergent\":" << jsonBool(summary.snapshotsDivergent)
             << ",\"exception_isolated\":" << jsonBool(summary.exceptionIsolated)
             << ",\"cleanup_ok\":" << jsonBool(summary.cleanupOk)
             << ",\"replay_mismatches\":" << replayMismatches << "},\n"
             << "  \"evidence\":{"
             << "\"trace\":" << jsonString(tracePath.string())
             << ",\"snapshots\":" << jsonString((outputDir / "snapshots.json").string())
             << ",\"correlation\":" << jsonString((outputDir / "correlation.json").string())
             << ",\"order_matrix\":" << jsonString((outputDir / "order_matrix.json").string())
             << ",\"replay\":" << jsonString(replayPath.string()) << "}\n"
             << "}\n";
    writeTextFile(outputDir / "manifest.json", manifest.str());

    std::ostringstream human;
    human << "instance-selection replay experiment\n"
          << "loaded_objects: " << summary.loadedObjects << '\n'
          << "isolated_errors: " << summary.skippedObjects << '\n'
          << "submissions: " << summary.acceptedRecords << '\n'
          << "stable_order_independent: "
          << (summary.orderIndependent ? "yes" : "no") << '\n'
          << "legacy_order_sensitive: "
          << (summary.legacyOrderSensitive ? "yes" : "no") << '\n'
          << "duplicate_suppressed: "
          << (summary.duplicateSuppressed ? "yes" : "no") << '\n'
          << "snapshot_differing_pixels: " << summary.differingPixels << '\n'
          << "temporary_state_cleaned: "
          << (summary.cleanupOk ? "yes" : "no") << '\n'
          << "replay_mismatches: " << replayMismatches << '\n'
          << "verdict: " << (summary.pass ? "PASS" : "FAIL") << '\n';
    writeTextFile(outputDir / "summary.txt", human.str());

    std::cout << "[experiment] " << human.str();
    std::cout << "[experiment] evidence: " << outputDir.string() << '\n';
    return summary.pass ? 0 : 1;
}

int replayTrace(const fs::path& tracePath, const fs::path& reportPath) {
    const std::vector<std::string> lines = readJournalLines(tracePath);
    struct ObjectCheck {
        std::string source;
        std::string recordHash;
        int64_t recordCount = 0;
    };
    struct OrderCheck {
        std::string policy;
        std::string order;
        std::string selectionHash;
        std::string drawOrderHash;
    };
    std::string evidenceInputDir;
    std::string originalInputDir;
    int64_t shuffleSeed = 0;
    std::map<std::string, ObjectCheck> objectChecks;
    std::map<std::string, OrderCheck> orderChecks;
    std::map<std::string, std::pair<std::string, std::string>> duplicateChecks;
    std::string finalLegacyHash;
    std::string finalStableHash;
    int64_t recordedLoaded = -1;
    int64_t recordedSkipped = -1;
    int64_t recordedRecords = -1;

    for (const std::string& line : lines) {
        std::string step;
        if (!extractString(line, "step", step)) continue;
        if (step == "run_begin") {
            extractString(line, "evidence_input_dir", evidenceInputDir);
            extractString(line, "original_input_dir", originalInputDir);
            extractInt(line, "shuffle_seed", shuffleSeed);
        } else if (step == "object_load") {
            std::string source;
            std::string recordHash;
            int64_t recordCount = 0;
            bool ok = false;
            extractBool(line, "ok", ok);
            if (ok && extractString(line, "evidence_source", source) &&
                extractString(line, "record_hash", recordHash) &&
                extractInt(line, "record_count", recordCount)) {
                objectChecks[source] = {source, recordHash, recordCount};
            }
        } else if (step == "order_selection") {
            OrderCheck check;
            if (extractString(line, "policy", check.policy) &&
                extractString(line, "order", check.order) &&
                extractString(line, "selection_hash", check.selectionHash) &&
                extractString(line, "draw_order_hash", check.drawOrderHash)) {
                orderChecks[check.policy + "|" + check.order] = check;
            }
        } else if (step == "duplicate_submit") {
            std::string policy;
            std::string before;
            std::string after;
            if (extractString(line, "policy", policy) &&
                extractString(line, "before_hash", before) &&
                extractString(line, "after_hash", after)) {
                duplicateChecks[policy] = {before, after};
            }
        } else if (step == "run_end") {
            extractString(line, "final_legacy_hash", finalLegacyHash);
            extractString(line, "final_stable_hash", finalStableHash);
            extractInt(line, "loaded_objects", recordedLoaded);
            extractInt(line, "skipped_objects", recordedSkipped);
            extractInt(line, "accepted_records", recordedRecords);
        }
    }
    if (evidenceInputDir.empty() || !fs::is_directory(evidenceInputDir)) {
        throw std::runtime_error(
            "trace does not reference a readable evidence input directory");
    }

    std::vector<std::string> mismatches;
    int checked = 0;
    LoadedBatch replayBatch;
    uint64_t sequence = 0;
    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(evidenceInputDir)) {
        if (entry.is_regular_file()) files.push_back(entry.path());
    }
    std::sort(files.begin(), files.end());
    std::set<std::string> consumedChecks;
    for (const fs::path& file : files) {
        const std::string absolute = fs::absolute(file).string();
        auto check = objectChecks.find(absolute);
        try {
            InputObject object = loadInputObject(file, sequence);
            const std::string recomputed = objectRecordHash(object);
            if (check == objectChecks.end()) {
                mismatches.push_back("unexpected replay acceptance: " + absolute);
            } else {
                consumedChecks.insert(absolute);
                ++checked;
                if (recomputed != check->second.recordHash) {
                    mismatches.push_back("record hash mismatch: " + absolute);
                }
                if (static_cast<int64_t>(object.records.size()) !=
                    check->second.recordCount) {
                    mismatches.push_back("record count mismatch: " + absolute);
                }
            }
            replayBatch.submissions.insert(replayBatch.submissions.end(),
                                           object.records.begin(),
                                           object.records.end());
            ++replayBatch.loadedObjects;
        } catch (const std::exception&) {
            if (check != objectChecks.end()) {
                mismatches.push_back("recorded input no longer parses: " +
                                     absolute);
                consumedChecks.insert(absolute);
                ++checked;
            } else {
                ++replayBatch.skippedObjects;
            }
        }
    }
    for (const auto& entry : objectChecks) {
        if (consumedChecks.count(entry.first) == 0) {
            mismatches.push_back("recorded input missing: " + entry.first);
        }
    }

    std::map<std::string, SelectionResult> recomputedSelections;
    const std::vector<std::string> orders = {
        "canonical", "reversed", "shuffled"};
    for (const std::string& order : orders) {
        const std::vector<Submission> ordered = makeOrder(
            replayBatch.submissions, order, static_cast<uint32_t>(shuffleSeed));
        for (SelectionPolicy policy : {SelectionPolicy::LegacyV1,
                                       SelectionPolicy::StableV2}) {
            SelectionResult result = resolveSelection(ordered, policy, order);
            const std::string key = result.policyName + "|" + order;
            recomputedSelections.emplace(key, result);
            auto recorded = orderChecks.find(key);
            if (recorded == orderChecks.end()) {
                mismatches.push_back("missing order check in trace: " + key);
                continue;
            }
            ++checked;
            if (recorded->second.selectionHash != result.selectionHash) {
                mismatches.push_back("selection hash mismatch: " + key);
            }
            if (recorded->second.drawOrderHash != result.drawOrderHash) {
                mismatches.push_back("draw order hash mismatch: " + key);
            }
        }
    }

    bool duplicateVerified = true;
    if (!replayBatch.submissions.empty()) {
        for (SelectionPolicy policy : {SelectionPolicy::LegacyV1,
                                       SelectionPolicy::StableV2}) {
            const SelectionResult baseline = resolveSelection(
                replayBatch.submissions, policy, "canonical");
            std::vector<Submission> retried = replayBatch.submissions;
            Submission duplicate = retried.front();
            duplicate.sourceSequence = 900000 + retried.size();
            retried.push_back(duplicate);
            const SelectionResult replay = resolveSelection(
                retried, policy, "duplicate_probe");
            auto recorded = duplicateChecks.find(
                selectionPolicyName(policy));
            ++checked;
            if (recorded == duplicateChecks.end()) {
                mismatches.push_back(std::string("missing duplicate check: ") +
                                     selectionPolicyName(policy));
                duplicateVerified = false;
            } else if (recorded->second.first != replay.selectionHash ||
                       recorded->second.second != replay.selectionHash ||
                       replay.duplicateSuppressed <=
                           baseline.duplicateSuppressed) {
                mismatches.push_back(std::string(
                    "duplicate check mismatch: ") +
                    selectionPolicyName(policy));
                duplicateVerified = false;
            }
        }
    } else {
        mismatches.push_back("replay has no submissions");
        duplicateVerified = false;
    }

    const SelectionResult& legacy = recomputedSelections.at(
        "legacy-v1|canonical");
    const SelectionResult& stable = recomputedSelections.at(
        "stable-v2|canonical");
    ++checked;
    if (finalLegacyHash != legacy.selectionHash ||
        finalStableHash != stable.selectionHash) {
        mismatches.push_back("final state hash mismatch");
    }
    if (recordedLoaded >= 0 &&
        static_cast<int64_t>(replayBatch.loadedObjects) != recordedLoaded) {
        mismatches.push_back("loaded object count mismatch");
    }
    if (recordedSkipped >= 0 &&
        static_cast<int64_t>(replayBatch.skippedObjects) != recordedSkipped) {
        mismatches.push_back("skipped object count mismatch");
    }
    if (recordedRecords >= 0 &&
        static_cast<int64_t>(replayBatch.submissions.size()) !=
            recordedRecords) {
        mismatches.push_back("submission count mismatch");
    }
    if (orderChecks.empty()) {
        mismatches.push_back("trace contains no order selection checks");
    }

    std::ostringstream report;
    report << "{\n"
           << "  \"trace\":" << jsonString(fs::absolute(tracePath).string())
           << ",\n"
           << "  \"original_input\":" << jsonString(originalInputDir) << ",\n"
           << "  \"evidence_input\":" << jsonString(evidenceInputDir) << ",\n"
           << "  \"shuffle_seed\":" << shuffleSeed << ",\n"
           << "  \"checkpoints\":" << checked << ",\n"
           << "  \"mismatches\":" << mismatches.size() << ",\n"
           << "  \"duplicate_verified\":" << jsonBool(duplicateVerified)
           << ",\n"
           << "  \"recomputed\":{\"legacy_hash\":"
           << jsonString(legacy.selectionHash)
           << ",\"stable_hash\":" << jsonString(stable.selectionHash)
           << ",\"loaded_objects\":" << replayBatch.loadedObjects
           << ",\"skipped_objects\":" << replayBatch.skippedObjects
           << ",\"submissions\":" << replayBatch.submissions.size() << "},\n"
           << "  \"ok\":" << jsonBool(mismatches.empty()) << ",\n"
           << "  \"details\":[";
    for (size_t i = 0; i < mismatches.size(); ++i) {
        if (i) report << ',';
        report << jsonString(mismatches[i]);
    }
    report << "]\n}\n";
    fs::create_directories(reportPath.parent_path());
    writeTextFile(reportPath, report.str());
    return static_cast<int>(mismatches.size());
}

} // namespace inst




