#pragma once
//
// On-disk skinned-mesh container format.
//
// Two revisions are supported, both little-endian:
//   * legacy  (format id 0x534B3100 = "SK1\0"): quantized uint16 bone weights,
//              row-major 3x4 bone matrices, one global bind-pose table.
//   * current (format id 0x534B3200 = "SK2\0"): float32 normalized weights,
//              column-major 4x4 bone matrices, per-bone inverse bind matrices
//              and an explicit node-parent skeleton hierarchy.
//
// Files are identified by their magic bytes, never by their file extension.
// An unknown magic / unknown extension is reported as a typed parse error so
// the batch driver can keep processing the remaining objects.
//
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

namespace skr {

constexpr uint32_t kMagicLegacy  = 0x534B3100u; // "SK1\0"
constexpr uint32_t kMagicCurrent = 0x534B3200u; // "SK2\0"
constexpr uint32_t kMaxBonesPerVertex = 4;
constexpr uint32_t kMaxBones = 64;

enum class FormatRevision : uint32_t { LegacyV1 = 1, CurrentV2 = 2, Unknown = 0 };

// Column-major 4x4, matching Vulkan push-constant / UBO layout.
struct Mat4 { float m[16]; };
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };

struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
    uint32_t boneIndex[kMaxBonesPerVertex];
    float    boneWeight[kMaxBonesPerVertex];
};

struct Bone {
    std::string name;
    int32_t parent = -1;
    Mat4 inverseBind;            // object-space -> bone-space (v2: on disk; v1: derived)
    Mat4 localTransform;         // bind-pose local transform
};

struct SkinnedMesh {
    FormatRevision revision = FormatRevision::Unknown;
    std::string sourcePath;
    std::string objectName;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::vector<Bone> bones;

    // Sampled animation pose: one local 4x4 per bone at the review frame.
    std::vector<Mat4> poseLocal;
    float animationTime = 0.0f;
};

// Binary writer used by the sample-data generator.
void writeLegacyMesh(const SkinnedMesh& mesh, const std::string& path);
void writeCurrentMesh(const SkinnedMesh& mesh, const std::string& path);

// Dispatch on magic bytes. Throws FormatError on anything unrecognized.
SkinnedMesh loadSkinnedMesh(const std::string& path);

class FormatError : public std::runtime_error {
public:
    explicit FormatError(const std::string& msg) : std::runtime_error(msg) {}
};

} // namespace skr
