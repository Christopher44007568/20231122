#include "core/export.hpp"

#include "core/hash.hpp"
#include "core/io.hpp"
#include "core/session.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace vfcr {

namespace {

Json fieldOrderJson(const std::vector<std::string>& fields) {
    Json array = Json::array();
    for (const std::string& field : fields) {
        array.push(field);
    }
    return array;
}

Json assertion(const std::string& id, bool passed, const std::string& detail) {
    Json value = Json::object();
    value.set("id", id);
    value.set("status", passed ? "PASS" : "FAIL");
    value.set("detail", detail);
    return value;
}

std::string boolText(bool value) {
    return value ? "true" : "false";
}

VerifyCheck makeCheck(const std::string& id, bool passed, const std::string& detail) {
    VerifyCheck check;
    check.id = id;
    check.passed = passed;
    check.detail = detail;
    return check;
}

const Json* pathFind(const Json& root, std::initializer_list<const char*> path) {
    const Json* current = &root;
    for (const char* key : path) {
        current = current->find(key);
        if (current == nullptr) {
            return nullptr;
        }
    }
    return current;
}

bool validateOrder(const Json& document, const std::vector<std::string>& expected,
                   const std::string& label, std::string& error) {
    if (!document.isObject()) {
        error = label + " is not an object";
        return false;
    }
    const auto& actual = document.objectItems();
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (i >= actual.size()) {
            error = label + " missing field " + expected[i];
            return false;
        }
        if (actual[i].first != expected[i]) {
            error = label + " field order mismatch: expected " + expected[i] + ", found " +
                    actual[i].first;
            return false;
        }
    }
    return true;
}

}  // namespace

