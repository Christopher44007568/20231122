#pragma once

#include "core/json.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace vfcr {

constexpr const char* kSchema = "vfcr.frustum-replay/v1";
constexpr const char* kToolVersion = "1.0.0";

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Camera {
    Vec3 position{0.0, 2.2, -1.0};
    double yawDeg = 0.0;
    double pitchDeg = -4.0;
    double fovYDeg = 56.0;
    double nearZ = 2.0;
    double farZ = 42.0;
    double aspect = 16.0 / 9.0;
};

struct Motion {
    Vec3 amplitude{0.0, 0.0, 0.0};
    double periodSec = 12.0;
    double phaseRad = 0.0;
};

struct RunRecord {
    int sequence = 0;
    std::string startedAt;
    double sampleA = 0.0;
    double clockOffsetMs = 0.0;
    bool visible = false;
    bool baselineKnown = false;
    bool changed = false;
    double costMs = 0.0;
    std::string cause;
    std::string inputHash;
};

struct ReplayObject {
    std::string id;
    std::string label;
    std::string kind;
    Vec3 basePosition;
    Vec3 halfExtents{1.0, 1.0, 1.0};
    Motion motion;
    int triangles = 0;
    bool baselineKnown = true;
    bool baselineVisible = false;
    double baselineCostMs = 0.0;
    std::vector<RunRecord> runs;
};

struct EvidenceEntry {
    int sequence = 0;
    std::string at;
    std::string kind;
    std::string objectId;
    std::string detail;
    std::string previous;
    std::string digest;
};

struct ClockSync {
    std::string deviceA = "CAM-A/ALPHA";
    std::string deviceB = "CAM-B/BRAVO";
    double offsetMs = 420.0;
    std::string method = "PTP_OFFSET_APPLIED";
    std::string confidence = "MEDIUM";
    double baselineSampleA = 11.5;
};

struct MissingFragment {
    std::string id = "GAP-017";
    double startA = 17.0;
    double endA = 21.4;
    std::string reason = "B PACKET DROP / DESYNC WINDOW";
    std::string source = "DEVICE_B";
};

struct Session {
    std::string schema = kSchema;
    std::string toolVersion = kToolVersion;
    std::string sessionId = "VFC-SESSION-0057";
    std::string sceneId = "TRACE-2026-11-22";
    std::string frameId = "FRAME-18422";
    std::string createdAt;
    std::string updatedAt;
    Camera camera;
    ClockSync clock;
    MissingFragment missing;
    std::vector<ReplayObject> objects;
    std::vector<EvidenceEntry> evidence;
    int selectedObject = 0;
    int compareSequence = 0;
    double playhead = 19.25;
    bool playing = false;
    int exportCount = 0;
    std::string lastExport;
};

struct CullEvaluation {
    bool visible = false;
    bool insideMissingWindow = false;
    double distance = 0.0;
    double costMs = 0.0;
    std::string decision;
};

Session makeDefaultSession();
Vec3 objectPositionAt(const ReplayObject& object, double sampleA);
CullEvaluation evaluateCulling(const Camera& camera, const ReplayObject& object, double sampleA);

std::string runInputHash(const Camera& camera, const ReplayObject& object, double sampleA,
                         double clockOffsetMs);
RunRecord rerunObject(Session& session, std::size_t objectIndex, double sampleA,
                      const std::string& startedAt);
void rerunAll(Session& session, double sampleA, const std::string& startedAt);
void adjustClockOffset(Session& session, double deltaMs, const std::string& at);
void shiftMissingFragment(Session& session, double deltaSeconds, const std::string& at);
void appendEvidence(Session& session, const std::string& kind, const std::string& objectId,
                    const std::string& detail, const std::string& at);
bool verifyEvidenceChain(const std::vector<EvidenceEntry>& entries, std::string* error = nullptr);
std::string evidenceHead(const std::vector<EvidenceEntry>& entries);

Json vec3ToJson(const Vec3& value);
Vec3 vec3FromJson(const Json& value, const Vec3& fallback = {});
Json cameraToJson(const Camera& camera);
Camera cameraFromJson(const Json& value);
Json objectToJson(const ReplayObject& object);
ReplayObject objectFromJson(const Json& value);
Json runToJson(const RunRecord& run);
RunRecord runFromJson(const Json& value);
Json evidenceToJson(const EvidenceEntry& entry);
EvidenceEntry evidenceFromJson(const Json& value);
Json sessionToJson(const Session& session);
Session sessionFromJson(const Json& value);
const std::vector<std::string>& objectFieldOrder();
const std::vector<std::string>& runFieldOrder();
const std::vector<std::string>& evidenceFieldOrder();
const std::vector<std::string>& sessionFieldOrder();

}  // namespace vfcr
