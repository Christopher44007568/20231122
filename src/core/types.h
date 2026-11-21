#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace clipfix {

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    Vec3 operator+(const Vec3& rhs) const;
    Vec3 operator-(const Vec3& rhs) const;
    Vec3 operator*(double scale) const;
    Vec3& operator+=(const Vec3& rhs);
    double dot(const Vec3& rhs) const;
    Vec3 cross(const Vec3& rhs) const;
    double length() const;
    Vec3 normalized(const Vec3& fallback = {0.0, 1.0, 0.0}) const;
};

inline Vec3 Vec3::operator+(const Vec3& rhs) const {
    return {x + rhs.x, y + rhs.y, z + rhs.z};
}

inline Vec3 Vec3::operator-(const Vec3& rhs) const {
    return {x - rhs.x, y - rhs.y, z - rhs.z};
}

inline Vec3 Vec3::operator*(double scale) const {
    return {x * scale, y * scale, z * scale};
}

inline Vec3& Vec3::operator+=(const Vec3& rhs) {
    x += rhs.x;
    y += rhs.y;
    z += rhs.z;
    return *this;
}

inline double Vec3::dot(const Vec3& rhs) const {
    return x * rhs.x + y * rhs.y + z * rhs.z;
}

inline Vec3 Vec3::cross(const Vec3& rhs) const {
    return {
        y * rhs.z - z * rhs.y,
        z * rhs.x - x * rhs.z,
        x * rhs.y - y * rhs.x,
    };
}

inline double Vec3::length() const {
    return std::sqrt(dot(*this));
}

inline Vec3 Vec3::normalized(const Vec3& fallback) const {
    const double len = length();
    return len > 1.0e-12 ? (*this * (1.0 / len)) : fallback;
}

struct Mesh {
    std::vector<Vec3> positions;
    std::vector<Vec3> normals;
    std::vector<std::uint32_t> indices;

    bool valid() const;
};

inline bool Mesh::valid() const {
    return !positions.empty() && !indices.empty() && indices.size() % 3 == 0;
}

struct RawAnchor {
    int id = 0;
    std::string name;
    Vec3 position;
    Vec3 normal{0.0, 1.0, 0.0};
    double rawOffsetMm = 0.0;
    std::string rawOffsetText;
};

struct RawObject {
    std::string id;
    std::string name;
    std::string relativeObjPath;
    std::string rawFileSha256;
    std::string material;
    Mesh mesh;
};

struct RawScene {
    std::string format;
    std::string name;
    std::string description;
    std::string createdAt;
    std::vector<RawAnchor> anchors;
    std::vector<RawObject> objects;
};

struct RunStats {
    std::size_t vertexCount = 0;
    std::size_t affectedCount = 0;
    double maxAbsBeforeMm = 0.0;
    double meanAbsBeforeMm = 0.0;
    double maxAbsAfterMm = 0.0;
    double meanAbsAfterMm = 0.0;
    double maxDeltaMm = 0.0;
    double meanDeltaMm = 0.0;
};

struct VertexEvidence {
    std::uint32_t vertexIndex = 0;
    double curveT = 0.0;
    double lateralMm = 0.0;
    double beforeOffsetMm = 0.0;
    double afterOffsetMm = 0.0;
    double deltaMm = 0.0;
};

struct RunRecord {
    std::string runId;
    std::string objectId;
    std::string startedUtc;
    std::string finishedUtc;
    long long startedEpochMs = 0;
    long long finishedEpochMs = 0;
    std::array<double, 3> rawOffsetsMm{};
    std::array<double, 3> appliedOffsetsMm{};
    double bandMm = 0.0;
    double widthMm = 0.0;
    RunStats stats;
    std::string sourceSha256;
    std::string outputObjSha256;
    std::string evidenceCsvSha256;
    std::string outputObjPath;
    std::string evidenceCsvPath;
    std::vector<VertexEvidence> keyVertices;
};

struct CompareResult {
    std::string objectId;
    std::string createdUtc;
    std::string aKind;
    std::string bKind;
    std::string aRef;
    std::string bRef;
    std::size_t comparedVertices = 0;
    std::size_t changedVertices = 0;
    double maxDeltaMm = 0.0;
    double meanDeltaMm = 0.0;
    std::string reportPath;
};

struct ExportRecord {
    std::string objectId;
    std::string runId;
    std::string exportedUtc;
    std::string objPath;
    std::string reportPath;
    std::string objSha256;
};

struct ObjectState {
    std::string id;
    bool dirty = true;
    bool compareReady = false;
    bool hasCompare = false;
    bool hasExport = false;
    CompareResult lastCompare;
    ExportRecord lastExport;
    std::vector<RunRecord> runs;
};

}  // namespace clipfix