Json buildExportRecord(const Session& session, const ExportOptions& options) {
    Json record = sessionToJson(session);
    record.set("recordFormat", "vfcr.correction-record/v1");
    record.set("recordEncoding", "UTF-8");
    record.set("sourceSessionSchema", session.schema);

    Json tool = Json::object();
    tool.set("name", "VFC Replay / Local Correction Workbench");
    tool.set("version", kToolVersion);
    tool.set("language", "C++17");
    tool.set("graphicsApi", "Vulkan");
    tool.set("windowing", "GLFW");
    tool.set("deterministicRerun", true);
    record.set("tool", std::move(tool));

    const ReplayObject* focusObject = nullptr;
    for (const ReplayObject& object : session.objects) {
        if (!options.focusObjectId.empty() && object.id == options.focusObjectId) {
            focusObject = &object;
            break;
        }
    }
    Json scope = Json::object();
    scope.set("kind", focusObject == nullptr ? "FULL_SESSION" : "SELECTED_OBJECT");
    scope.set("focusObjectId", focusObject == nullptr ? "" : focusObject->id);
    scope.set("focusLabel", focusObject == nullptr ? "" : focusObject->label);
    scope.set("containsAllObjects", true);
    scope.set("note", focusObject == nullptr
                         ? "Complete session evidence bundle."
                         : "Selected object is the review focus; all objects remain embedded for context.");
    record.set("exportScope", std::move(scope));

    Json clock = Json::object();
    clock.set("deviceA", session.clock.deviceA);
    clock.set("deviceB", session.clock.deviceB);
    clock.set("offsetBMinusAMs", session.clock.offsetMs);
    clock.set("correction", "sampleA = sampleB - offset");
    clock.set("method", session.clock.method);
    clock.set("confidence", session.clock.confidence);
    clock.set("baselineSampleA", session.clock.baselineSampleA);
    clock.set("rawBWindowStart", session.missing.startA + session.clock.offsetMs / 1000.0);
    clock.set("rawBWindowEnd", session.missing.endA + session.clock.offsetMs / 1000.0);
    record.set("clockReview", std::move(clock));

    Json missing = Json::object();
    missing.set("id", session.missing.id);
    missing.set("startA", session.missing.startA);
    missing.set("endA", session.missing.endA);
    missing.set("durationSec", session.missing.endA - session.missing.startA);
    missing.set("source", session.missing.source);
    missing.set("reason", session.missing.reason);
    missing.set("recoveryPolicy", "PER_OBJECT_RERUN_AT_CORRECTED_A_TIME");
    record.set("missingFragmentReview", std::move(missing));

    Json reload = Json::object();
    reload.set("preserveFieldOrder", true);
    reload.set("objectFieldOrder", fieldOrderJson(objectFieldOrder()));
    reload.set("runFieldOrder", fieldOrderJson(runFieldOrder()));
    reload.set("evidenceFieldOrder", fieldOrderJson(evidenceFieldOrder()));
    reload.set("sessionFieldOrder", fieldOrderJson(sessionFieldOrder()));
    record.set("reloadContract", std::move(reload));

    Json screenshot = Json::object();
    screenshot.set("file", options.screenshotFile);
    screenshot.set("available", !options.screenshotSha256.empty());
    screenshot.set("sha256", options.screenshotSha256);
    screenshot.set("width", options.screenshotWidth);
    screenshot.set("height", options.screenshotHeight);
    screenshot.set("format", "PPM/P6");
    record.set("screenshot", std::move(screenshot));

    Json assertions = Json::array();
    assertions.push(assertion("clock.window.valid", session.missing.endA > session.missing.startA,
                              "missing fragment has positive duration"));
    assertions.push(assertion("clock.offset.bounded", std::abs(session.clock.offsetMs) <= 2000.0,
                              "B-A offset is within +/-2000 ms"));
    assertions.push(assertion("evidence.chain.head", true,
                              "head=" + evidenceHead(session.evidence)));
    assertions.push(assertion("replay.object.count",
                              session.objects.size() == 6U,
                              "objects=" + std::to_string(session.objects.size())));
    assertions.push(assertion("export.scope.focus",
                              options.focusObjectId.empty() || focusObject != nullptr,
                              focusObject == nullptr ? "full session"
                                                     : "object=" + focusObject->id));

    for (std::size_t objectIndex = 0; objectIndex < session.objects.size(); ++objectIndex) {
        const ReplayObject& object = session.objects[objectIndex];
        if (object.baselineKnown) {
            const CullEvaluation baseline =
                evaluateCulling(session.camera, object, session.clock.baselineSampleA);
            assertions.push(assertion(
                "baseline." + object.id,
                baseline.visible == object.baselineVisible,
                "expected=" + boolText(object.baselineVisible) +
                    " recomputed=" + boolText(baseline.visible)));
        } else {
            assertions.push(assertion("baseline." + object.id, true,
                                      "baseline unavailable inside missing fragment"));
        }
        for (const RunRecord& run : object.runs) {
            const CullEvaluation evaluation =
                evaluateCulling(session.camera, object, run.sampleA);
            const std::string expectedHash =
                runInputHash(session.camera, object, run.sampleA, run.clockOffsetMs);
            assertions.push(assertion(
                "run." + object.id + "." + std::to_string(run.sequence) + ".visibility",
                evaluation.visible == run.visible,
                "expected=" + boolText(run.visible) +
                    " recomputed=" + boolText(evaluation.visible)));
            assertions.push(assertion(
                "run." + object.id + "." + std::to_string(run.sequence) + ".inputHash",
                expectedHash == run.inputHash, "sha256=" + expectedHash));
        }
    }
    record.set("assertions", std::move(assertions));

    Json checks = Json::object();
    checks.set("command", "vfcr-verify <record.json>");
    checks.set("independent", true);
    checks.set("requirements", "No session file, GPU, or GUI state is required.");
    checks.set("canonicalization", "vfcr-json-v1 ordered objects, UTF-8, no insignificant space");
    record.set("verification", std::move(checks));

    Json manifest = Json::object();
    manifest.set("record", "record.json");
    manifest.set("screenshot", options.screenshotSha256.empty() ? Json() : Json(options.screenshotFile));
    manifest.set("screenshotSha256", options.screenshotSha256);
    manifest.set("recordSha256", "see integrity.recordSha256");
    record.set("bundle", std::move(manifest));

    std::string hashSource = canonicalJson(record);
    const std::string recordHash = sha256(hashSource);
    Json integrity = Json::object();
    integrity.set("algorithm", "SHA-256");
    integrity.set("canonicalization", "vfcr-json-v1");
    integrity.set("recordSha256", recordHash);
    record.set("integrity", std::move(integrity));
    return record;
}

