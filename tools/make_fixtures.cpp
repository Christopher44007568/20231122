#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct Vertex {
    float x;
    float y;
    float z;
    float u;
    float v;
};

struct Seam {
    std::uint16_t a;
    std::uint16_t b;
    std::uint16_t orientation;
    std::uint16_t confidence;
    float forward;
    float reverse;
};

void appendU16(std::vector<std::uint8_t>& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xffu));
    out.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
}

void appendF32(std::vector<std::uint8_t>& out, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    out.push_back(static_cast<std::uint8_t>(bits & 0xffu));
    out.push_back(static_cast<std::uint8_t>((bits >> 8) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((bits >> 16) & 0xffu));
    out.push_back(static_cast<std::uint8_t>((bits >> 24) & 0xffu));
}

void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) throw std::runtime_error("cannot write " + path.string());
}

std::vector<std::uint8_t> buildSnapshot(int version) {
    constexpr int columns = 13;
    constexpr int rows = 9;
    constexpr float pi = 3.14159265358979323846f;

    std::vector<Vertex> vertices;
    vertices.reserve(columns * rows);
    for (int row = 0; row < rows; ++row) {
        const float v = static_cast<float>(row) / static_cast<float>(rows - 1);
        for (int column = 0; column < columns; ++column) {
            const float u = static_cast<float>(column) / static_cast<float>(columns - 1);
            const float x = (u - 0.5f) * 3.15f;
            const float z = (v - 0.5f) * 2.05f;
            const float ridge =
                0.31f * std::sin((u * 2.0f - 0.22f) * pi * 2.0f) *
                    std::cos((v - 0.5f) * pi * 1.4f) +
                0.10f * std::sin((u + v) * pi * 3.0f);
            const float y = ridge + static_cast<float>(version - 1) * 0.018f;

            float uvU = u;
            float uvV = v;
            if (version == 1) {
                uvU = u * 0.94f + 0.07f * v;
                uvV = v * 0.94f - 0.025f * std::sin(u * pi);
            } else {
                uvU = u * 1.02f + 0.018f * std::sin(v * pi);
                uvV = v * 1.02f + 0.012f * std::sin(u * pi);
            }
            vertices.push_back({x, y, z, uvU, uvV});
        }
    }

    std::vector<std::uint16_t> indices;
    for (int row = 0; row < rows - 1; ++row) {
        for (int column = 0; column < columns - 1; ++column) {
            const std::uint16_t a =
                static_cast<std::uint16_t>(row * columns + column);
            const std::uint16_t b = static_cast<std::uint16_t>(a + 1);
            const std::uint16_t c = static_cast<std::uint16_t>(a + columns);
            const std::uint16_t d = static_cast<std::uint16_t>(c + 1);
            indices.push_back(a);
            indices.push_back(c);
            indices.push_back(b);
            indices.push_back(b);
            indices.push_back(c);
            indices.push_back(d);
        }
    }

    std::vector<Seam> seams;
    if (version == 2) {
        seams.push_back({static_cast<std::uint16_t>(4 * columns + 4),
                         static_cast<std::uint16_t>(4 * columns + 5),
                         1, 2, 0.00018f, 0.0102f});
        seams.push_back({static_cast<std::uint16_t>(5 * columns + 7),
                         static_cast<std::uint16_t>(5 * columns + 8),
                         1, 2, 0.00024f, 0.0087f});
        seams.push_back({static_cast<std::uint16_t>(4 * columns + 6),
                         static_cast<std::uint16_t>(5 * columns + 6),
                         0, 0, -0.00035f, 0.00035f});
    }

    std::vector<std::uint8_t> out;
    out.insert(out.end(), {'S', 'U', 'F', static_cast<std::uint8_t>('0' + version)});
    appendU16(out, static_cast<std::uint16_t>(version));
    appendU16(out, static_cast<std::uint16_t>(vertices.size()));
    appendU16(out, static_cast<std::uint16_t>(indices.size() / 3));
    appendU16(out, static_cast<std::uint16_t>(seams.size()));
    appendF32(out, version == 1 ? 0.94f : 1.02f);
    for (const auto& vertex : vertices) {
        appendF32(out, vertex.x);
        appendF32(out, vertex.y);
        appendF32(out, vertex.z);
        appendF32(out, vertex.u);
        appendF32(out, vertex.v);
    }
    for (const auto index : indices) appendU16(out, index);
    for (const auto& seam : seams) {
        appendU16(out, seam.a);
        appendU16(out, seam.b);
        appendU16(out, seam.orientation);
        appendU16(out, seam.confidence);
        appendF32(out, seam.forward);
        appendF32(out, seam.reverse);
    }
    return out;
}

