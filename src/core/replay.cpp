#include "core/replay.hpp"

#include "core/hash.hpp"
#include "core/io.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace vfcr {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTimelineDuration = 30.0;

double clampDouble(double value, double low, double high) {
    return std::max(low, std::min(high, value));
}

double toRadians(double degrees) {
    return degrees * kPi / 180.0;
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

Vec3 multiply(const Vec3& value, double scale) {
    return {value.x * scale, value.y * scale, value.z * scale};
}

double dot(const Vec3& a, const Vec3& b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

Vec3 normalized(const Vec3& value) {
    const double length = std::sqrt(dot(value, value));
    if (length <= 1e-12) {
        return {0.0, 0.0, 1.0};
    }
    return multiply(value, 1.0 / length);
}

struct Plane {
    Vec3 normal;
    double distance = 0.0;
};

double planeValue(const Plane& plane, const Vec3& point) {
    return dot(plane.normal, point) + plane.distance;
}

std::string visibilityName(bool visible) {
    return visible ? "VISIBLE" : "CULLED";
}

Json doubleArray(const Vec3& value) {
    Json array = Json::array();
    array.push(value.x);
    array.push(value.y);
    array.push(value.z);
    return array;
}

Vec3 vec3FromArray(const Json* value, const Vec3& fallback) {
    if (value == nullptr || !value->isArray() || value->size() < 3U) {
        return fallback;
    }
    const auto& items = value->arrayItems();
    return {items[0].asNumber(fallback.x), items[1].asNumber(fallback.y),
            items[2].asNumber(fallback.z)};
}

}  // namespace

Json vec3ToJson(const Vec3& value) {
    return doubleArray(value);
}

Vec3 vec3FromJson(const Json& value, const Vec3& fallback) {
    return vec3FromArray(&value, fallback);
}

Json cameraToJson(const Camera& camera) {
    Json value = Json::object();
    value.set("position", doubleArray(camera.position));
    value.set("yawDeg", camera.yawDeg);
    value.set("pitchDeg", camera.pitchDeg);
    value.set("fovYDeg", camera.fovYDeg);
    value.set("nearZ", camera.nearZ);
    value.set("farZ", camera.farZ);
    value.set("aspect", camera.aspect);
    return value;
}

Camera cameraFromJson(const Json& value) {
    Camera camera;
    camera.position = vec3FromArray(value.find("position"), camera.position);
    camera.yawDeg = value.find("yawDeg") != nullptr ? value.find("yawDeg")->asNumber(camera.yawDeg) : camera.yawDeg;
    camera.pitchDeg = value.find("pitchDeg") != nullptr ? value.find("pitchDeg")->asNumber(camera.pitchDeg) : camera.pitchDeg;
    camera.fovYDeg = value.find("fovYDeg") != nullptr ? value.find("fovYDeg")->asNumber(camera.fovYDeg) : camera.fovYDeg;
    camera.nearZ = value.find("nearZ") != nullptr ? value.find("nearZ")->asNumber(camera.nearZ) : camera.nearZ;
    camera.farZ = value.find("farZ") != nullptr ? value.find("farZ")->asNumber(camera.farZ) : camera.farZ;
    camera.aspect = value.find("aspect") != nullptr ? value.find("aspect")->asNumber(camera.aspect) : camera.aspect;
    return camera;
}

Vec3 objectPositionAt(const ReplayObject& object, double sampleA) {
    const double period = std::max(0.001, object.motion.periodSec);
    const double wave = std::sin((kPi * 2.0 * sampleA / period) + object.motion.phaseRad);
    return add(object.basePosition, multiply(object.motion.amplitude, wave));
}

CullEvaluation evaluateCulling(const Camera& camera, const ReplayObject& object, double sampleA) {
    const double yaw = toRadians(camera.yawDeg);
    const double pitch = toRadians(camera.pitchDeg);
    const Vec3 forward = normalized(
        {std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)});
    const Vec3 worldUp{0.0, 1.0, 0.0};
    const Vec3 right = normalized(cross(worldUp, forward));
    const Vec3 up = normalized(cross(forward, right));
    const double tanY = std::tan(toRadians(camera.fovYDeg) * 0.5);
    const double tanX = tanY * camera.aspect;

    const Vec3 cameraToForward = {dot(forward, camera.position), 0.0, 0.0};
    (void)cameraToForward;
    const double camForward = dot(forward, camera.position);
    const double camRight = dot(right, camera.position);
    const double camUp = dot(up, camera.position);

    const Plane planes[6] = {
        {forward, -camForward - camera.nearZ},
        {multiply(forward, -1.0), camForward + camera.farZ},
        {add(multiply(right, -1.0), multiply(forward, tanX)),
         camRight - tanX * camForward},
        {add(right, multiply(forward, tanX)), -camRight - tanX * camForward},
        {add(multiply(up, -1.0), multiply(forward, tanY)),
         camUp - tanY * camForward},
        {add(up, multiply(forward, tanY)), -camUp - tanY * camForward},
    };

    const Vec3 center = objectPositionAt(object, sampleA);
    bool visible = true;
    for (const Plane& plane : planes) {
        bool anyInside = false;
        for (int sx = -1; sx <= 1; sx += 2) {
            for (int sy = -1; sy <= 1; sy += 2) {
                for (int sz = -1; sz <= 1; sz += 2) {
                    const Vec3 corner{
                        center.x + static_cast<double>(sx) * object.halfExtents.x,
                        center.y + static_cast<double>(sy) * object.halfExtents.y,
                        center.z + static_cast<double>(sz) * object.halfExtents.z,
                    };
                    if (planeValue(plane, corner) >= 0.0) {
                        anyInside = true;
                    }
                }
            }
        }
        if (!anyInside) {
            visible = false;
            break;
        }
    }

    const double relativeX = center.x - camera.position.x;
    const double relativeZ = center.z - camera.position.z;
    const double distance = std::sqrt(relativeX * relativeX + relativeZ * relativeZ);
    CullEvaluation result;
    result.visible = visible;
    result.distance = distance;
    result.costMs = 0.09 + static_cast<double>(object.triangles) * 0.00085 +
                    (visible ? 0.11 : 0.025);
    result.decision = visible ? "INSIDE_FRUSTUM" : "OUTSIDE_PLANE";
    return result;
}

