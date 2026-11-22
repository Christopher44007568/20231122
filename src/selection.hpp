#pragma once

#include "format.hpp"

#include <string>
#include <vector>

namespace inst {

enum class SelectionPolicy {
    LegacyV1,
    StableV2,
};

struct SelectionEvent {
    uint32_t objectId = 0;
    uint32_t incomingRevision = 0;
    uint32_t retainedRevision = 0;
    uint32_t incomingSequence = 0;
    std::string action;
    std::string reason;
};

struct SelectedInstance {
    uint32_t objectId = 0;
    uint32_t revision = 0;
    uint32_t model = 0;
    uint32_t legacySelector = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
    float yaw = 0.0f;
    uint32_t batchId = 0;
    uint32_t sourceSequence = 0;
    std::string sourcePath;
    std::string sourceExtension;
    std::string signature;
    std::string reason;
};

struct SelectionResult {
    std::string policyName;
    std::string orderName;
    std::vector<SelectedInstance> selected;
    std::vector<SelectionEvent> events;
    uint64_t accepted = 0;
    uint64_t duplicateSuppressed = 0;
    uint64_t superseded = 0;
    uint64_t rejected = 0;
    uint64_t conflictsKeptFirst = 0;
    std::string selectionHash;
    std::string drawOrderHash;
};

const char* selectionPolicyName(SelectionPolicy policy);
std::string submissionSignature(const Submission& submission);
SelectionResult resolveSelection(const std::vector<Submission>& submissions,
                                 SelectionPolicy policy,
                                 const std::string& orderName);

std::string selectionResultJson(const SelectionResult& result, bool includeEvents);

} // namespace inst
