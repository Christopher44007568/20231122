#include "format.hpp"
#include "selection.hpp"

#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

inst::Submission make(uint32_t id, uint32_t revision, uint32_t selector,
                      uint32_t model, float x) {
    inst::Submission item;
    item.objectId = id;
    item.revision = revision;
    item.legacySelector = selector;
    item.model = model;
    item.x = x;
    item.y = 0.0f;
    item.scale = 0.5f;
    item.r = 0.9f;
    item.g = 0.5f;
    item.b = 0.2f;
    return item;
}

} // namespace

int main() {
    const inst::fs::path directory =
        inst::fs::temp_directory_path() / "instance_selection_core_tests";
    inst::fs::create_directories(directory);

    const std::vector<inst::Submission> records = {
        make(1, 1, 0, 1, -0.5f),
        make(2, 1, 1, 0, 0.0f),
        make(2, 2, 0, 2, 0.0f),
        make(3, 1, 2, 2, 0.5f),
    };
    const inst::fs::path legacyPath = directory / "fixture.imd1";
    const inst::fs::path currentPath = directory / "fixture.imd2";
    inst::writeInputObject(legacyPath, inst::FormatRevision::LegacyV1,
                           7, records);
    inst::writeInputObject(currentPath, inst::FormatRevision::CurrentV2,
                           7, records);

    uint64_t sequence = 0;
    const inst::InputObject legacy =
        inst::loadInputObject(legacyPath, sequence);
    const inst::InputObject current =
        inst::loadInputObject(currentPath, sequence);
    check(legacy.revision == inst::FormatRevision::LegacyV1,
          "legacy fixture dispatch");
    check(current.revision == inst::FormatRevision::CurrentV2,
          "current fixture dispatch");
    check(legacy.records.size() == records.size(),
          "legacy record count");
    check(current.records.size() == records.size(),
          "current record count");
    check(current.records[2].model == 2,
          "explicit v2 model survives parse");

    const std::vector<inst::Submission> ordered = {
        make(1, 1, 0, 1, -0.5f),
        make(2, 1, 1, 0, 0.0f),
        make(2, 2, 0, 2, 0.0f),
        make(3, 1, 2, 2, 0.5f),
    };
    const std::vector<inst::Submission> reversed(ordered.rbegin(),
                                                 ordered.rend());
    const inst::SelectionResult legacyA = inst::resolveSelection(
        ordered, inst::SelectionPolicy::LegacyV1, "canonical");
    const inst::SelectionResult legacyB = inst::resolveSelection(
        reversed, inst::SelectionPolicy::LegacyV1, "reversed");
    const inst::SelectionResult stableA = inst::resolveSelection(
        ordered, inst::SelectionPolicy::StableV2, "canonical");
    const inst::SelectionResult stableB = inst::resolveSelection(
        reversed, inst::SelectionPolicy::StableV2, "reversed");
    check(legacyA.selectionHash != legacyB.selectionHash,
          "legacy selection is arrival-order sensitive");
    check(stableA.selectionHash == stableB.selectionHash,
          "stable selection is order independent");
    check(stableA.selected.size() == 3, "one selection per object id");
    check(stableA.selected[1].revision == 2,
          "stable selection keeps highest revision");

    std::vector<inst::Submission> duplicate = ordered;
    duplicate.push_back(ordered.front());
    const inst::SelectionResult duplicateStable = inst::resolveSelection(
        duplicate, inst::SelectionPolicy::StableV2, "duplicate");
    check(duplicateStable.selectionHash == stableA.selectionHash,
          "duplicate does not change stable state");
    check(duplicateStable.duplicateSuppressed == 1,
          "duplicate is counted as suppressed");

    const inst::fs::path unknown = directory / "mystery.zzz";
    {
        std::ofstream out(unknown, std::ios::binary);
        const uint32_t magic = 0x58444D49u;
        out.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
        out << "not a supported container";
    }
    bool formatThrown = false;
    try {
        inst::loadInputObject(unknown, sequence);
    } catch (const inst::InputFormatError&) {
        formatThrown = true;
    }
    check(formatThrown, "unknown format raises typed error");

    std::error_code error;
    inst::fs::remove_all(directory, error);
    if (failures == 0) {
        std::cout << "core tests passed\n";
        return 0;
    }
    std::cerr << failures << " core test(s) failed\n";
    return 1;
}