std::string runInputHash(const Camera& camera, const ReplayObject& object, double sampleA,
                         double clockOffsetMs) {
    Json input = Json::object();
    input.set("camera", cameraToJson(camera));
    input.set("objectId", object.id);
    input.set("sampleA", sampleA);
    input.set("clockOffsetMs", clockOffsetMs);
    input.set("position", doubleArray(objectPositionAt(object, sampleA)));
    input.set("halfExtents", doubleArray(object.halfExtents));
    return sha256(canonicalJson(input));
}

RunRecord rerunObject(Session& session, std::size_t objectIndex, double sampleA,
                      const std::string& startedAt) {
    if (objectIndex >= session.objects.size()) {
        return {};
    }
    ReplayObject& object = session.objects[objectIndex];
    const double sample = clampDouble(sampleA, 0.0, kTimelineDuration);
    const CullEvaluation evaluation = evaluateCulling(session.camera, object, sample);
    const bool insideMissing = sample >= session.missing.startA && sample <= session.missing.endA;

    RunRecord run;
    run.sequence = static_cast<int>(object.runs.size()) + 1;
    run.startedAt = startedAt;
    run.sampleA = sample;
    run.clockOffsetMs = session.clock.offsetMs;
    run.visible = evaluation.visible;
    run.baselineKnown = object.baselineKnown;
    run.changed = object.baselineKnown && run.visible != object.baselineVisible;
    run.costMs = evaluation.costMs;
    run.cause = insideMissing ? "MISSING_SEGMENT_RECOVERY" : "MANUAL_REPLAY";
    run.inputHash = runInputHash(session.camera, object, sample, session.clock.offsetMs);
    object.runs.push_back(run);
    session.compareSequence = run.sequence;
    session.updatedAt = startedAt;

    std::ostringstream detail;
    detail << "RUN " << (run.sequence < 10 ? "0" : "") << run.sequence << " / "
           << visibilityName(run.visible) << " / T=" << sample << "s / "
           << shortDigest(run.inputHash, 10);
    appendEvidence(session, insideMissing ? "RERUN_RECOVERY" : "RERUN_OBJECT", object.id,
                   detail.str(), startedAt);
    return run;
}

