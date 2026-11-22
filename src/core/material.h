#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ssor {

constexpr std::uint64_t kMaterialHeaderBytes = 80;
constexpr std::uint64_t kMaterialObjectBytes = 80;
constexpr std::uint64_t kMaterialBoundaryBytes = 16;
constexpr std::uint32_t kMaterialVersion = 1;
constexpr std::uint32_t kInvalidObject = 0xffffffffu;

struct Vec3f {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class MeshType : std::uint32_t {
    Cube = 0,
    GroundPlane = 1,
};

enum class ProbeKind : std::uint32_t {
    Background = 0,
    Boundary = 1,
    Interior = 2,
};

struct ParseLimits {
    std::uint64_t maxFileBytes = 8ull * 1024ull * 1024ull;
    std::uint32_t maxObjects = 1024;
    std::uint32_t maxBoundaryPixels = 65536;
    std::uint64_t maxPixels = 4ull * 1024ull * 1024ull;
};

struct SourceField {
    std::string field;
    std::uint64_t offset = 0;
    std::uint32_t bytes = 0;
    std::string value;
};

struct ObjectDef {
    std::string id;
    MeshType mesh = MeshType::Cube;
    std::uint32_t drawOrder = 0;
    Vec3f translation;
    Vec3f originalOffset;
    float rotationYDeg = 0.0f;
    Vec3f scale{1.0f, 1.0f, 1.0f};
    std::uint32_t repeatOf = kInvalidObject;
    std::uint32_t flags = 0;
    std::uint64_t recordOffset = 0;
    std::uint64_t translationOffset = 0;
    std::uint64_t originalOffsetOffset = 0;

    bool duplicate() const { return (flags & 1u) != 0; }
};

struct BoundaryProbe {
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    ProbeKind expected = ProbeKind::Background;
    std::uint32_t objectIndex = 0;
    std::uint64_t recordOffset = 0;
};

struct RawMaterial {
    std::string path;
    std::uint64_t fileSize = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t objectCount = 0;
    std::uint32_t boundaryCount = 0;
    float outlineDepth = 0.0f;
    float normalThreshold = 0.0f;
    float gtDepthEps = 0.0f;
    float gtNormalEps = 0.0f;
    Vec3f cameraEye;
    Vec3f cameraTarget;
    std::uint32_t flags = 0;
    std::uint32_t reserved = 0;
    std::uint64_t headerBytes = kMaterialHeaderBytes;
    std::vector<ObjectDef> objects;
    std::vector<BoundaryProbe> boundaryPixels;
    std::vector<SourceField> sourceFields;
    std::string sha256;

    std::uint64_t objectsOffset = kMaterialHeaderBytes;
    std::uint64_t boundariesOffset = kMaterialHeaderBytes;
    std::uint64_t outlineDepthOffset = 32;
    std::uint64_t normalThresholdOffset = 36;
    std::uint64_t boundaryCountOffset = 28;
    std::uint64_t objectCountOffset = 24;

    int duplicateObjectIndex() const;
};

enum class MaterialError {
    None,
    MissingFile,
    EmptyFile,
    OversizedFile,
    TruncatedHeader,
    BadMagic,
    UnsupportedVersion,
    BadDimensions,
    OversizedDeclaredCount,
    TruncatedPayload,
    InvalidObject,
    InvalidBoundary,
    ReadFailure,
};

struct ParseResult {
    bool ok = false;
    MaterialError error = MaterialError::None;
    std::string code;
    std::string message;
    std::uint64_t rejectedAtOffset = 0;
    std::uint64_t observed = 0;
    std::uint64_t limit = 0;
    RawMaterial material;
};

ParseResult parseMaterialFile(const std::string& path,
                              const ParseLimits& limits = ParseLimits{});
std::vector<std::uint8_t> encodeMaterial(const RawMaterial& material);
bool writeMaterialFile(const std::string& path, const RawMaterial& material);

int exitCodeForMaterialError(MaterialError error);
std::string materialErrorName(MaterialError error);
std::string meshTypeName(MeshType mesh);
std::string probeKindName(ProbeKind kind);

nlohmann::ordered_json materialToJson(const RawMaterial& material,
                                      bool includeProbes = true);

}  // namespace ssor
