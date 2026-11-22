#include "surface_provenance.h"
#include "sha256.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

std::string readText(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    try {
        const fs::path root = SURFPROV_SOURCE_DIR;
        const surfprov::BatchResult batch =
            surfprov::processBatch({root, true});

        require(batch.records.size() == 3, "expected exactly three objects");
        require(batch.records[0].id == "snap-v1", "v1 ordering drifted");
        require(batch.records[1].id == "unknown-uvx",
                "unknown extension should be processed between snapshots");
        require(batch.records[2].id == "snap-v2", "v2 ordering drifted");
        require(batch.records[0].status == surfprov::RecordStatus::Pass,
                "v1 should pass");
        require(batch.records[1].status == surfprov::RecordStatus::Failed,
                "unknown extension should be isolated as failed");
        require(batch.records[2].status == surfprov::RecordStatus::Undecidable,
                "v2 should carry the undecidable seam boundary");
        require(batch.snapshotCount == 2, "expected two version snapshots");
        require(batch.validSnapshotCount == 2,
                "both snapshots should decode successfully");
        require(batch.passCount == 1, "expected one fully passing object");
        require(batch.failedCount == 1, "expected one isolated object");
        require(batch.undecidableCount == 1, "expected one undecidable object");
        require(batch.boundaryCount == 1, "expected one boundary record");
        require(batch.exceptionCaught, "unknown format exception was not caught");
        require(batch.continuedAfterException,
                "batch did not continue after the unknown extension");
        require(batch.tempCleaned, "temporary state was not cleaned");
        require(!fs::exists(batch.tempPath), "temp directory still exists");
        require(!fs::exists(root / ".tmp") || fs::is_empty(root / ".tmp"),
                "temporary root is not empty");

        const auto& unknown = batch.records[1];
        const auto& v2 = batch.records[2];
        require(unknown.extension == "uvx", "unknown extension was not recorded");
        require(unknown.fileSize == 96, "unknown probe size drifted");
        require(unknown.evidence.size() >= 2,
                "unknown extension raw evidence was not preserved");
        require(v2.changedPixels > 0, "v2 should differ from v1 preview");
        require(v2.boundary.present, "v2 boundary info missing");
        require(v2.boundary.seamIndex == 2, "ambiguous seam index drifted");
        require(v2.boundary.orientationField == 0,
                "ambiguous seam orientation should remain unspecified");
        require(v2.boundary.tolerance > 0.0f, "boundary tolerance missing");
        require(std::abs(v2.boundary.candidateForward) <=
                    v2.boundary.tolerance &&
                std::abs(v2.boundary.candidateReverse) <=
                    v2.boundary.tolerance,
                "both boundary candidates must remain inside tolerance");
        require(v2.boundary.candidateForward * v2.boundary.candidateReverse < 0.0f,
                "boundary candidates should disagree in sign");
        require(v2.sha256 == surfprov::sha256Hex(readBytes(root / v2.sourcePath)),
                "v2 hash does not match original input bytes");

        for (const auto& record : batch.records) {
            std::set<std::string> stepIds;
            std::set<std::string> evidenceIds;
            for (const auto& step : record.steps) {
                require(stepIds.insert(step.id).second,
                        "duplicate step id in " + record.id + ": " + step.id);
                for (const auto& id : step.evidenceIds) {
                    require(!id.empty(), "empty evidence reference in step");
                }
            }
            for (const auto& evidence : record.evidence) {
                require(evidenceIds.insert(evidence.id).second,
                        "duplicate evidence id in " + record.id + ": " + evidence.id);
                require(!evidence.sha256.empty(), "evidence hash missing");
            }
            require(!record.conclusion.stepIds.empty(),
                    "conclusion has no step references");
            require(!record.conclusion.evidenceIds.empty(),
                    "conclusion has no evidence references");
            for (const auto& id : record.conclusion.stepIds) {
                require(stepIds.count(id) == 1,
                        "dangling step reference " + id + " in " + record.id);
            }
            for (const auto& id : record.conclusion.evidenceIds) {
                require(evidenceIds.count(id) == 1,
                        "dangling evidence reference " + id + " in " + record.id);
            }
            for (const auto& step : record.steps) {
                for (const auto& id : step.evidenceIds) {
                    require(evidenceIds.count(id) == 1,
                            "dangling step evidence reference " + id);
                }
            }
        }

        const fs::path evidence = root / "artifacts" / "evidence";
        require(fs::exists(evidence / "manifest.json"), "manifest missing");
        require(fs::exists(evidence / "processing_trace.jsonl"), "trace missing");
        require(fs::exists(evidence / "input_manifest.sha256"),
                "input manifest hash missing");
        require(fs::exists(evidence / "boundary_seam.hex"),
                "boundary evidence missing");
        require(fs::exists(evidence / "snap-v1.record.json"), "v1 record missing");
        require(fs::exists(evidence / "unknown-uvx.record.json"),
                "unknown record missing");
        require(fs::exists(evidence / "snap-v2.record.json"), "v2 record missing");

        const auto manifest =
            nlohmann::json::parse(readText(evidence / "manifest.json"));
        require(manifest["temporaryState"]["cleaned"] == true,
                "manifest does not report cleanup");
        require(manifest["continuedAfterException"] == true,
                "manifest does not report continuation");
        require(manifest["summary"]["undecidable"] == 1,
                "manifest boundary count mismatch");
        require(manifest["summary"]["validSnapshots"] == 2,
                "manifest valid snapshot count mismatch");

        const std::string boundary = readText(evidence / "boundary_seam.hex");
        require(boundary.find("UNDECIDABLE") != std::string::npos,
                "boundary classification missing");
        require(boundary.find("candidateForward") != std::string::npos,
                "boundary candidate metrics missing");
        require(boundary.find("E06") != std::string::npos,
                "boundary raw-byte evidence missing");
        require(boundary.find(v2.sha256) != std::string::npos,
                "boundary is not tied to the original input hash");

        const std::string trace = readText(evidence / "processing_trace.jsonl");
        require(trace.find("exception_caught_and_isolated") != std::string::npos,
                "exception event missing");
        require(trace.find("boundary_undecidable") != std::string::npos,
                "boundary event missing");
        require(trace.find("temp_cleanup") != std::string::npos,
                "cleanup event missing");

        std::cout << "provenance tests passed: snapshots=2 isolated=1 "
                     "boundary=1 continued=yes temp_cleaned=yes\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "provenance tests failed: " << error.what() << '\n';
        return 1;
    }
}