void rerunAll(Session& session, double sampleA, const std::string& startedAt) {
    for (std::size_t i = 0; i < session.objects.size(); ++i) {
        rerunObject(session, i, sampleA, startedAt);
    }
}

void adjustClockOffset(Session& session, double deltaMs, const std::string& at) {
    session.clock.offsetMs = clampDouble(session.clock.offsetMs + deltaMs, -2000.0, 2000.0);
    session.updatedAt = at;
    std::ostringstream detail;
    detail << "B-A OFFSET " << (session.clock.offsetMs >= 0.0 ? "+" : "")
           << session.clock.offsetMs << "ms";
    appendEvidence(session, "CLOCK_ADJUST", "", detail.str(), at);
}

void shiftMissingFragment(Session& session, double deltaSeconds, const std::string& at) {
    const double duration = std::max(0.1, session.missing.endA - session.missing.startA);
    session.missing.startA =
        clampDouble(session.missing.startA + deltaSeconds, 0.0, kTimelineDuration - duration);
    session.missing.endA = session.missing.startA + duration;
    session.updatedAt = at;
    std::ostringstream detail;
    detail << "GAP " << session.missing.id << " MOVED TO [" << session.missing.startA << ", "
           << session.missing.endA << "]s / EXISTING RUNS KEPT";
    appendEvidence(session, "GAP_ADJUST", "", detail.str(), at);
}

void appendEvidence(Session& session, const std::string& kind, const std::string& objectId,
                    const std::string& detail, const std::string& at) {
    EvidenceEntry entry;
    entry.sequence = static_cast<int>(session.evidence.size()) + 1;
    entry.at = at;
    entry.kind = kind;
    entry.objectId = objectId;
    entry.detail = detail;
    entry.previous = evidenceHead(session.evidence);
    Json core = Json::object();
    core.set("sequence", entry.sequence);
    core.set("at", entry.at);
    core.set("kind", entry.kind);
    core.set("objectId", entry.objectId);
    core.set("detail", entry.detail);
    core.set("previous", entry.previous);
    entry.digest = sha256(canonicalJson(core) + entry.previous);
    session.evidence.push_back(std::move(entry));
}

bool verifyEvidenceChain(const std::vector<EvidenceEntry>& entries, std::string* error) {
    std::string previous(64, '0');
    int expectedSequence = 1;
    for (const EvidenceEntry& entry : entries) {
        if (entry.sequence != expectedSequence) {
            if (error != nullptr) {
                *error = "evidence sequence discontinuity at " + std::to_string(entry.sequence);
            }
            return false;
        }
        if (entry.previous != previous) {
            if (error != nullptr) {
                *error = "evidence previous digest mismatch at sequence " +
                         std::to_string(entry.sequence);
            }
            return false;
        }
        Json core = Json::object();
        core.set("sequence", entry.sequence);
        core.set("at", entry.at);
        core.set("kind", entry.kind);
        core.set("objectId", entry.objectId);
        core.set("detail", entry.detail);
        core.set("previous", entry.previous);
        const std::string expected = sha256(canonicalJson(core) + entry.previous);
        if (expected != entry.digest) {
            if (error != nullptr) {
                *error = "evidence digest mismatch at sequence " +
                         std::to_string(entry.sequence);
            }
            return false;
        }
        previous = entry.digest;
        ++expectedSequence;
    }
    return true;
}

std::string evidenceHead(const std::vector<EvidenceEntry>& entries) {
    return entries.empty() ? std::string(64, '0') : entries.back().digest;
}