bool writeExportBundle(Session& session, const ExportOptions& options, std::string* recordPath,
                       std::string* error) {
    if (!options.focusObjectId.empty()) {
        bool found = false;
        for (const ReplayObject& object : session.objects) {
            found = found || object.id == options.focusObjectId;
        }
        if (!found) {
            if (error != nullptr) {
                *error = "focus object not found: " + options.focusObjectId;
            }
            return false;
        }
    }
    if (!ensureDirectory(options.directory, error)) {
        return false;
    }
    const std::string path = pathJoin(options.directory, "record.json");
    const std::string at = nowIso8601Utc();
    std::ostringstream detail;
    detail << "BUNDLE " << fileName(options.directory) << " / OFFSET "
           << (session.clock.offsetMs >= 0.0 ? "+" : "") << session.clock.offsetMs
           << "ms / focus " << (options.focusObjectId.empty() ? "SESSION" : options.focusObjectId)
           << " / SCREENSHOT " << (options.screenshotSha256.empty()
                                         ? "UNAVAILABLE"
                                         : shortDigest(options.screenshotSha256, 12));
    appendEvidence(session, "EXPORT_RECORD", "", detail.str(), at);
    ++session.exportCount;
    session.lastExport = options.directory;
    session.updatedAt = at;

    const Json record = buildExportRecord(session, options);
    const std::string serialized = record.dump(true) + "\n";
    if (!writeTextFileAtomic(path, serialized, error)) {
        return false;
    }

    const std::string hash = pathFind(record, {"integrity", "recordSha256"})->asString();
    std::ostringstream review;
    review << "# VFC Replay correction record\n\n"
           << "This bundle is self-contained. It does not require the workstation session.\n\n"
           << "- Record: `record.json`\n"
           << "- Screenshot: `" << options.screenshotFile << "`\n"
           << "- Record SHA-256: `" << hash << "`\n"
           << "- Evidence head: `" << evidenceHead(session.evidence) << "`\n"
           << "- Export scope: `" << (options.focusObjectId.empty() ? "FULL_SESSION"
                                                                   : "SELECTED_OBJECT " +
                                                                         options.focusObjectId)
           << "`\n"
           << "- Clock offset B-A: `" << session.clock.offsetMs << " ms`\n"
           << "- Missing fragment A: `[" << session.missing.startA << ", "
           << session.missing.endA << "] s`\n\n"
           << "## Independent review\n\n"
           << "```sh\n"
           << "vfcr-verify record.json\n"
           << "```\n\n"
           << "The verifier checks field order, run order, frustum visibility, per-run input "
              "hashes, the evidence chain, and the record SHA-256. Recompute the screenshot "
              "digest with `sha256sum " << options.screenshotFile << "` when available.\n";
    if (!writeTextFileAtomic(pathJoin(options.directory, "REVIEW.md"), review.str(), error)) {
        return false;
    }

    if (recordPath != nullptr) {
        *recordPath = path;
    }
    return true;
}

bool loadExportRecord(const std::string& path, Session& session, std::string* error) {
    std::string text;
    if (!readTextFile(path, text, error)) {
        return false;
    }
    std::string parseError;
    Json document = Json::parse(text, &parseError);
    if (!parseError.empty()) {
        if (error != nullptr) {
            *error = "invalid JSON: " + parseError;
        }
        return false;
    }
    const Json* format = document.find("recordFormat");
    if (format == nullptr || format->stringOr("") != "vfcr.correction-record/v1") {
        if (error != nullptr) {
            *error = "not a vfcr.correction-record/v1 artifact";
        }
        return false;
    }
    const Json* objects = document.find("objects");
    if (objects == nullptr || !objects->isArray()) {
        if (error != nullptr) {
            *error = "artifact has no objects array";
        }
        return false;
    }
    for (const Json& object : objects->arrayItems()) {
        std::string orderError;
        if (!validateObjectFieldOrder(object, &orderError)) {
            if (error != nullptr) {
                *error = orderError;
            }
            return false;
        }
    }
    const VerifyResult verified = verifyExportDocument(document, parentPath(path));
    if (!verified.ok) {
        if (error != nullptr) {
            *error = verified.failures.empty() ? "artifact verification failed"
                                               : verified.failures.front();
        }
        return false;
    }
    session = sessionFromJson(document);
    session.schema = pathFind(document, {"sourceSessionSchema"}) != nullptr
                         ? pathFind(document, {"sourceSessionSchema"})->stringOr(kSchema)
                         : kSchema;
    return true;
}