std::vector<std::uint8_t> buildUnknownProbe() {
    std::vector<std::uint8_t> bytes = {
        'U', 'V', 'X', 0x00,
        's', 'e', 'a', 'm', '-', 'p', 'r', 'o', 'b', 'e', '/', 'v', '7', '\n',
        'e', 'x', 't', '=', '.', 'u', 'v', 'x', '\n',
        'c', 'o', 'v', 'e', 'r', 'a', 'g', 'e', '=', '0', '.', '5', '0', '0', '\n',
        'p', 'r', 'o', 'f', 'i', 'l', 'e', '=', 'u', 'n', 'r', 'e', 'g', 'i', 's', 't', 'e', 'r', 'e', 'd', '\n',
        0x1e, 0x00, 0x7f, 0x03, 0x2a, 0xd1, 0x55, 0x44,
        0x91, 0x02, 0x6c, 0xbf, 0x37, 0x18, 0xaa, 0x10,
    };
    while (bytes.size() < 96) {
        bytes.push_back(static_cast<std::uint8_t>(0x40 + bytes.size() % 31));
    }
    return bytes;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const fs::path root = argc > 1 ? fs::path(argv[1])
                                       : fs::path(".");
        const auto v1 = buildSnapshot(1);
        const auto v2 = buildSnapshot(2);
        const auto unknown = buildUnknownProbe();
        const fs::path v1Path = root / "data" / "snapshots" / "ribbed_sheet_v1.suf";
        const fs::path v2Path = root / "data" / "snapshots" / "ribbed_sheet_v2.suf";
        const fs::path unknownPath = root / "data" / "records" / "seam_probe.uvx";
        writeBytes(v1Path, v1);
        writeBytes(v2Path, v2);
        writeBytes(unknownPath, unknown);

        json manifest;
        manifest["schema"] = "surface-unfold-provenance/v1";
        manifest["surface"] = "ribbed_sheet";
        manifest["objects"] = json::array();
        manifest["objects"].push_back({
            {"id", "snap-v1"},
            {"path", "snapshots/ribbed_sheet_v1.suf"},
            {"role", "version_snapshot"},
            {"revision", "v1"},
            {"expectedBytes", v1.size()},
            {"sourceRevision", "materials/ribbed_sheet.obj@r18"},
            {"sourceOffset", 0},
        });
        manifest["objects"].push_back({
            {"id", "unknown-uvx"},
            {"path", "records/seam_probe.uvx"},
            {"role", "unknown_extension"},
            {"revision", "unknown"},
            {"expectedBytes", unknown.size()},
            {"sourceRevision", "intake:seam-probe@v7"},
            {"sourceOffset", 0},
        });
        manifest["objects"].push_back({
            {"id", "snap-v2"},
            {"path", "snapshots/ribbed_sheet_v2.suf"},
            {"role", "version_snapshot"},
            {"revision", "v2"},
            {"expectedBytes", v2.size()},
            {"sourceRevision", "materials/ribbed_sheet.obj@r19"},
            {"sourceOffset", 0},
        });
        const fs::path manifestPath = root / "data" / "manifest.json";
        fs::create_directories(manifestPath.parent_path());
        std::ofstream output(manifestPath, std::ios::trunc);
        output << manifest.dump(2) << '\n';
        if (!output) throw std::runtime_error("cannot write manifest");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "fixture generation failed: %s\n", error.what());
        return 1;
    }
}
