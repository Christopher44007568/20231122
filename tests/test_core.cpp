#include "core/export.hpp"
#include "core/hash.hpp"
#include "core/io.hpp"
#include "core/json.hpp"
#include "core/replay.hpp"
#include "core/session.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace {

int gFailures = 0;

bool resultHasCheck(const vfcr::VerifyResult& result, const std::string& id);

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++gFailures;
    }
}

void testJson() {
    const std::string source = R"({"z":1,"a":{"two":2,"one":1},"items":[true,null,"x"]})";
    std::string error;
    const vfcr::Json value = vfcr::Json::parse(source, &error);
    expect(error.empty(), "json parses");
    const auto& fields = value.objectItems();
    expect(fields.size() == 3U && fields[0].first == "z" && fields[1].first == "a",
           "json preserves object insertion order");
    const std::string roundTrip = value.dump(false);
    expect(roundTrip == source, "json round trip keeps field order");

    const vfcr::Json nested = value.find("a")->find("one") != nullptr
                                  ? *value.find("a")->find("one")
                                  : vfcr::Json();
    expect(nested.asInt() == 1, "nested lookup works");
    expect(vfcr::sha256("abc") ==
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
           "sha256 known vector");
}

void testReplay() {
    vfcr::Session session = vfcr::makeDefaultSession();
    expect(session.objects.size() == 6U, "default session has six objects");
    expect(!session.objects[3].baselineKnown, "beacon baseline is missing");
    expect(session.objects[3].runs.size() == 1U, "missing object starts with recovery evidence");

    const auto first = vfcr::rerunObject(session, 3, session.playhead, "2026-11-22T00:00:00Z");
    expect(first.sequence == 2, "first manual rerun sequence");
    expect(first.cause == "MISSING_SEGMENT_RECOVERY", "gap rerun is labelled recovery");
    expect(first.visible, "beacon is recoverable as visible");
    expect(!first.inputHash.empty(), "run has deterministic input hash");

    const auto second = vfcr::rerunObject(session, 3, session.playhead, "2026-11-22T00:01:00Z");
    expect(second.inputHash == first.inputHash, "same sample has same deterministic input");
    expect(second.sequence == 3, "reruns append in order");

    std::string chainError;
    expect(vfcr::verifyEvidenceChain(session.evidence, &chainError),
           "evidence chain verifies: " + chainError);

    const std::string before = vfcr::sessionToJson(session).dump(false);
    const vfcr::Session restored = vfcr::sessionFromJson(vfcr::sessionToJson(session));
    expect(vfcr::sessionToJson(restored).dump(false) == before,
           "session serialization is stable across reload");

    const double gapStart = session.missing.startA;
    const double gapEnd = session.missing.endA;
    vfcr::shiftMissingFragment(session, 0.25, "2026-11-22T00:02:00Z");
    expect(std::abs(session.missing.startA - (gapStart + 0.25)) < 1e-9 &&
               std::abs(session.missing.endA - (gapEnd + 0.25)) < 1e-9,
           "missing fragment shifts without changing duration");
}

void testExport() {
    vfcr::Session session = vfcr::makeDefaultSession();
    vfcr::rerunObject(session, 3, session.playhead, "2026-11-22T00:00:00Z");
    const std::string directory =
        (std::filesystem::temp_directory_path() / "vfcr-test-export").string();
    std::filesystem::remove_all(directory);
    std::string error;
    expect(vfcr::writeTextFileAtomic(vfcr::pathJoin(directory, "frame.ppm"), "test-frame", &error),
           "test screenshot writes");

    vfcr::ExportOptions options;
    options.directory = directory;
    options.screenshotSha256 = vfcr::sha256("test-frame");
    options.screenshotWidth = 320;
    options.screenshotHeight = 200;
    options.focusObjectId = "OBJ-112";
    std::string recordPath;
    expect(vfcr::writeExportBundle(session, options, &recordPath, &error),
           "export bundle writes: " + error);
    expect(vfcr::pathExists(recordPath), "record.json exists");

    const vfcr::VerifyResult verified = vfcr::verifyExportFile(recordPath);
    expect(verified.ok, verified.failures.empty() ? "record verifies"
                                                  : "record verifies: " + verified.failures[0]);
    expect(resultHasCheck(verified, "reload.object-and-run-order"),
           "verifier checks field order");
    expect(resultHasCheck(verified, "export.scope"), "verifier checks export scope");

    vfcr::Session reloaded;
    expect(vfcr::loadExportRecord(recordPath, reloaded, &error),
           "export bundle reloads: " + error);
    expect(reloaded.objects.size() == session.objects.size(), "reload keeps object count");
    expect(reloaded.objects[3].runs.size() == session.objects[3].runs.size(),
           "reload keeps run order");
    expect(vfcr::objectToJson(reloaded.objects[3]).dump(false) ==
               vfcr::objectToJson(session.objects[3]).dump(false),
           "reload preserves object fields and order");

    std::string text;
    expect(vfcr::readTextFile(recordPath, text, &error), "tamper input reads");
    const std::string needle = "\"visible\": true";
    const std::size_t position = text.find(needle);
    if (position != std::string::npos) {
        text.replace(position, needle.size(), "\"visible\": false");
        expect(vfcr::writeTextFileAtomic(recordPath, text, &error), "tamper write");
        const vfcr::VerifyResult tampered = vfcr::verifyExportFile(recordPath);
        expect(!tampered.ok, "tampered record is rejected");
    }
    std::filesystem::remove_all(directory);
}

bool resultHasCheck(const vfcr::VerifyResult& result, const std::string& id) {
    for (const auto& check : result.checks) {
        if (check.id == id) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    testJson();
    testReplay();
    testExport();
    if (gFailures != 0) {
        std::cerr << gFailures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all core tests passed\n";
    return 0;
}
