#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace {

struct Rgbe {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
    std::uint8_t e;
};

Rgbe toRgbe(float red, float green, float blue) {
    const float peak = std::max({red, green, blue});
    if (peak < 1.0e-32f) return {0, 0, 0, 0};
    int exponent = 0;
    const float mantissa = std::frexp(peak, &exponent);
    const float scale = mantissa * 256.0f / peak;
    return {
        static_cast<std::uint8_t>(std::clamp(red * scale, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(green * scale, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(blue * scale, 0.0f, 255.0f)),
        static_cast<std::uint8_t>(std::clamp(exponent + 128, 0, 255)),
    };
}

Rgbe samplePixel(int x, int y, int width, int height, bool revisionTwo) {
    const float u = static_cast<float>(x) / static_cast<float>(width - 1) - 0.5f;
    const float v = static_cast<float>(y) / static_cast<float>(height - 1) - 0.5f;
    const float exposure = revisionTwo ? 1.14f : 1.0f;

    float red = 0.025f + 0.20f * std::exp(-9.0f * (u + 0.32f) * (u + 0.32f) -
                                          6.0f * (v - 0.10f) * (v - 0.10f));
    float green = 0.035f + 0.47f * std::exp(-5.5f * u * u - 3.0f * (v + 0.18f) * (v + 0.18f));
    float blue = 0.08f + 0.78f * std::exp(-2.8f * (u - 0.08f) * (u - 0.08f) -
                                          4.0f * (v + 0.04f) * (v + 0.04f));

    const float sunU = revisionTwo ? -0.12f : -0.17f;
    const float sunV = revisionTwo ? -0.20f : -0.16f;
    const float sunDistance = (u - sunU) * (u - sunU) + (v - sunV) * (v - sunV);
    const float sun = std::exp(-150.0f * sunDistance);
    const float halo = std::exp(-18.0f * sunDistance);
    red += 4.6f * sun + 0.46f * halo;
    green += 2.15f * sun + 0.22f * halo;
    blue += 0.55f * sun + 0.08f * halo;

    const float streak = std::exp(-80.0f * (v - 0.28f - 0.12f * std::sin(u * 6.2f)) *
                                  (v - 0.28f - 0.12f * std::sin(u * 6.2f)));
    red += 0.055f * streak;
    green += 0.35f * streak;
    blue += 0.82f * streak;

    const float edge = std::max(0.0f, 0.5f - std::abs(u) - std::abs(v));
    red += 0.08f * edge;
    green += 0.015f * edge;
    blue += 0.12f * edge;

    return toRgbe(red * exposure, green * exposure, blue * exposure);
}

void appendAscii(std::vector<std::uint8_t>& out, const std::string& text) {
    out.insert(out.end(), text.begin(), text.end());
}

void appendRleChannel(std::vector<std::uint8_t>& out, const std::vector<std::uint8_t>& channel) {
    std::size_t cursor = 0;
    while (cursor < channel.size()) {
        const std::size_t run = std::min<std::size_t>(127, channel.size() - cursor);
        out.push_back(static_cast<std::uint8_t>(run));
        out.insert(out.end(), channel.begin() + static_cast<std::ptrdiff_t>(cursor),
                   channel.begin() + static_cast<std::ptrdiff_t>(cursor + run));
        cursor += run;
    }
}

std::vector<std::uint8_t> buildHdr(bool revisionTwo) {
    constexpr int width = 96;
    constexpr int height = 64;
    std::vector<std::uint8_t> out;
    appendAscii(out, "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n");
    appendAscii(out, revisionTwo ? "SOFTWARE=atlas-baker 1.9.0\n"
                                 : "SOFTWARE=atlas-baker 1.7.3\n");
    appendAscii(out, revisionTwo ? "EXPOSURE=1.140000\n" : "EXPOSURE=1.000000\n");
    appendAscii(out, "GAMMA=1.000000\n");
    appendAscii(out, revisionTwo ? "PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600\n"
                                 : "PRIMARIES=0.6400 0.3300 0.3000 0.6000 0.1500 0.0600\n");
    appendAscii(out, "\n-Y 64 +X 96\n");

    for (int y = 0; y < height; ++y) {
        out.push_back(2);
        out.push_back(2);
        out.push_back(static_cast<std::uint8_t>((width >> 8) & 0xff));
        out.push_back(static_cast<std::uint8_t>(width & 0xff));
        std::vector<std::uint8_t> channels[4];
        for (auto& channel : channels) channel.reserve(width);
        for (int x = 0; x < width; ++x) {
            const Rgbe pixel = samplePixel(x, y, width, height, revisionTwo);
            channels[0].push_back(pixel.r);
            channels[1].push_back(pixel.g);
            channels[2].push_back(pixel.b);
            channels[3].push_back(pixel.e);
        }
        for (const auto& channel : channels) appendRleChannel(out, channel);
    }
    return out;
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void writeBytes(const fs::path& path, const std::vector<std::uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

int main(int argc, char** argv) {
    const fs::path root = argc > 1 ? argv[1] : HDRPROV_SOURCE_DIR;
    const fs::path data = root / "data";
    fs::create_directories(data / "snapshots");
    fs::create_directories(data / "records");

    const auto v1 = buildHdr(false);
    const auto v2 = buildHdr(true);
    const auto partial = std::vector<std::uint8_t>(v2.begin(), v2.begin() + 409);
    writeBytes(data / "snapshots" / "hdr_atlas_v1.hdr", v1);
    writeBytes(data / "snapshots" / "hdr_atlas_v2.hdr", v2);
    writeBytes(data / "records" / "hdr_atlas_tile_07.partial", partial);

    json manifest;
    manifest["batch"] = "hdr-atlas-source-check";
    manifest["schema"] = "hdrprovenance/1";
    manifest["objects"] = json::array({
        {{"id", "snap-v1"},
         {"path", "snapshots/hdr_atlas_v1.hdr"},
         {"role", "version_snapshot"},
         {"revision", "v1"},
         {"expectedBytes", v1.size()}},
        {{"id", "tile-07"},
         {"path", "records/hdr_atlas_tile_07.partial"},
         {"role", "truncated_record"},
         {"revision", "transfer-07"},
         {"expectedBytes", 8192},
         {"sourceRevision", "v2"},
         {"sourceOffset", 18432}},
        {{"id", "snap-v2"},
         {"path", "snapshots/hdr_atlas_v2.hdr"},
         {"role", "version_snapshot"},
         {"revision", "v2"},
         {"expectedBytes", v2.size()}},
    });

    std::ofstream manifestOut(data / "manifest.json", std::ios::trunc);
    manifestOut << manifest.dump(2) << '\n';
    return 0;
}
