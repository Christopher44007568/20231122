#include "core/material.h"
#include "core/util.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ssor {
namespace {

constexpr char kMagic[8] = {'S', 'S', 'O', 'R', 'A', 'W', '1', '\0'};

std::uint32_t rdU32(const std::vector<std::uint8_t>& data, std::uint64_t offset) {
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

float rdF32(const std::vector<std::uint8_t>& data, std::uint64_t offset) {
    const std::uint32_t bits = rdU32(data, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

Vec3f rdVec3(const std::vector<std::uint8_t>& data, std::uint64_t offset) {
    return {rdF32(data, offset), rdF32(data, offset + 4), rdF32(data, offset + 8)};
}

void wrU32(std::vector<std::uint8_t>& data, std::uint64_t offset,
           std::uint32_t value) {
    data[offset + 0] = static_cast<std::uint8_t>(value & 0xff);
    data[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    data[offset + 2] = static_cast<std::uint8_t>((value >> 16) & 0xff);
    data[offset + 3] = static_cast<std::uint8_t>((value >> 24) & 0xff);
}

void wrF32(std::vector<std::uint8_t>& data, std::uint64_t offset, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    wrU32(data, offset, bits);
}

void wrVec3(std::vector<std::uint8_t>& data, std::uint64_t offset, Vec3f value) {
    wrF32(data, offset, value.x);
    wrF32(data, offset + 4, value.y);
    wrF32(data, offset + 8, value.z);
}

std::string bytesText(const std::vector<std::uint8_t>& data, std::uint64_t offset,
                      std::uint32_t count) {
    std::string out;
    for (std::uint32_t i = 0; i < count; ++i) {
        const char c = static_cast<char>(data[offset + i]);
        if (c == '\0') break;
        out.push_back(c);
    }
    return out;
}

std::string vecText(Vec3f v) {
    std::ostringstream out;
    out << v.x << "," << v.y << "," << v.z;
    return out.str();
}

ParseResult failure(MaterialError error, std::string code, std::string message,
                    std::uint64_t offset, std::uint64_t observed = 0,
                    std::uint64_t limit = 0) {
    ParseResult result;
    result.error = error;
    result.code = std::move(code);
    result.message = std::move(message);
    result.rejectedAtOffset = offset;
    result.observed = observed;
    result.limit = limit;
    return result;
}

}  // namespace

int RawMaterial::duplicateObjectIndex() const {
    for (std::size_t i = 0; i < objects.size(); ++i) {
        if (objects[i].duplicate()) return static_cast<int>(i);
    }
    return -1;
}

std::string meshTypeName(MeshType mesh) {
    switch (mesh) {
        case MeshType::Cube: return "cube";
        case MeshType::GroundPlane: return "ground_plane";
    }
    return "unknown";
}

std::string probeKindName(ProbeKind kind) {
    switch (kind) {
        case ProbeKind::Background: return "background";
        case ProbeKind::Boundary: return "boundary";
        case ProbeKind::Interior: return "interior";
    }
    return "unknown";
}

std::string materialErrorName(MaterialError error) {
    switch (error) {
        case MaterialError::None: return "NONE";
        case MaterialError::MissingFile: return "MISSING_FILE";
        case MaterialError::EmptyFile: return "EMPTY_INPUT";
        case MaterialError::OversizedFile: return "OVERSIZED_FILE";
        case MaterialError::TruncatedHeader: return "TRUNCATED_HEADER";
        case MaterialError::BadMagic: return "BAD_MAGIC";
        case MaterialError::UnsupportedVersion: return "UNSUPPORTED_VERSION";
        case MaterialError::BadDimensions: return "BAD_DIMENSIONS";
        case MaterialError::OversizedDeclaredCount: return "OVERSIZED_DECLARED_COUNT";
        case MaterialError::TruncatedPayload: return "TRUNCATED_PAYLOAD";
        case MaterialError::InvalidObject: return "INVALID_OBJECT";
        case MaterialError::InvalidBoundary: return "INVALID_BOUNDARY";
        case MaterialError::ReadFailure: return "READ_FAILURE";
    }
    return "UNKNOWN";
}

int exitCodeForMaterialError(MaterialError error) {
    switch (error) {
        case MaterialError::None: return 0;
        case MaterialError::MissingFile: return 2;
        case MaterialError::EmptyFile: return 4;
        case MaterialError::OversizedFile:
        case MaterialError::OversizedDeclaredCount: return 5;
        default: return 7;
    }
}

ParseResult parseMaterialFile(const std::string& path, const ParseLimits& limits) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) ||
        !std::filesystem::is_regular_file(path, ec)) {
        return failure(MaterialError::MissingFile, "MISSING_FILE",
                       "material file does not exist", 0);
    }
    const std::uint64_t size =
        static_cast<std::uint64_t>(std::filesystem::file_size(path, ec));
    if (ec) {
        return failure(MaterialError::ReadFailure, "READ_FAILURE",
                       "cannot stat material file", 0);
    }
    if (size == 0) {
        return failure(MaterialError::EmptyFile, "EMPTY_INPUT",
                       "material file is zero bytes", 0, 0, 1);
    }
    if (size > limits.maxFileBytes) {
        return failure(MaterialError::OversizedFile, "OVERSIZED_FILE",
                       "material file exceeds the byte limit", 0, size,
                       limits.maxFileBytes);
    }

    bool readOk = false;
    const std::string bytes =
        readFile(path, static_cast<std::size_t>(limits.maxFileBytes), nullptr, &readOk);
    if (!readOk) {
        return failure(MaterialError::ReadFailure, "READ_FAILURE",
                       "material file could not be read", 0);
    }
    std::vector<std::uint8_t> data(bytes.begin(), bytes.end());
    if (data.size() < kMaterialHeaderBytes) {
        return failure(MaterialError::TruncatedHeader, "TRUNCATED_HEADER",
                       "header is shorter than 80 bytes", data.size());
    }
    if (std::memcmp(data.data(), kMagic, sizeof(kMagic)) != 0) {
        return failure(MaterialError::BadMagic, "BAD_MAGIC",
                       "expected SSORAW1 magic", 0);
    }
    const std::uint32_t version = rdU32(data, 8);
    if (version != kMaterialVersion) {
        return failure(MaterialError::UnsupportedVersion, "UNSUPPORTED_VERSION",
                       "unsupported material version", 8, version, kMaterialVersion);
    }

    RawMaterial material;
    material.path = path;
    material.fileSize = data.size();
    material.headerBytes = rdU32(data, 12);
    material.width = rdU32(data, 16);
    material.height = rdU32(data, 20);
    material.objectCount = rdU32(data, 24);
    material.boundaryCount = rdU32(data, 28);
    material.outlineDepth = rdF32(data, 32);
    material.normalThreshold = rdF32(data, 36);
    material.gtDepthEps = rdF32(data, 40);
    material.gtNormalEps = rdF32(data, 44);
    material.cameraEye = rdVec3(data, 48);
    material.cameraTarget = rdVec3(data, 60);
    material.flags = rdU32(data, 72);
    material.reserved = rdU32(data, 76);

    if (material.headerBytes < kMaterialHeaderBytes ||
        material.headerBytes > data.size()) {
        return failure(MaterialError::TruncatedHeader, "BAD_HEADER_SIZE",
                       "headerBytes is outside the file", 12,
                       material.headerBytes, data.size());
    }
    if (material.width == 0 || material.height == 0) {
        return failure(MaterialError::BadDimensions, "BAD_DIMENSIONS",
                       "width and height must be non-zero", 16);
    }
    if (static_cast<std::uint64_t>(material.width) * material.height >
        limits.maxPixels) {
        return failure(MaterialError::OversizedDeclaredCount, "OVERSIZED_DIMENSIONS",
                       "pixel count exceeds the limit", 16,
                       static_cast<std::uint64_t>(material.width) * material.height,
                       limits.maxPixels);
    }
    if (material.objectCount > limits.maxObjects) {
        return failure(MaterialError::OversizedDeclaredCount,
                       "OVERSIZED_DECLARED_COUNT",
                       "objectCount exceeds the limit", 24,
                       material.objectCount, limits.maxObjects);
    }
    if (material.boundaryCount > limits.maxBoundaryPixels) {
        return failure(MaterialError::OversizedDeclaredCount,
                       "OVERSIZED_DECLARED_COUNT",
                       "boundaryCount exceeds the limit", 28,
                       material.boundaryCount, limits.maxBoundaryPixels);
    }
    const std::uint64_t required =
        material.headerBytes +
        static_cast<std::uint64_t>(material.objectCount) * kMaterialObjectBytes +
        static_cast<std::uint64_t>(material.boundaryCount) * kMaterialBoundaryBytes;
    if (required > data.size()) {
        return failure(MaterialError::TruncatedPayload, "TRUNCATED_PAYLOAD",
                       "declared object/boundary records exceed the file",
                       data.size(), required, data.size());
    }

    material.objectsOffset = material.headerBytes;
    material.boundariesOffset =
        material.headerBytes +
        static_cast<std::uint64_t>(material.objectCount) * kMaterialObjectBytes;

    material.objects.reserve(material.objectCount);
    for (std::uint32_t i = 0; i < material.objectCount; ++i) {
        const std::uint64_t off =
            material.objectsOffset + static_cast<std::uint64_t>(i) * kMaterialObjectBytes;
        ObjectDef object;
        object.recordOffset = off;
        object.id = bytesText(data, off, 24);
        object.mesh = static_cast<MeshType>(rdU32(data, off + 24));
        object.drawOrder = rdU32(data, off + 28);
        object.translationOffset = off + 32;
        object.translation = rdVec3(data, off + 32);
        object.originalOffsetOffset = off + 44;
        object.originalOffset = rdVec3(data, off + 44);
        object.rotationYDeg = rdF32(data, off + 56);
        object.scale = rdVec3(data, off + 60);
        object.repeatOf = rdU32(data, off + 72);
        object.flags = rdU32(data, off + 76);
        if (object.id.empty()) {
            return failure(MaterialError::InvalidObject, "INVALID_OBJECT",
                           "object id is empty", off);
        }
        if (object.mesh != MeshType::Cube &&
            object.mesh != MeshType::GroundPlane) {
            return failure(MaterialError::InvalidObject, "INVALID_OBJECT",
                           "unknown mesh type", off + 24,
                           static_cast<std::uint32_t>(object.mesh), 1);
        }
        if (object.repeatOf != kInvalidObject &&
            object.repeatOf >= material.objectCount) {
            return failure(MaterialError::InvalidObject, "INVALID_OBJECT",
                           "repeatOf is out of range", off + 72,
                           object.repeatOf, material.objectCount);
        }
        if (object.duplicate() &&
            (object.repeatOf == kInvalidObject || object.repeatOf == i)) {
            return failure(MaterialError::InvalidObject, "INVALID_OBJECT",
                           "duplicate object needs a different repeatOf base",
                           off + 72, object.repeatOf, material.objectCount);
        }
        material.objects.push_back(object);
    }

    material.boundaryPixels.reserve(material.boundaryCount);
    for (std::uint32_t i = 0; i < material.boundaryCount; ++i) {
        const std::uint64_t off =
            material.boundariesOffset +
            static_cast<std::uint64_t>(i) * kMaterialBoundaryBytes;
        BoundaryProbe probe;
        probe.recordOffset = off;
        probe.x = rdU32(data, off);
        probe.y = rdU32(data, off + 4);
        probe.expected = static_cast<ProbeKind>(rdU32(data, off + 8));
        probe.objectIndex = rdU32(data, off + 12);
        if (probe.x >= material.width || probe.y >= material.height) {
            return failure(MaterialError::InvalidBoundary, "INVALID_BOUNDARY",
                           "boundary pixel is outside the viewport", off,
                           probe.x, material.width);
        }
        if (static_cast<std::uint32_t>(probe.expected) > 2) {
            return failure(MaterialError::InvalidBoundary, "INVALID_BOUNDARY",
                           "unknown boundary expected class", off + 8,
                           static_cast<std::uint32_t>(probe.expected), 2);
        }
        if (probe.objectIndex >= material.objectCount) {
            return failure(MaterialError::InvalidBoundary, "INVALID_BOUNDARY",
                           "boundary object index is out of range", off + 12,
                           probe.objectIndex, material.objectCount);
        }
        material.boundaryPixels.push_back(probe);
    }

    material.outlineDepthOffset = 32;
    material.normalThresholdOffset = 36;
    material.boundaryCountOffset = 28;
    material.objectCountOffset = 24;
    material.sha256 = sha256Hex(data.data(), data.size());

    auto addField = [&](const std::string& field, std::uint64_t offset,
                        std::uint32_t count, const std::string& value) {
        material.sourceFields.push_back({field, offset, count, value});
    };
    addField("header.magic", 0, 8, "SSORAW1\\0");
    addField("header.version", 8, 4, std::to_string(version));
    addField("header.headerBytes", 12, 4, std::to_string(material.headerBytes));
    addField("header.width", 16, 4, std::to_string(material.width));
    addField("header.height", 20, 4, std::to_string(material.height));
    addField("header.objectCount", 24, 4, std::to_string(material.objectCount));
    addField("header.boundaryCount", 28, 4, std::to_string(material.boundaryCount));
    addField("header.outlineDepth", 32, 4, std::to_string(material.outlineDepth));
    addField("header.normalThreshold", 36, 4,
             std::to_string(material.normalThreshold));
    addField("header.gtDepthEps", 40, 4, std::to_string(material.gtDepthEps));
    addField("header.gtNormalEps", 44, 4, std::to_string(material.gtNormalEps));
    addField("header.cameraEye", 48, 12, vecText(material.cameraEye));
    addField("header.cameraTarget", 60, 12, vecText(material.cameraTarget));
    addField("header.flags", 72, 4, std::to_string(material.flags));
    for (std::uint32_t i = 0; i < material.objectCount; ++i) {
        const ObjectDef& object = material.objects[i];
        const std::uint64_t off = object.recordOffset;
        const std::string prefix = "objects[" + std::to_string(i) + "]";
        addField(prefix + ".id", off, 24, object.id);
        addField(prefix + ".mesh", off + 24, 4, meshTypeName(object.mesh));
        addField(prefix + ".translation", off + 32, 12, vecText(object.translation));
        addField(prefix + ".originalOffset", off + 44, 12,
                 vecText(object.originalOffset));
        addField(prefix + ".repeatOf", off + 72, 4,
                 object.repeatOf == kInvalidObject
                     ? "none"
                     : std::to_string(object.repeatOf));
    }
    for (std::uint32_t i = 0; i < material.boundaryCount; ++i) {
        const BoundaryProbe& probe = material.boundaryPixels[i];
        std::ostringstream value;
        value << "x=" << probe.x << ",y=" << probe.y
              << ",expected=" << probeKindName(probe.expected)
              << ",object=" << probe.objectIndex;
        addField("boundaries[" + std::to_string(i) + "]", probe.recordOffset, 16,
                 value.str());
    }

    ParseResult result;
    result.ok = true;
    result.material = std::move(material);
    return result;
}

std::vector<std::uint8_t> encodeMaterial(const RawMaterial& material) {
    std::vector<std::uint8_t> data(
        static_cast<std::size_t>(
            kMaterialHeaderBytes +
            static_cast<std::uint64_t>(material.objects.size()) * kMaterialObjectBytes +
            static_cast<std::uint64_t>(material.boundaryPixels.size()) *
                kMaterialBoundaryBytes),
        0);
    std::memcpy(data.data(), kMagic, sizeof(kMagic));
    wrU32(data, 8, kMaterialVersion);
    wrU32(data, 12, static_cast<std::uint32_t>(kMaterialHeaderBytes));
    wrU32(data, 16, material.width);
    wrU32(data, 20, material.height);
    wrU32(data, 24, static_cast<std::uint32_t>(material.objects.size()));
    wrU32(data, 28, static_cast<std::uint32_t>(material.boundaryPixels.size()));
    wrF32(data, 32, material.outlineDepth);
    wrF32(data, 36, material.normalThreshold);
    wrF32(data, 40, material.gtDepthEps);
    wrF32(data, 44, material.gtNormalEps);
    wrVec3(data, 48, material.cameraEye);
    wrVec3(data, 60, material.cameraTarget);
    wrU32(data, 72, material.flags);
    wrU32(data, 76, material.reserved);

    for (std::size_t i = 0; i < material.objects.size(); ++i) {
        const ObjectDef& object = material.objects[i];
        const std::uint64_t off =
            kMaterialHeaderBytes + static_cast<std::uint64_t>(i) * kMaterialObjectBytes;
        const std::string id = object.id.substr(0, 23);
        std::memcpy(data.data() + off, id.data(), id.size());
        wrU32(data, off + 24, static_cast<std::uint32_t>(object.mesh));
        wrU32(data, off + 28, object.drawOrder);
        wrVec3(data, off + 32, object.translation);
        wrVec3(data, off + 44, object.originalOffset);
        wrF32(data, off + 56, object.rotationYDeg);
        wrVec3(data, off + 60, object.scale);
        wrU32(data, off + 72, object.repeatOf);
        wrU32(data, off + 76, object.flags);
    }
    const std::uint64_t boundaryBase =
        kMaterialHeaderBytes +
        static_cast<std::uint64_t>(material.objects.size()) * kMaterialObjectBytes;
    for (std::size_t i = 0; i < material.boundaryPixels.size(); ++i) {
        const BoundaryProbe& probe = material.boundaryPixels[i];
        const std::uint64_t off =
            boundaryBase + static_cast<std::uint64_t>(i) * kMaterialBoundaryBytes;
        wrU32(data, off + 0, probe.x);
        wrU32(data, off + 4, probe.y);
        wrU32(data, off + 8, static_cast<std::uint32_t>(probe.expected));
        wrU32(data, off + 12, probe.objectIndex);
    }
    return data;
}

bool writeMaterialFile(const std::string& path, const RawMaterial& material) {
    const std::vector<std::uint8_t> data = encodeMaterial(material);
    return writeFile(path, data.data(), data.size());
}

nlohmann::ordered_json materialToJson(const RawMaterial& material,
                                      bool includeProbes) {
    nlohmann::ordered_json json;
    json["path"] = material.path;
    json["bytes"] = material.fileSize;
    json["sha256"] = material.sha256;
    json["width"] = material.width;
    json["height"] = material.height;
    json["objectCount"] = material.objectCount;
    json["boundaryCount"] = material.boundaryCount;
    json["outlineDepth"] = material.outlineDepth;
    json["normalThreshold"] = material.normalThreshold;
    json["gtDepthEps"] = material.gtDepthEps;
    json["gtNormalEps"] = material.gtNormalEps;
    json["cameraEye"] = {material.cameraEye.x, material.cameraEye.y,
                         material.cameraEye.z};
    json["cameraTarget"] = {material.cameraTarget.x, material.cameraTarget.y,
                            material.cameraTarget.z};
    json["flags"] = material.flags;
    json["rawOffsets"] = {
        {"objectCount", material.objectCountOffset},
        {"boundaryCount", material.boundaryCountOffset},
        {"outlineDepth", material.outlineDepthOffset},
        {"normalThreshold", material.normalThresholdOffset},
        {"objectsBase", material.objectsOffset},
        {"boundariesBase", material.boundariesOffset},
    };

    nlohmann::ordered_json objects = nlohmann::ordered_json::array();
    for (std::size_t i = 0; i < material.objects.size(); ++i) {
        const ObjectDef& object = material.objects[i];
        nlohmann::ordered_json item;
        item["index"] = i;
        item["id"] = object.id;
        item["mesh"] = meshTypeName(object.mesh);
        item["drawOrder"] = object.drawOrder;
        item["duplicate"] = object.duplicate();
        item["repeatOf"] = object.repeatOf == kInvalidObject
                               ? nlohmann::ordered_json(nullptr)
                               : nlohmann::ordered_json(object.repeatOf);
        item["translation"] = {object.translation.x, object.translation.y,
                               object.translation.z};
        item["originalOffset"] = {object.originalOffset.x, object.originalOffset.y,
                                  object.originalOffset.z};
        item["rotationYDeg"] = object.rotationYDeg;
        item["scale"] = {object.scale.x, object.scale.y, object.scale.z};
        item["sourceOffsets"] = {
            {"record", object.recordOffset},
            {"translation", object.translationOffset},
            {"originalOffset", object.originalOffsetOffset},
        };
        objects.push_back(std::move(item));
    }
    json["objects"] = std::move(objects);

    if (includeProbes) {
        nlohmann::ordered_json probes = nlohmann::ordered_json::array();
        for (std::size_t i = 0; i < material.boundaryPixels.size(); ++i) {
            const BoundaryProbe& probe = material.boundaryPixels[i];
            probes.push_back({
                {"index", i},
                {"x", probe.x},
                {"y", probe.y},
                {"expected", probeKindName(probe.expected)},
                {"objectIndex", probe.objectIndex},
                {"sourceOffset", probe.recordOffset},
            });
        }
        json["boundaryPixels"] = std::move(probes);
    }

    nlohmann::ordered_json fields = nlohmann::ordered_json::array();
    for (const SourceField& field : material.sourceFields) {
        fields.push_back({
            {"field", field.field},
            {"offset", field.offset},
            {"bytes", field.bytes},
            {"value", field.value},
        });
    }
    json["sourceFields"] = std::move(fields);
    return json;
}

}  // namespace ssor