Session makeDefaultSession() {
    Session session;
    session.createdAt = nowIso8601Utc();
    session.updatedAt = session.createdAt;

    auto addObject = [&session](const char* id, const char* label, const char* kind,
                                Vec3 position, Vec3 halfExtents, Motion motion, int triangles,
                                bool baselineKnown) {
        ReplayObject object;
        object.id = id;
        object.label = label;
        object.kind = kind;
        object.basePosition = position;
        object.halfExtents = halfExtents;
        object.motion = motion;
        object.triangles = triangles;
        object.baselineKnown = baselineKnown;
        session.objects.push_back(std::move(object));
    };

    addObject("OBJ-731", "CRATE_03", "PROP", {3.5, 1.0, 8.0}, {1.0, 1.0, 1.0},
              {{0.8, 0.0, 1.5}, 19.0, 0.4}, 1248, true);
    addObject("OBJ-204", "DRONE_12", "DYNAMIC", {-7.0, 1.5, 14.0}, {1.5, 1.0, 1.5},
              {{-1.2, 0.4, 1.2}, 17.0, 1.0}, 2864, true);
    addObject("OBJ-988", "GATE_07", "STRUCTURE", {13.0, 1.0, 17.0}, {2.0, 2.0, 1.0},
              {{1.5, 0.0, 0.5}, 22.0, 2.0}, 5320, true);
    addObject("OBJ-112", "BEACON_04", "SIGNAL", {5.0, 1.2, 12.0}, {0.8, 2.0, 0.8},
              {{0.6, 0.0, 1.0}, 15.0, 0.7}, 880, false);
    addObject("OBJ-553", "LAMP_09", "PROP", {-24.0, 1.0, 24.0}, {1.0, 1.0, 1.0},
              {{1.0, 0.0, 1.0}, 18.0, 0.2}, 640, true);
    addObject("OBJ-067", "PYLON_21", "STRUCTURE", {10.0, 1.0, 43.0}, {2.0, 3.0, 2.0},
              {{2.0, 0.0, 1.0}, 16.0, 2.5}, 4120, true);

    for (ReplayObject& object : session.objects) {
        if (!object.baselineKnown) {
            object.baselineCostMs = 0.0;
            continue;
        }
        const CullEvaluation baseline =
            evaluateCulling(session.camera, object, session.clock.baselineSampleA);
        object.baselineVisible = baseline.visible;
        object.baselineCostMs = baseline.costMs;
    }

    session.selectedObject = 3;
    session.compareSequence = 0;
    appendEvidence(session, "SESSION_CREATED", "",
                   "TRACE LOADED / 6 OBJECTS / CLOCK OFFSET +420ms", session.createdAt);
    const RunRecord recovery =
        rerunObject(session, 3U, session.playhead, session.createdAt);
    session.compareSequence = recovery.sequence;
    return session;
}

Json objectToJson(const ReplayObject& object) {
    Json value = Json::object();
    value.set("id", object.id);
    value.set("label", object.label);
    value.set("kind", object.kind);
    value.set("basePosition", doubleArray(object.basePosition));
    value.set("halfExtents", doubleArray(object.halfExtents));
    Json motion = Json::object();
    motion.set("amplitude", doubleArray(object.motion.amplitude));
    motion.set("periodSec", object.motion.periodSec);
    motion.set("phaseRad", object.motion.phaseRad);
    value.set("motion", std::move(motion));
    value.set("triangles", object.triangles);
    value.set("baselineKnown", object.baselineKnown);
    value.set("baselineVisible", object.baselineVisible);
    value.set("baselineCostMs", object.baselineCostMs);
    Json runs = Json::array();
    for (const RunRecord& run : object.runs) {
        runs.push(runToJson(run));
    }
    value.set("runs", std::move(runs));
    return value;
}

