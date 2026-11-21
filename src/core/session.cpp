#include "core/session.hpp"

#include "core/hash.hpp"
#include "core/io.hpp"

#include <cstdlib>

namespace vfcr {

namespace {

bool validateFieldOrderImpl(const Json& document, const std::vector<std::string>& expected,
                            const std::string& label, std::string* error) {
    if (!document.isObject()) {
        if (error != nullptr) {
            *error = label + " is not an object";
        }
        return false;
    }
    const auto& actual = document.objectItems();
    if (actual.size() < expected.size()) {
        if (error != nullptr) {
            *error = label + " has missing fields";
        }
        return false;
    }
    for (std::size_t i = 0; i < expected.size() && i < actual.size(); ++i) {
        if (actual[i].first != expected[i]) {
            if (error != nullptr) {
                *error = label + " field order mismatch at index " + std::to_string(i) +
                         ": expected '" + expected[i] + "', found '" + actual[i].first + "'";
            }
            return false;
        }
    }
    return true;
}

}  // namespace

std::string defaultStateDirectory() {
    if (const char* configured = std::getenv("VFCR_STATE_DIR"); configured != nullptr &&
        configured[0] != '\0') {
        return configured;
    }
    return ".vfcr";
}

std::string defaultStatePath() {
    return pathJoin(defaultStateDirectory(), "session.json");
}

bool saveSessionFile(const std::string& path, Session& session, std::string* error) {
    session.updatedAt = nowIso8601Utc();
    const std::string serialized = sessionToJson(session).dump(true) + "\n";
    return writeTextFileAtomic(path, serialized, error);
}

bool loadSessionFile(const std::string& path, Session& session, std::string* error) {
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
    if (const Json* objects = document.find("objects"); objects != nullptr && objects->isArray()) {
        for (const Json& object : objects->arrayItems()) {
            if (!validateObjectFieldOrder(object, error)) {
                return false;
            }
        }
    }
    session = sessionFromJson(document);
    if (session.objects.empty()) {
        if (error != nullptr) {
            *error = "record contains no render objects";
        }
        return false;
    }
    std::string chainError;
    if (!verifyEvidenceChain(session.evidence, &chainError)) {
        if (error != nullptr) {
            *error = "evidence chain invalid: " + chainError;
        }
        return false;
    }
    return true;
}

bool validateObjectFieldOrder(const Json& objectDocument, std::string* error) {
    return validateFieldOrderImpl(objectDocument, objectFieldOrder(), "object", error);
}

std::string sessionContentDigest(const Session& session) {
    return sha256(canonicalJson(sessionToJson(session)));
}

}  // namespace vfcr
