#include "record.hpp"
#include "sha256.hpp"
#include "state.hpp"

#undef NDEBUG
#include <cassert>
#include <filesystem>
#include <iostream>

using namespace offscreen;

namespace {

StateMachine runPrefix(const std::vector<Event>& events, std::size_t count) {
    StateMachine machine;
    for (std::size_t i = 0; i < count; ++i) machine.apply(events[i]);
    return machine;
}

void testHashes() {
    assert(sha256Hex("") ==
           "e3b0c44298fc1c149afbf4c8996fb924"
           "27ae41e4649b934ca495991b7852b855");
    assert(sha256Hex("abc") ==
           "ba7816bf8f01cfea414140de5dae2223"
           "b00361a396177a9cb410ff61f20015ad");
}

void testRecordAndRecovery() {
    const auto record = loadRecord(std::filesystem::path(TEST_DATA_DIR) /
                                   "canonical.record");
    assert(record.events.size() == 12);
    assert(!record.truncatedTail);

    const auto truncatedText = makeTruncatedRecord(record.events, 4, 13);
    const auto tempPath = std::filesystem::temp_directory_path() /
                          "offscreen-truncated-test.record";
    writeText(tempPath, truncatedText);
    const auto truncated = loadRecord(tempPath);
    assert(truncated.truncatedTail);
    assert(truncated.events.size() == 5);
    assert(truncated.incompleteTail.rfind("5\tADD", 0) == 0);

    StateMachine baseline;
    for (const Event& event : record.events) baseline.apply(event);
    const VisualState expected = baseline.snapshot().visual;

    StateMachine prefix = runPrefix(record.events, 5);
    const std::string checkpointText =
        serializeCheckpoint(prefix.snapshot(), std::string(64, 'a'));
    const auto checkpointPath = std::filesystem::temp_directory_path() /
                                "offscreen-checkpoint-test.ckpt";
    writeText(checkpointPath, checkpointText);
    Checkpoint checkpoint = loadCheckpoint(checkpointPath);
    assert(checkpoint.machine.snapshot().visual == prefix.snapshot().visual);
    for (std::size_t i = 5; i < record.events.size(); ++i) {
        checkpoint.machine.apply(record.events[i]);
    }
    assert(checkpoint.machine.snapshot().visual == expected);
    assert(checkpoint.frameSha256 == std::string(64, 'a'));
}

void testDuplicateHandling() {
    const auto record = loadRecord(std::filesystem::path(TEST_DATA_DIR) /
                                   "canonical.record");
    StateMachine machine = runPrefix(record.events, 5);
    for (std::size_t i = 5; i <= 11; ++i) machine.apply(record.events[i]);
    const VisualState finalVisual = machine.snapshot().visual;

    const StepOutcome duplicateEvent = machine.apply(record.events[11]);
    assert(duplicateEvent.status == "duplicate_event_dropped");
    assert(machine.snapshot().visual == finalVisual);

    Event duplicateSubmit;
    duplicateSubmit.id = 100;
    duplicateSubmit.op = "SUBMIT";
    duplicateSubmit.arg0 = "rev-2";
    const StepOutcome duplicateSubmission = machine.apply(duplicateSubmit);
    assert(duplicateSubmission.status == "duplicate_submit_deduped");
    assert(machine.snapshot().visual == finalVisual);
}

void testReorderFindsFirstDivergence() {
    const auto record = loadRecord(std::filesystem::path(TEST_DATA_DIR) /
                                   "canonical.record");
    StateMachine baseline = runPrefix(record.events, 8);
    const VisualState baselineAt7 = baseline.snapshot().visual;
    baseline.apply(record.events[8]);
    const VisualState baselineAt8 = baseline.snapshot().visual;
    baseline.apply(record.events[9]);
    const VisualState baselineAt9 = baseline.snapshot().visual;
    assert(!(baselineAt7 == baselineAt8));
    assert(!(baselineAt8 == baselineAt9));

    StateMachine reordered = runPrefix(record.events, 8);
    reordered.apply(record.events[9]);
    assert(!(reordered.snapshot().visual == baselineAt8));
    assert(reordered.snapshot().visual == baselineAt7);
    reordered.apply(record.events[8]);
    assert(reordered.snapshot().visual == baselineAt8);
    assert(!(reordered.snapshot().visual == baselineAt9));
}

}  // namespace

int main() {
    testHashes();
    testRecordAndRecovery();
    testDuplicateHandling();
    testReorderFindsFirstDivergence();
    std::cout << "state tests passed\n";
    return 0;
}