ReplayObject objectFromJson(const Json& value) {
    ReplayObject object;
    object.id = value.find("id") != nullptr ? value.find("id")->stringOr(object.id) : object.id;
    object.label = value.find("label") != nullptr ? value.find("label")->stringOr(object.label) : object.label;
    object.kind = value.find("kind") != nullptr ? value.find("kind")->stringOr(object.kind) : object.kind;
    object.basePosition = vec3FromArray(value.find("basePosition"), object.basePosition);
    object.halfExtents = vec3FromArray(value.find("halfExtents"), object.halfExtents);
    if (const Json* motion = value.find("motion"); motion != nullptr) {
        object.motion.amplitude = vec3FromArray(motion->find("amplitude"), object.motion.amplitude);
        if (const Json* item = motion->find("periodSec"); item != nullptr) {
            object.motion.periodSec = item->asNumber(object.motion.periodSec);
        }
        if (const Json* item = motion->find("phaseRad"); item != nullptr) {
            object.motion.phaseRad = item->asNumber(object.motion.phaseRad);
        }
    }
    if (const Json* item = value.find("triangles"); item != nullptr) {
        object.triangles = static_cast<int>(item->asInt(object.triangles));
    }
    if (const Json* item = value.find("baselineKnown"); item != nullptr) {
        object.baselineKnown = item->asBool(object.baselineKnown);
    }
    if (const Json* item = value.find("baselineVisible"); item != nullptr) {
        object.baselineVisible = item->asBool(object.baselineVisible);
    }
    if (const Json* item = value.find("baselineCostMs"); item != nullptr) {
        object.baselineCostMs = item->asNumber(object.baselineCostMs);
    }
    if (const Json* runs = value.find("runs"); runs != nullptr && runs->isArray()) {
        object.runs.clear();
        for (const Json& run : runs->arrayItems()) {
            object.runs.push_back(runFromJson(run));
        }
    }
    return object;
}

Json runToJson(const RunRecord& run) {
    Json value = Json::object();
    value.set("sequence", run.sequence);
    value.set("startedAt", run.startedAt);
    value.set("sampleA", run.sampleA);
    value.set("clockOffsetMs", run.clockOffsetMs);
    value.set("visible", run.visible);
    value.set("baselineKnown", run.baselineKnown);
    value.set("changed", run.changed);
    value.set("costMs", run.costMs);
    value.set("cause", run.cause);
    value.set("inputHash", run.inputHash);
    return value;
}

RunRecord runFromJson(const Json& value) {
    RunRecord run;
    if (const Json* item = value.find("sequence"); item != nullptr) {
        run.sequence = static_cast<int>(item->asInt(run.sequence));
    }
    if (const Json* item = value.find("startedAt"); item != nullptr) {
        run.startedAt = item->stringOr(run.startedAt);
    }
    if (const Json* item = value.find("sampleA"); item != nullptr) {
        run.sampleA = item->asNumber(run.sampleA);
    }
    if (const Json* item = value.find("clockOffsetMs"); item != nullptr) {
        run.clockOffsetMs = item->asNumber(run.clockOffsetMs);
    }
    if (const Json* item = value.find("visible"); item != nullptr) {
        run.visible = item->asBool(run.visible);
    }
    if (const Json* item = value.find("baselineKnown"); item != nullptr) {
        run.baselineKnown = item->asBool(run.baselineKnown);
    }
    if (const Json* item = value.find("changed"); item != nullptr) {
        run.changed = item->asBool(run.changed);
    }
    if (const Json* item = value.find("costMs"); item != nullptr) {
        run.costMs = item->asNumber(run.costMs);
    }
    if (const Json* item = value.find("cause"); item != nullptr) {
        run.cause = item->stringOr(run.cause);
    }
    if (const Json* item = value.find("inputHash"); item != nullptr) {
        run.inputHash = item->stringOr(run.inputHash);
    }
    return run;
}

Json evidenceToJson(const EvidenceEntry& entry) {
    Json value = Json::object();
    value.set("sequence", entry.sequence);
    value.set("at", entry.at);
    value.set("kind", entry.kind);
    value.set("objectId", entry.objectId);
    value.set("detail", entry.detail);
    value.set("previous", entry.previous);
    value.set("digest", entry.digest);
    return value;
}

