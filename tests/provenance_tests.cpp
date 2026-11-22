#include "hdr_provenance.h"
#include "sha256.h"

#include <nlohmann/json.hpp>

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
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

}  // namespace

int main() {
    try {
        const fs::path root = HDRPROV_SOURCE_DIR;
        const hdrprov::BatchResult batch =
            hdrprov::processBatch({root, true});

        require(batch.records.size() == 3, "expected exactly three source objects");
        require(batch.records[0].status == hdrprov::RecordStatus::Pass, "v1 should pass");
        require(batch.records[1].status == hdrprov::RecordStatus::Undecidable,
                "truncated record should stay undecidable");
        require(batch.records[2].status == hdrprov::RecordStatus::Pass,
                "v2 must still run after the exception");
        require(batch.exceptionCaught, "truncated exception was not caught");
        require(batch.continuedAfterException, "batch did not continue after exception");
        require(batch.passCount == 2, "expected two passing snapshots");
        require(batch.undecidableCount == 1, "expected one undecidable boundary");
        require(batch.tempCleaned, "temporary state was not cleaned");
        require(!fs::exists(batch.tempPath), "temp directory still exists");
        require(!fs::exists(root / ".tmp") || fs::is_empty(root / ".tmp"),
                "temporary state root is not empty");

        const auto& v1 = batch.records[0];
        const auto& partial = batch.records[1];
        const auto& v2 = batch.records[2];
        require(v1.preview.width == 96 && v1.preview.height == 64,
                "unexpected v1 preview dimensions");
        require(v2.preview.width == 96 && v2.preview.height == 64,
                "unexpected v2 preview dimensions");
        require(v2.changedPixels > 0, "v2 should differ from v1");
        require(partial.fileSize == 409, "truncated input size drifted");
        require(partial.declaredSize == 8192, "declared size drifted");
        require(partial.sourceRevision == "v2", "source revision was not retained");
        require(partial.sourceOffset == 18432, "source offset was not retained");
        require(v2.sha256 == hdrprov::sha256Hex(readBytes(root / v2.sourcePath)),
                "v2 input hash does not match the original bytes");

        for (const auto& record : batch.records) {
            std::set<std::string> stepIds;
            std::set<std::string> evidenceIds;
            for (const auto& step : record.steps) stepIds.insert(step.id);
            for (const auto& evidence : record.evidence) evidenceIds.insert(evidence.id);
            require(!record.conclusion.stepIds.empty(), "conclusion has no step refs");
            require(!record.conclusion.evidenceIds.empty(), "conclusion has no evidence refs");
            for (const auto& id : record.conclusion.stepIds) {
                require(stepIds.count(id) == 1, "dangling step reference " + id);
            }
            for (const auto& id : record.conclusion.evidenceIds) {
                require(evidenceIds.count(id) == 1, "dangling evidence reference " + id);
            }
        }

        const fs::path evidence = root / "artifacts" / "evidence";
        require(fs::exists(evidence / "manifest.json"), "manifest missing");
        require(fs::exists(evidence / "processing_trace.jsonl"), "trace missing");
        require(fs::exists(evidence / "boundary_bytes.hex"), "boundary evidence missing");
        require(fs::exists(evidence / "snap-v1.record.json"), "v1 record missing");
        require(fs::exists(evidence / "tile-07.record.json"), "boundary record missing");
        require(fs::exists(evidence / "snap-v2.record.json"), "v2 record missing");

        const auto manifest = nlohmann::json::parse(readText(evidence / "manifest.json"));
        require(manifest["temporaryState"]["cleaned"] == true,
                "manifest does not report temp cleanup");
        require(manifest["continuedAfterException"] == true,
                "manifest does not report continuation");
        require(manifest["summary"]["undecidable"] == 1,
                "manifest boundary count mismatch");

        const std::string boundary = readText(evidence / "boundary_bytes.hex");
        require(boundary.find("UNDECIDABLE") != std::string::npos,
                "boundary classification not retained");
        require(boundary.find("input sha256") != std::string::npos,
                "boundary is not tied to original input");
        require(boundary.find("declared bytes: 8192") != std::string::npos,
                "declared length not retained");

        const std::string trace = readText(evidence / "processing_trace.jsonl");
        require(trace.find("exception_caught_and_classified") != std::string::npos,
                "exception event missing from trace");
        require(trace.find("temp_cleanup") != std::string::npos,
                "cleanup event missing from trace");

        std::cout << "provenance tests passed: 3 objects, 2 pass, 1 undecidable, "
                     "continued=" << (batch.continuedAfterException ? "yes" : "no")
                  << ", temp_cleaned=" << (batch.tempCleaned ? "yes" : "no") << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "provenance tests failed: " << error.what() << '\n';
        return 1;
    }
}
