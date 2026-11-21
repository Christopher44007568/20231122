#include "skinning_format.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <array>

namespace skr {
namespace {

template <typename T>
void readPod(std::ifstream& in, T& out) {
    in.read(reinterpret_cast<char*>(&out), sizeof(T));
}

template <typename T>
T readPod(std::ifstream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(T));
    return value;
}

template <typename T>
void writePod(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(T));
}

std::vector<char> readAllBytes(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw FormatError("cannot open file: " + path);
    const auto size = static_cast<size_t>(in.tellg());
    std::vector<char> bytes(size);
    in.seekg(0);
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    return bytes;
}

// Row-major 3x4 (legacy) -> column-major 4x4 with an implicit last row.
Mat4 fromRowMajor3x4(const float r[12]) {
    Mat4 out{};
    out.m[0]  = r[0];  out.m[1]  = r[4];  out.m[2]  = r[8];  out.m[3]  = 0.0f;
    out.m[4]  = r[1];  out.m[5]  = r[5];  out.m[6]  = r[9];  out.m[7]  = 0.0f;
    out.m[8]  = r[2];  out.m[9]  = r[6];  out.m[10] = r[10]; out.m[11] = 0.0f;
    out.m[12] = r[3];  out.m[13] = r[7];  out.m[14] = r[11]; out.m[15] = 1.0f;
    return out;
}

Mat4 identity4() {
    Mat4 m{};
    m.m[0] = m.m[5] = m.m[10] = m.m[15] = 1.0f;
    return m;
}

} // namespace

static SkinnedMesh loadLegacyV1(const std::vector<char>& bytes, const std::string& path) {
    SkinnedMesh mesh;
    mesh.revision = FormatRevision::LegacyV1;
    mesh.sourcePath = path;

    struct Header { uint32_t magic; uint32_t vertexCount; uint32_t indexCount;
                    uint32_t boneCount; char name[32]; };
    if (bytes.size() < sizeof(Header))
        throw FormatError(path + ": truncated legacy header");
    Header header{};
    std::memcpy(&header, bytes.data(), sizeof(Header));
    mesh.objectName = std::string(header.name, strnlen(header.name, sizeof(header.name)));
    if (header.boneCount > kMaxBones)
        throw FormatError(path + ": bone count exceeds engine limit");

    size_t offset = sizeof(Header);
    auto take = [&](void* dst, size_t n) {
        if (offset + n > bytes.size())
            throw FormatError(path + ": unexpected end of file");
        std::memcpy(dst, bytes.data() + offset, n);
        offset += n;
    };

    struct LegacyVertex {
        float position[3]; float normal[3]; float uv[2];
        uint16_t boneIndex[4]; uint16_t boneWeightQ[4]; // weights / 65535
    };
    mesh.vertices.resize(header.vertexCount);
    for (uint32_t i = 0; i < header.vertexCount; ++i) {
        LegacyVertex lv{};
        take(&lv, sizeof(lv));
        Vertex& v = mesh.vertices[i];
        std::memcpy(v.position, lv.position, sizeof(v.position));
        std::memcpy(v.normal, lv.normal, sizeof(v.normal));
        std::memcpy(v.uv, lv.uv, sizeof(v.uv));
        float weightSum = 0.0f;
        for (uint32_t k = 0; k < 4; ++k) {
            v.boneIndex[k] = lv.boneIndex[k];
            v.boneWeight[k] = lv.boneWeightQ[k] / 65535.0f;
            weightSum += v.boneWeight[k];
        }
        if (weightSum > 0.0f)
            for (float& w : v.boneWeight) w /= weightSum; // renormalize
    }

    mesh.indices.resize(header.indexCount);
    take(mesh.indices.data(), header.indexCount * sizeof(uint32_t));

    mesh.bones.resize(header.boneCount);
    mesh.poseLocal.resize(header.boneCount, identity4());
    for (uint32_t b = 0; b < header.boneCount; ++b) {
        char name[24]{}; take(name, sizeof(name));
        int32_t parent = 0; take(&parent, sizeof(parent));
        float bind3x4[12]{}; take(bind3x4, sizeof(bind3x4));
        float pose3x4[12]{}; take(pose3x4, sizeof(pose3x4));
        mesh.bones[b].name = name;
        mesh.bones[b].parent = parent;
        mesh.bones[b].inverseBind = fromRowMajor3x4(bind3x4);
        mesh.bones[b].localTransform = fromRowMajor3x4(bind3x4);
        mesh.poseLocal[b] = fromRowMajor3x4(pose3x4);
    }
    mesh.animationTime = 0.0f;
    return mesh;
}

static SkinnedMesh loadCurrentV2(const std::vector<char>& bytes, const std::string& path) {
    SkinnedMesh mesh;
    mesh.revision = FormatRevision::CurrentV2;
    mesh.sourcePath = path;

    struct Header { uint32_t magic; uint32_t version; uint32_t vertexCount;
                    uint32_t indexCount; uint32_t boneCount;
                    float animationTime; char name[32]; };
    if (bytes.size() < sizeof(Header))
        throw FormatError(path + ": truncated current header");
    Header header{};
    std::memcpy(&header, bytes.data(), sizeof(Header));
    if (header.version != 2u)
        throw FormatError(path + ": unsupported v2 minor version " +
                          std::to_string(header.version));
    mesh.objectName = std::string(header.name, strnlen(header.name, sizeof(header.name)));
    mesh.animationTime = header.animationTime;
    if (header.boneCount > kMaxBones)
        throw FormatError(path + ": bone count exceeds engine limit");

    size_t offset = sizeof(Header);
    auto take = [&](void* dst, size_t n) {
        if (offset + n > bytes.size())
            throw FormatError(path + ": unexpected end of file");
        std::memcpy(dst, bytes.data() + offset, n);
        offset += n;
    };

    mesh.vertices.resize(header.vertexCount);
    for (Vertex& v : mesh.vertices) take(&v, sizeof(Vertex));

    mesh.indices.resize(header.indexCount);
    take(mesh.indices.data(), header.indexCount * sizeof(uint32_t));

    mesh.bones.resize(header.boneCount);
    mesh.poseLocal.resize(header.boneCount);
    for (uint32_t b = 0; b < header.boneCount; ++b) {
        uint16_t nameLen = 0; take(&nameLen, sizeof(nameLen));
        std::string name(nameLen, '\0');
        take(name.data(), nameLen);
        take(&mesh.bones[b].parent, sizeof(int32_t));
        take(&mesh.bones[b].inverseBind, sizeof(Mat4));
        take(&mesh.bones[b].localTransform, sizeof(Mat4));
        take(&mesh.poseLocal[b], sizeof(Mat4));
        mesh.bones[b].name = std::move(name);
    }
    return mesh;
}

