#pragma once

#include "core/raw_material.h"
#include "core/time_util.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace clipfix {

struct WorkspaceOptions {
    std::filesystem::path root;
    std::filesystem::path materialRoot;
};

struct SessionState {
    int revision = 0;
    bool suspended = false;
    int selectedObject = 0;
    std::string createdAt;
    std::string updatedAt;
    std::string lastSuspendAt;
    std::string lastResumeAt;
    int resumeCount = 0;
    std::array<double, 3> currentOffsetsMm{};
    std::vector<ObjectState> objects;
};

struct VerificationResult {
    bool rawMaterialOk = false;
    bool evidenceChainOk = false;
    bool sessionLoaded = false;
    std::size_t evidenceEventCount = 0;
    std::string rawError;
    std::string evidenceError;
    std::string aggregateSha256;
};

class Workspace {
public:
    static Workspace open(const WorkspaceOptions& options);

    void save();
    void suspend(const std::string& reason);
    void setCurrentOffset(std::size_t anchorIndex, double valueMm);
    void setCurrentOffsets(const std::array<double, 3>& valuesMm);
    void resetCurrentOffsetsToRaw();

    RunRecord runObject(const std::string& objectId);
    std::vector<RunRecord> runAllObjects();
    CompareResult compareObject(const std::string& objectId, bool previousRun);
    ExportRecord exportObject(const std::string& objectId);
    VerificationResult verify();

    void appendEvent(const std::string& type, const nlohmann::json& details);
    nlohmann::json buildSessionJson() const;

    const RawMaterialBundle& material() const { return material_; }
    const SessionState& state() const { return state_; }
    SessionState& state() { return state_; }
    const std::filesystem::path& root() const { return root_; }
    const std::filesystem::path& rawCopyRoot() const { return rawCopyRoot_; }
    const std::filesystem::path& sourceRoot() const { return sourceRoot_; }
    std::string aggregateSha256() const { return material_.aggregateSha256; }
    std::string manifestSha256() const { return material_.manifestSha256; }

    const RawObject* findRawObject(const std::string& objectId) const;
    const ObjectState* findObjectState(const std::string& objectId) const;
    ObjectState* findObjectState(const std::string& objectId);
    const RunRecord* latestRun(const std::string& objectId) const;
    const RunRecord* findRun(const std::string& objectId, const std::string& runId) const;
    Mesh loadRunMesh(const std::string& objectId, const std::string& runId) const;
    Mesh loadLatestRunMesh(const std::string& objectId) const;

    std::filesystem::path path(const std::string& relative) const;
    std::string relative(const std::filesystem::path& path) const;
    std::array<Vec3, 3> anchorPositions() const;
    std::array<Vec3, 3> anchorNormals() const;
    std::array<double, 3> rawOffsets() const;

private:
    Workspace() = default;
    void initialize(const WorkspaceOptions& options);
    void loadExisting(const WorkspaceOptions& options);
    void loadEvidenceChain();
    void protectRawCopy();
    std::string nextRunId(const std::string& objectId) const;
    void writeJsonAtomic(const std::filesystem::path& target, const nlohmann::json& value) const;
    nlohmann::json evidenceEvent(
        const std::string& type,
        const nlohmann::json& details,
        const Timestamp& stamp) const;

    std::filesystem::path root_;
    std::filesystem::path rawCopyRoot_;
    std::filesystem::path sourceRoot_;
    RawMaterialBundle material_;
    SessionState state_;
    std::uint64_t evidenceSequence_ = 0;
    std::string lastEvidenceHash_ = "GENESIS";
};

bool verifyEvidenceChain(
    const std::filesystem::path& evidenceLog,
    std::size_t* eventCount,
    std::string* error);

}  // namespace clipfix
