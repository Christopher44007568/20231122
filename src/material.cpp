#include "capture.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>

#include "sha256.hpp"
#include "util.hpp"

namespace inspect {
namespace {

bool parseFloat(const std::string& token, float& value) {
    try {
        std::size_t consumed = 0;
        const float parsed = std::stof(token, &consumed);
        if (consumed != token.size() || !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace

bool loadMaterial(const std::filesystem::path& path, Material& material,
                  std::string& error) {
    if (!readBinaryFile(path, material.bytes)) {
        error = "cannot read material: " + path.string();
        return false;
    }
    material.path = path;
    material.byte_size = material.bytes.size();
    material.sha256 = sha256Hex(material.bytes);
    material.vertices.clear();

    const std::string text(material.bytes.begin(), material.bytes.end());
    std::istringstream lines(text);
    std::string line;
    std::uint32_t vertex_index = 0;
    std::uint64_t byte_offset = 0;
    while (std::getline(lines, line)) {
        const std::uint64_t line_offset = byte_offset;
        byte_offset += static_cast<std::uint64_t>(line.size()) + 1U;
        const std::string content = trim(line);
        if (content.empty() || content.front() == '#') {
            continue;
        }

        std::istringstream fields(content);
        std::string tag;
        fields >> tag;
        if (tag != "v") {
            error = "unsupported material record at offset " +
                    std::to_string(line_offset);
            return false;
        }
        InputVertex vertex{};
        bool valid = true;
        for (float& value : vertex.position) {
            std::string token;
            valid = valid && static_cast<bool>(fields >> token) &&
                    parseFloat(token, value);
        }
        for (float& value : vertex.color) {
            std::string token;
            valid = valid && static_cast<bool>(fields >> token) &&
                    parseFloat(token, value);
        }
        if (!valid) {
            error = "invalid vertex at material offset " +
                    std::to_string(line_offset);
            return false;
        }
        vertex.source_offset = static_cast<std::uint32_t>(line_offset);
        vertex.vertex_index = vertex_index++;
        material.vertices.push_back(vertex);
    }

    if (material.vertices.empty() || material.vertices.size() % 3U != 0U) {
        error = "material vertex count must be a non-zero multiple of three";
        return false;
    }
    if (material.vertices.size() > std::numeric_limits<std::uint32_t>::max()) {
        error = "material vertex count is too large";
        return false;
    }
    return true;
}

std::vector<GsRecord> emulateGeometryOutput(const Material& material) {
    std::vector<GsRecord> records;
    records.reserve(material.vertices.size());
    for (std::uint32_t primitive = 0; primitive < material.triangleCount();
         ++primitive) {
        const InputVertex& a = material.vertices[primitive * 3U];
        const InputVertex& b = material.vertices[primitive * 3U + 1U];
        const InputVertex& c = material.vertices[primitive * 3U + 2U];
        const float cx = (a.position[0] + b.position[0] + c.position[0]) / 3.0f;
        const float cy = (a.position[1] + b.position[1] + c.position[1]) / 3.0f;
        const float cz = (a.position[2] + b.position[2] + c.position[2]) / 3.0f;
        const InputVertex* vertices[3] = {&a, &b, &c};
        for (std::uint32_t edge = 0; edge < 3; ++edge) {
            const InputVertex& first = *vertices[edge];
            const InputVertex& second = *vertices[(edge + 1U) % 3U];
            GsRecord record{};
            record.p0[0] = cx + (first.position[0] - cx) * 0.96f;
            record.p0[1] = cy + (first.position[1] - cy) * 0.96f;
            record.p0[2] = cz + (first.position[2] - cz) * 0.96f;
            record.p0[3] = 1.0f;
            record.p1[0] = cx + (second.position[0] - cx) * 0.96f;
            record.p1[1] = cy + (second.position[1] - cy) * 0.96f;
            record.p1[2] = cz + (second.position[2] - cz) * 0.96f;
            record.p1[3] = 1.0f;
            for (std::uint32_t channel = 0; channel < 3; ++channel) {
                record.color[channel] =
                    (first.color[channel] + second.color[channel]) * 0.5f;
            }
            record.color[3] = 1.0f;
            record.source_offset =
                std::min(first.source_offset, second.source_offset);
            record.primitive = primitive;
            record.edge = edge;
            record.input_vertex = first.vertex_index;
            records.push_back(record);
        }
    }
    return records;
}

std::vector<std::uint8_t> packRecords(const std::vector<GsRecord>& records) {
    std::vector<std::uint8_t> bytes(records.size() * sizeof(GsRecord));
    if (!bytes.empty()) {
        std::memcpy(bytes.data(), records.data(), bytes.size());
    }
    return bytes;
}

bool unpackRecords(const std::vector<std::uint8_t>& bytes,
                   std::vector<GsRecord>& records) {
    if (bytes.size() % sizeof(GsRecord) != 0U) {
        return false;
    }
    records.resize(bytes.size() / sizeof(GsRecord));
    if (!bytes.empty()) {
        std::memcpy(records.data(), bytes.data(), bytes.size());
    }
    return true;
}

}  // namespace inspect