SkinnedMesh loadSkinnedMesh(const std::string& path) {
    const std::vector<char> bytes = readAllBytes(path);
    if (bytes.size() < sizeof(uint32_t))
        throw FormatError(path + ": file too small to contain a format id");
    uint32_t magic = 0;
    std::memcpy(&magic, bytes.data(), sizeof(uint32_t));
    switch (magic) {
        case kMagicLegacy:  return loadLegacyV1(bytes, path);
        case kMagicCurrent: return loadCurrentV2(bytes, path);
        default:
            throw FormatError(path + ": unknown format id 0x" +
                              [magic] { char buf[16];
                                        std::snprintf(buf, sizeof(buf), "%08X", magic);
                                        return std::string(buf); }());
    }
}

void writeLegacyMesh(const SkinnedMesh& mesh, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw FormatError("cannot write file: " + path);
    struct Header { uint32_t magic; uint32_t vertexCount; uint32_t indexCount;
                    uint32_t boneCount; char name[32]; } header{};
    header.magic = kMagicLegacy;
    header.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    header.indexCount = static_cast<uint32_t>(mesh.indices.size());
    header.boneCount = static_cast<uint32_t>(mesh.bones.size());
    std::strncpy(header.name, mesh.objectName.c_str(), sizeof(header.name) - 1);
    writePod(out, header);

    for (const Vertex& v : mesh.vertices) {
        struct LegacyVertex {
            float position[3]; float normal[3]; float uv[2];
            uint16_t boneIndex[4]; uint16_t boneWeightQ[4];
        } lv{};
        std::memcpy(lv.position, v.position, sizeof(lv.position));
        std::memcpy(lv.normal, v.normal, sizeof(lv.normal));
        std::memcpy(lv.uv, v.uv, sizeof(lv.uv));
        for (uint32_t k = 0; k < 4; ++k) {
            lv.boneIndex[k] = static_cast<uint16_t>(v.boneIndex[k]);
            lv.boneWeightQ[k] =
                static_cast<uint16_t>(std::min(1.0f, std::max(0.0f, v.boneWeight[k])) * 65535.0f);
        }
        writePod(out, lv);
    }
    out.write(reinterpret_cast<const char*>(mesh.indices.data()),
              static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    for (const Bone& b : mesh.bones) {
        char name[24]{};
        std::strncpy(name, b.name.c_str(), sizeof(name) - 1);
        out.write(name, sizeof(name));
        writePod(out, b.parent);
        const auto toRow = [](const Mat4& m) {
            std::array<float, 12> r = {{ m.m[0], m.m[4], m.m[8],  m.m[12],
                            m.m[1], m.m[5], m.m[9],  m.m[13],
                            m.m[2], m.m[6], m.m[10], m.m[14] }};
            return r;
        };
        const auto bind = toRow(b.inverseBind);
        out.write(reinterpret_cast<const char*>(bind.data()), bind.size() * sizeof(float));
        const Mat4& pose = mesh.poseLocal[&b - &mesh.bones[0]];
        const auto poseRow = toRow(pose);
        out.write(reinterpret_cast<const char*>(poseRow.data()),
                  poseRow.size() * sizeof(float));
    }
}

void writeCurrentMesh(const SkinnedMesh& mesh, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw FormatError("cannot write file: " + path);
    struct Header { uint32_t magic; uint32_t version; uint32_t vertexCount;
                    uint32_t indexCount; uint32_t boneCount;
                    float animationTime; char name[32]; } header{};
    header.magic = kMagicCurrent;
    header.version = 2;
    header.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    header.indexCount = static_cast<uint32_t>(mesh.indices.size());
    header.boneCount = static_cast<uint32_t>(mesh.bones.size());
    header.animationTime = mesh.animationTime;
    std::strncpy(header.name, mesh.objectName.c_str(), sizeof(header.name) - 1);
    writePod(out, header);
    for (const Vertex& v : mesh.vertices) writePod(out, v);
    out.write(reinterpret_cast<const char*>(mesh.indices.data()),
              static_cast<std::streamsize>(mesh.indices.size() * sizeof(uint32_t)));
    for (uint32_t b = 0; b < mesh.bones.size(); ++b) {
        const auto nameLen = static_cast<uint16_t>(
            std::min<size_t>(mesh.bones[b].name.size(), 65535));
        writePod(out, nameLen);
        out.write(mesh.bones[b].name.data(), nameLen);
        writePod(out, mesh.bones[b].parent);
        writePod(out, mesh.bones[b].inverseBind);
        writePod(out, mesh.bones[b].localTransform);
        writePod(out, mesh.poseLocal[b]);
    }
}

} // namespace skr