EvidenceEntry evidenceFromJson(const Json& value) {
    EvidenceEntry entry;
    if (const Json* item = value.find("sequence"); item != nullptr) {
        entry.sequence = static_cast<int>(item->asInt(entry.sequence));
    }
    if (const Json* item = value.find("at"); item != nullptr) {
        entry.at = item->stringOr(entry.at);
    }
    if (const Json* item = value.find("kind"); item != nullptr) {
        entry.kind = item->stringOr(entry.kind);
    }
    if (const Json* item = value.find("objectId"); item != nullptr) {
        entry.objectId = item->stringOr(entry.objectId);
    }
    if (const Json* item = value.find("detail"); item != nullptr) {
        entry.detail = item->stringOr(entry.detail);
    }
    if (const Json* item = value.find("previous"); item != nullptr) {
        entry.previous = item->stringOr(entry.previous);
    }
    if (const Json* item = value.find("digest"); item != nullptr) {
        entry.digest = item->stringOr(entry.digest);
    }
    return entry;
}

Json sessionToJson(const Session& session) {
    Json value = Json::object();
    value.set("schema", session.schema);
    value.set("toolVersion", session.toolVersion);
    value.set("sessionId", session.sessionId);
    value.set("sceneId", session.sceneId);
    value.set("frameId", session.frameId);
    value.set("createdAt", session.createdAt);
    value.set("updatedAt", session.updatedAt);
    value.set("camera", cameraToJson(session.camera));

    Json clock = Json::object();
    clock.set("deviceA", session.clock.deviceA);
    clock.set("deviceB", session.clock.deviceB);
    clock.set("offsetMs", session.clock.offsetMs);
    clock.set("method", session.clock.method);
    clock.set("confidence", session.clock.confidence);
    clock.set("baselineSampleA", session.clock.baselineSampleA);
    value.set("clock", std::move(clock));

    Json missing = Json::object();
    missing.set("id", session.missing.id);
    missing.set("startA", session.missing.startA);
    missing.set("endA", session.missing.endA);
    missing.set("reason", session.missing.reason);
    missing.set("source", session.missing.source);
    value.set("missing", std::move(missing));

    Json objects = Json::array();
    for (const ReplayObject& object : session.objects) {
        objects.push(objectToJson(object));
    }
    value.set("objects", std::move(objects));

    Json evidence = Json::array();
    for (const EvidenceEntry& entry : session.evidence) {
        evidence.push(evidenceToJson(entry));
    }
    value.set("evidence", std::move(evidence));
    value.set("selectedObject", session.selectedObject);
    value.set("compareSequence", session.compareSequence);
    value.set("playhead", session.playhead);
    value.set("playing", session.playing);
    value.set("exportCount", session.exportCount);
    value.set("lastExport", session.lastExport);
    return value;
}