VerifyResult verifyExportDocument(const Json& document, const std::string& baseDirectory) {
    VerifyResult result;
    auto add = [&result](const std::string& id, bool passed, const std::string& detail) {
        result.checks.push_back(makeCheck(id, passed, detail));
        if (!passed) {
            result.failures.push_back(id + ": " + detail);
        }
        return passed;
    };

    const Json* format = document.find("recordFormat");
    add("record.format", format != nullptr &&
                             format->stringOr("") == "vfcr.correction-record/v1",
        format == nullptr ? "recordFormat missing" : format->stringOr(""));

    const Json* contract = document.find("reloadContract");
    bool orderContract = contract != nullptr;
    if (contract != nullptr) {
        auto checkOrderArray = [&](const char* key, const std::vector<std::string>& expected) {
            const Json* array = contract->find(key);
            if (array == nullptr || !array->isArray() || array->size() != expected.size()) {
                return false;
            }
            for (std::size_t i = 0; i < expected.size(); ++i) {
                if (array->arrayItems()[i].stringOr("") != expected[i]) {
                    return false;
                }
            }
            return true;
        };
        orderContract = checkOrderArray("objectFieldOrder", objectFieldOrder()) &&
                        checkOrderArray("runFieldOrder", runFieldOrder()) &&
                        checkOrderArray("evidenceFieldOrder", evidenceFieldOrder()) &&
                        checkOrderArray("sessionFieldOrder", sessionFieldOrder()) &&
                        contract->find("preserveFieldOrder") != nullptr &&
                        contract->find("preserveFieldOrder")->asBool(false);
    }
    add("reload.contract", orderContract,
        orderContract ? "field order contract is intact"
                      : "reload contract or field-order arrays are invalid");

    const Json* objectsDocument = document.find("objects");
    const bool hasObjects = objectsDocument != nullptr && objectsDocument->isArray();
    if (hasObjects) {
        bool objectsOrdered = true;
        for (const Json& object : objectsDocument->arrayItems()) {
            std::string orderError;
            if (!validateObjectFieldOrder(object, &orderError)) {
                objectsOrdered = false;
                result.failures.push_back("object.order: " + orderError);
                break;
            }
            if (const Json* runs = object.find("runs"); runs != nullptr && runs->isArray()) {
                int expected = 1;
                for (const Json& run : runs->arrayItems()) {
                    std::string runOrderError;
                    if (!validateOrder(run, runFieldOrder(), "run", runOrderError)) {
                        objectsOrdered = false;
                        result.failures.push_back("run.order: " + runOrderError);
                        break;
                    }
                    if (run.find("sequence") == nullptr ||
                        static_cast<int>(run.find("sequence")->asInt()) != expected) {
                        objectsOrdered = false;
                        result.failures.push_back(
                            "run.sequence: expected " + std::to_string(expected));
                        break;
                    }
                    ++expected;
                }
            }
            if (!objectsOrdered) {
                break;
            }
        }
        add("reload.object-and-run-order", objectsOrdered,
            objectsOrdered ? "all object and run fields retain canonical order"
                           : "object or run field/order changed");
    } else {
        add("reload.object-and-run-order", false, "objects array missing");
    }

    Session session = sessionFromJson(document);
    add("replay.object-count", session.objects.size() == 6U,
        "objects=" + std::to_string(session.objects.size()));

    const Json* scope = document.find("exportScope");
    bool scopeOk = true;
    std::string scopeDetail = "FULL_SESSION";
    if (scope != nullptr) {
        const std::string focus = scope->find("focusObjectId") != nullptr
                                      ? scope->find("focusObjectId")->stringOr("")
                                      : "";
        if (!focus.empty()) {
            scopeOk = false;
            scopeDetail = "focus object missing: " + focus;
            for (const ReplayObject& object : session.objects) {
                if (object.id == focus) {
                    scopeOk = true;
                    scopeDetail = "SELECTED_OBJECT " + focus;
                    break;
                }
            }
        }
    }
    add("export.scope", scopeOk, scopeDetail);

    std::string chainError;
    const bool chainOk = verifyEvidenceChain(session.evidence, &chainError);
    result.evidenceHead = evidenceHead(session.evidence);
    add("evidence.chain", chainOk,
        chainOk ? "verified " + std::to_string(session.evidence.size()) + " entries"
                : chainError);

    bool recomputed = true;
    for (const ReplayObject& object : session.objects) {
        if (object.baselineKnown) {
            const CullEvaluation baseline =
                evaluateCulling(session.camera, object, session.clock.baselineSampleA);
            if (baseline.visible != object.baselineVisible) {
                recomputed = false;
                result.failures.push_back(
                    "baseline." + object.id + ": expected " +
                    boolText(object.baselineVisible) + " recomputed " +
                    boolText(baseline.visible));
            }
        }
        int runSequence = 1;
        for (const RunRecord& run : object.runs) {
            if (run.sequence != runSequence) {
                recomputed = false;
                result.failures.push_back("run." + object.id + ": sequence discontinuity");
            }
            const CullEvaluation evaluation =
                evaluateCulling(session.camera, object, run.sampleA);
            if (evaluation.visible != run.visible) {
                recomputed = false;
                result.failures.push_back(
                    "run." + object.id + "." + std::to_string(run.sequence) +
                    ": visibility mismatch");
            }
            const std::string inputHash =
                runInputHash(session.camera, object, run.sampleA, run.clockOffsetMs);
            if (inputHash != run.inputHash) {
                recomputed = false;
                result.failures.push_back(
                    "run." + object.id + "." + std::to_string(run.sequence) +
                    ": input hash mismatch");
            }
            ++runSequence;
        }
    }
    add("replay.deterministic-results", recomputed,
        recomputed ? "all baseline/run results recompute from scene inputs"
                   : "one or more recomputed results differ");

    const Json* assertions = document.find("assertions");
    bool assertionsPassed = assertions != nullptr && assertions->isArray();
    int assertionCount = 0;
    if (assertionsPassed) {
        for (const Json& item : assertions->arrayItems()) {
            ++assertionCount;
            if (item.find("status") == nullptr ||
                item.find("status")->stringOr("") != "PASS") {
                assertionsPassed = false;
                break;
            }
        }
    }
    add("record.assertions", assertionsPassed,
        "PASS assertions=" + std::to_string(assertionCount));

    const Json* integrity = document.find("integrity");
    bool hashOk = false;
    if (integrity != nullptr && integrity->find("algorithm") != nullptr &&
        integrity->find("algorithm")->stringOr("") == "SHA-256" &&
        integrity->find("recordSha256") != nullptr) {
        result.recordSha256 = integrity->find("recordSha256")->stringOr("");
        Json unsignedDocument = document;
        unsignedDocument.erase("integrity");
        hashOk = sha256(canonicalJson(unsignedDocument)) == result.recordSha256;
    }
    add("record.sha256", hashOk,
        hashOk ? result.recordSha256 : "record hash missing or does not match");

    const Json* screenshot = document.find("screenshot");
    if (screenshot != nullptr && screenshot->find("available") != nullptr &&
        screenshot->find("available")->asBool(false)) {
        bool screenshotOk = true;
        std::string detail = "digest recorded";
        if (!baseDirectory.empty() && screenshot->find("file") != nullptr &&
            screenshot->find("sha256") != nullptr) {
            const std::string filePath =
                pathJoin(baseDirectory, screenshot->find("file")->stringOr(""));
            std::string bytes;
            std::string readError;
            if (!readTextFile(filePath, bytes, &readError)) {
                screenshotOk = false;
                detail = readError;
            } else {
                const std::string expected = screenshot->find("sha256")->stringOr("");
                const std::string actual = sha256(bytes);
                screenshotOk = actual == expected;
                detail = screenshotOk ? "sha256=" + actual : "screenshot digest mismatch";
            }
        }
        add("screenshot.integrity", screenshotOk, detail);
    }

    result.ok = result.failures.empty();
    return result;
}

VerifyResult verifyExportFile(const std::string& path) {
    VerifyResult result;
    std::string text;
    std::string error;
    if (!readTextFile(path, text, &error)) {
        result.failures.push_back(error);
        return result;
    }
    std::string parseError;
    const Json document = Json::parse(text, &parseError);
    if (!parseError.empty()) {
        result.failures.push_back("invalid JSON: " + parseError);
        return result;
    }
    return verifyExportDocument(document, parentPath(path));
}

}  // namespace vfcr
