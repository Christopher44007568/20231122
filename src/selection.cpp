#include "selection.hpp"

#include <algorithm>
#include <iomanip>
#include <map>
#include <sstream>

namespace inst {
namespace {

struct PolicySpec {
    bool stableOrder = false;
    bool highestRevision = false;
    bool legacyModelLookup = false;
};

PolicySpec specFor(SelectionPolicy policy) {
    if (policy == SelectionPolicy::LegacyV1) {
        return {false, false, true};
    }
    return {true, true, false};
}

std::string canonicalRecord(const SelectedInstance& instance) {
    std::ostringstream out;
    out << instance.objectId << '|' << instance.revision << '|'
        << instance.model << '|' << instance.legacySelector << '|'
        << std::setprecision(8) << instance.x << ',' << instance.y << ','
        << instance.z << ',' << instance.scale << ',' << instance.r << ','
        << instance.g << ',' << instance.b << ',' << instance.yaw;
    return out.str();
}

std::string orderedHash(std::vector<SelectedInstance> selected,
                        bool sortByObjectId) {
    if (sortByObjectId) {
        std::sort(selected.begin(), selected.end(),
                  [](const SelectedInstance& a, const SelectedInstance& b) {
                      return a.objectId < b.objectId;
                  });
    }
    std::string payload;
    for (const SelectedInstance& item : selected) {
        payload += canonicalRecord(item);
        payload.push_back('\n');
    }
    return hex64(fnv1a64(payload));
}

std::string eventJson(const SelectionEvent& event) {
    return joinJson({
        field("object_id", event.objectId),
        field("incoming_revision", event.incomingRevision),
        field("retained_revision", event.retainedRevision),
        field("incoming_sequence", event.incomingSequence),
        field("action", jsonString(event.action)),
        field("reason", jsonString(event.reason)),
    });
}

} // namespace

const char* selectionPolicyName(SelectionPolicy policy) {
    return policy == SelectionPolicy::LegacyV1 ? "legacy-v1" : "stable-v2";
}

std::string submissionSignature(const Submission& submission) {
    std::ostringstream out;
    out << submission.objectId << '|' << submission.revision << '|'
        << submission.legacySelector << '|' << submission.model << '|'
        << std::setprecision(8) << submission.x << ',' << submission.y << ','
        << submission.z << ',' << submission.scale << ',' << submission.r
        << ',' << submission.g << ',' << submission.b << ','
        << submission.yaw;
    return out.str();
}

SelectionResult resolveSelection(const std::vector<Submission>& submissions,
                                 SelectionPolicy policy,
                                 const std::string& orderName) {
    const PolicySpec spec = specFor(policy);
    SelectionResult result;
    result.policyName = selectionPolicyName(policy);
    result.orderName = orderName;

    std::map<uint32_t, SelectedInstance> winners;
    std::vector<uint32_t> arrivalOrder;
    for (const Submission& submission : submissions) {
        const uint32_t selectedModel = spec.legacyModelLookup
            ? submission.legacySelector % 3u
            : submission.model % 3u;
        const std::string incomingSignature = submissionSignature(submission);
        auto found = winners.find(submission.objectId);
        if (found == winners.end()) {
            SelectedInstance item;
            item.objectId = submission.objectId;
            item.revision = submission.revision;
            item.model = selectedModel;
            item.legacySelector = submission.legacySelector;
            item.x = submission.x;
            item.y = submission.y;
            item.z = submission.z;
            item.scale = submission.scale;
            item.r = submission.r;
            item.g = submission.g;
            item.b = submission.b;
            item.yaw = submission.yaw;
            item.batchId = submission.batchId;
            item.sourceSequence = static_cast<uint32_t>(submission.sourceSequence);
            item.sourcePath = submission.sourcePath;
            item.sourceExtension = submission.sourceExtension;
            item.signature = incomingSignature;
            item.reason = "accepted";
            winners.emplace(item.objectId, item);
            arrivalOrder.push_back(item.objectId);
            ++result.accepted;
            result.events.push_back({item.objectId, submission.revision,
                                     item.revision, item.sourceSequence,
                                     "accepted", "first_occurrence"});
            continue;
        }

        SelectedInstance& current = found->second;
        const SelectedInstance incoming = [&] {
            SelectedInstance item = current;
            item.revision = submission.revision;
            item.model = selectedModel;
            item.legacySelector = submission.legacySelector;
            item.x = submission.x;
            item.y = submission.y;
            item.z = submission.z;
            item.scale = submission.scale;
            item.r = submission.r;
            item.g = submission.g;
            item.b = submission.b;
            item.yaw = submission.yaw;
            item.batchId = submission.batchId;
            item.sourceSequence = static_cast<uint32_t>(submission.sourceSequence);
            item.sourcePath = submission.sourcePath;
            item.sourceExtension = submission.sourceExtension;
            item.signature = incomingSignature;
            return item;
        }();

        if (incomingSignature == current.signature) {
            ++result.duplicateSuppressed;
            result.events.push_back({submission.objectId, submission.revision,
                                     current.revision,
                                     static_cast<uint32_t>(submission.sourceSequence),
                                     "duplicate_suppressed",
                                     "identical_payload"});
            continue;
        }

        if (spec.highestRevision) {
            const bool higherRevision = submission.revision > current.revision;
            const bool sameRevisionWinsTie = submission.revision == current.revision &&
                incomingSignature > current.signature;
            if (higherRevision || sameRevisionWinsTie) {
                current = incoming;
                current.reason = higherRevision ? "superseded_higher_revision"
                                                : "superseded_stable_tiebreak";
                ++result.superseded;
                result.events.push_back({submission.objectId, submission.revision,
                                         current.revision,
                                         static_cast<uint32_t>(submission.sourceSequence),
                                         "superseded", current.reason});
            } else {
                ++result.rejected;
                result.events.push_back({submission.objectId, submission.revision,
                                         current.revision,
                                         static_cast<uint32_t>(submission.sourceSequence),
                                         "rejected", "lower_or_equal_revision"});
            }
        } else {
            ++result.conflictsKeptFirst;
            result.events.push_back({submission.objectId, submission.revision,
                                     current.revision,
                                     static_cast<uint32_t>(submission.sourceSequence),
                                     "conflict_kept_first", "arrival_order"});
        }
    }

    if (spec.stableOrder) {
        for (const auto& entry : winners) result.selected.push_back(entry.second);
    } else {
        for (uint32_t id : arrivalOrder) result.selected.push_back(winners.at(id));
    }
    result.selectionHash = orderedHash(result.selected, true);
    result.drawOrderHash = orderedHash(result.selected, false);
    return result;
}

std::string selectionResultJson(const SelectionResult& result,
                                bool includeEvents) {
    std::ostringstream out;
    out << '{'
        << field("policy", jsonString(result.policyName)) << ','
        << field("order", jsonString(result.orderName)) << ','
        << field("selection_hash", jsonString(result.selectionHash)) << ','
        << field("draw_order_hash", jsonString(result.drawOrderHash)) << ','
        << field("accepted", result.accepted) << ','
        << field("duplicate_suppressed", result.duplicateSuppressed) << ','
        << field("superseded", result.superseded) << ','
        << field("rejected", result.rejected) << ','
        << field("conflicts_kept_first", result.conflictsKeptFirst) << ','
        << "\"selected\":[";
    for (size_t i = 0; i < result.selected.size(); ++i) {
        if (i) out << ',';
        const SelectedInstance& item = result.selected[i];
        out << joinJson({
            field("object_id", item.objectId),
            field("revision", item.revision),
            field("model", item.model),
            field("model_name", jsonString(modelName(item.model))),
            field("legacy_selector", item.legacySelector),
            field("x", jsonNumber(item.x)),
            field("y", jsonNumber(item.y)),
            field("z", jsonNumber(item.z)),
            field("scale", jsonNumber(item.scale)),
            field("r", jsonNumber(item.r)),
            field("g", jsonNumber(item.g)),
            field("b", jsonNumber(item.b)),
            field("yaw", jsonNumber(item.yaw)),
            field("source_sequence", item.sourceSequence),
            field("source", jsonString(item.sourcePath)),
            field("reason", jsonString(item.reason)),
        });
    }
    out << ']';
    if (includeEvents) {
        out << ",\"events\":[";
        for (size_t i = 0; i < result.events.size(); ++i) {
            if (i) out << ',';
            out << eventJson(result.events[i]);
        }
        out << ']';
    }
    out << '}';
    return out.str();
}

} // namespace inst
