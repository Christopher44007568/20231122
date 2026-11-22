#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace inspect {

constexpr std::uint32_t kInputVertexStride = 32;
constexpr std::uint32_t kCaptureRecordBytes = 64;

struct InputVertex {
    float position[3];
    float color[3];
    std::uint32_t source_offset;
    std::uint32_t vertex_index;
};

struct GsRecord {
    float p0[4];
    float p1[4];
    float color[4];
    std::uint32_t source_offset;
    std::uint32_t primitive;
    std::uint32_t edge;
    std::uint32_t input_vertex;
};

static_assert(sizeof(InputVertex) == kInputVertexStride,
              "InputVertex must match the Vulkan vertex stride");
static_assert(sizeof(GsRecord) == kCaptureRecordBytes,
              "GsRecord must match the GLSL std430 layout");

struct Material {
    std::filesystem::path path;
    std::vector<std::uint8_t> bytes;
    std::vector<InputVertex> vertices;
    std::string sha256;
    std::uint64_t byte_size = 0;

    std::uint32_t triangleCount() const {
        return static_cast<std::uint32_t>(vertices.size() / 3U);
    }
};

bool loadMaterial(const std::filesystem::path& path, Material& material,
                  std::string& error);
std::vector<GsRecord> emulateGeometryOutput(const Material& material);
std::vector<std::uint8_t> packRecords(const std::vector<GsRecord>& records);
bool unpackRecords(const std::vector<std::uint8_t>& bytes,
                   std::vector<GsRecord>& records);

std::vector<std::uint8_t> rasterizeRecords(const std::vector<GsRecord>& records,
                                           std::uint32_t width,
                                           std::uint32_t height);
bool writePpm(const std::filesystem::path& path,
              const std::vector<std::uint8_t>& rgba, std::uint32_t width,
              std::uint32_t height);

}  // namespace inspect