Session sessionFromJson(const Json& value) {
    Session session;
    session.schema = value.find("schema") != nullptr ? value.find("schema")->stringOr(session.schema) : session.schema;
    session.toolVersion = value.find("toolVersion") != nullptr ? value.find("toolVersion")->stringOr(session.toolVersion) : session.toolVersion;
    session.sessionId = value.find("sessionId") != nullptr ? value.find("sessionId")->stringOr(session.sessionId) : session.sessionId;
    session.sceneId = value.find("sceneId") != nullptr ? value.find("sceneId")->stringOr(session.sceneId) : session.sceneId;
    session.frameId = value.find("frameId") != nullptr ? value.find("frameId")->stringOr(session.frameId) : session.frameId;
    session.createdAt = value.find("createdAt") != nullptr ? value.find("createdAt")->stringOr(session.createdAt) : session.createdAt;
    session.updatedAt = value.find("updatedAt") != nullptr ? value.find("updatedAt")->stringOr(session.updatedAt) : session.updatedAt;
    if (const Json* camera = value.find("camera"); camera != nullptr) {
        session.camera = cameraFromJson(*camera);
    }
    if (const Json* clock = value.find("clock"); clock != nullptr) {
        if (const Json* item = clock->find("deviceA"); item != nullptr) session.clock.deviceA = item->stringOr(session.clock.deviceA);
        if (const Json* item = clock->find("deviceB"); item != nullptr) session.clock.deviceB = item->stringOr(session.clock.deviceB);
        if (const Json* item = clock->find("offsetMs"); item != nullptr) session.clock.offsetMs = item->asNumber(session.clock.offsetMs);
        if (const Json* item = clock->find("method"); item != nullptr) session.clock.method = item->stringOr(session.clock.method);
        if (const Json* item = clock->find("confidence"); item != nullptr) session.clock.confidence = item->stringOr(session.clock.confidence);
        if (const Json* item = clock->find("baselineSampleA"); item != nullptr) session.clock.baselineSampleA = item->asNumber(session.clock.baselineSampleA);
    }
    if (const Json* missing = value.find("missing"); missing != nullptr) {
        if (const Json* item = missing->find("id"); item != nullptr) session.missing.id = item->stringOr(session.missing.id);
        if (const Json* item = missing->find("startA"); item != nullptr) session.missing.startA = item->asNumber(session.missing.startA);
        if (const Json* item = missing->find("endA"); item != nullptr) session.missing.endA = item->asNumber(session.missing.endA);
        if (const Json* item = missing->find("reason"); item != nullptr) session.missing.reason = item->stringOr(session.missing.reason);
        if (const Json* item = missing->find("source"); item != nullptr) session.missing.source = item->stringOr(session.missing.source);
    }
    if (const Json* objects = value.find("objects"); objects != nullptr && objects->isArray()) {
        session.objects.clear();
        for (const Json& object : objects->arrayItems()) {
            session.objects.push_back(objectFromJson(object));
        }
    }
    if (const Json* evidence = value.find("evidence"); evidence != nullptr && evidence->isArray()) {
        session.evidence.clear();
        for (const Json& entry : evidence->arrayItems()) {
            session.evidence.push_back(evidenceFromJson(entry));
        }
    }
    if (const Json* item = value.find("selectedObject"); item != nullptr) {
        session.selectedObject = static_cast<int>(item->asInt(session.selectedObject));
    }
    if (const Json* item = value.find("compareSequence"); item != nullptr) {
        session.compareSequence = static_cast<int>(item->asInt(session.compareSequence));
    }
    if (const Json* item = value.find("playhead"); item != nullptr) {
        session.playhead = item->asNumber(session.playhead);
    }
    if (const Json* item = value.find("playing"); item != nullptr) {
        session.playing = item->asBool(session.playing);
    }
    if (const Json* item = value.find("exportCount"); item != nullptr) {
        session.exportCount = static_cast<int>(item->asInt(session.exportCount));
    }
    if (const Json* item = value.find("lastExport"); item != nullptr) {
        session.lastExport = item->stringOr(session.lastExport);
    }
    if (session.objects.empty()) {
        session.objects = makeDefaultSession().objects;
    }
    session.selectedObject = std::max(0, std::min(
        static_cast<int>(session.objects.size()) - 1, session.selectedObject));
    session.playhead = clampDouble(session.playhead, 0.0, kTimelineDuration);
    return session;
}

const std::vector<std::string>& objectFieldOrder() {
    static const std::vector<std::string> fields = {
        "id", "label", "kind", "basePosition", "halfExtents", "motion", "triangles",
        "baselineKnown", "baselineVisible", "baselineCostMs", "runs"};
    return fields;
}

const std::vector<std::string>& runFieldOrder() {
    static const std::vector<std::string> fields = {
        "sequence", "startedAt", "sampleA", "clockOffsetMs", "visible", "baselineKnown",
        "changed", "costMs", "cause", "inputHash"};
    return fields;
}

const std::vector<std::string>& evidenceFieldOrder() {
    static const std::vector<std::string> fields = {
        "sequence", "at", "kind", "objectId", "detail", "previous", "digest"};
    return fields;
}

const std::vector<std::string>& sessionFieldOrder() {
    static const std::vector<std::string> fields = {
        "schema", "toolVersion", "sessionId", "sceneId", "frameId", "createdAt",
        "updatedAt", "camera", "clock", "missing", "objects", "evidence",
        "selectedObject", "compareSequence", "playhead", "playing", "exportCount",
        "lastExport"};
    return fields;
}

}  // namespace vfcr
