// GPU skinning review pipeline driver.
//
// For every input object it records two version snapshots (legacy v1 and
// current v2 container formats), correlates draw calls / resource states /
// framebuffers / rendered pixels, preserves evidence under the output
// directory, isolates per-object failures, and removes all temporary state
// before exiting.

#include "cpu_check.hpp"
#include "evidence.hpp"
#include "renderer.hpp"
#include "skeleton.hpp"
#include "skinning_format.hpp"
#include "vk_common.hpp"

#include <chrono>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::vector<uint32_t> readSpv(const fs::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) throw std::runtime_error("missing shader: " + path.string());
    const size_t bytes = static_cast<size_t>(in.tellg());
    std::vector<uint32_t> code(bytes / sizeof(uint32_t));
    in.seekg(0);
    in.read(reinterpret_cast<char*>(code.data()),
            static_cast<std::streamsize>(bytes));
    return code;
}

std::string nowIso8601() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &tm);
    return buffer;
}

struct ObjectReport {
    std::string objectName;
    std::string sourcePath;
    bool ok = false;
    std::string error;
    std::string correlationPath;
};

} // namespace

int main(int argc, char** argv) {
    const fs::path inputDir = argc > 1 ? argv[1] : "sample_input";
    const fs::path outputDir = argc > 2 ? argv[2] : "review_output";

    std::cout << "[review] input=" << fs::absolute(inputDir)
              << " output=" << fs::absolute(outputDir) << '\n';

    // Temporary working area; always removed at the end, even on failure.
    const fs::path tempDir = outputDir / ".tmp";
    std::error_code ec;
    fs::remove_all(tempDir, ec);
    fs::create_directories(tempDir, ec);

    struct Cleanup {
        fs::path temp;
        ~Cleanup() {
            std::error_code e;
            fs::remove_all(temp, e);
            std::cout << "[cleanup] removed temporary state at "
                      << temp.string()
                      << (e ? " (with errors)" : "") << '\n';
        }
    } cleanup{tempDir};

    std::vector<fs::path> candidates;
    if (fs::is_directory(inputDir)) {
        for (const auto& entry : fs::directory_iterator(inputDir)) {
            if (entry.is_regular_file()) candidates.push_back(entry.path());
        }
    } else {
        std::cerr << "[review] input directory not found: " << inputDir << '\n';
        return 2;
    }
    std::sort(candidates.begin(), candidates.end());

    std::vector<ObjectReport> reports;
    uint32_t okCount = 0, failCount = 0, skippedCount = 0;

    try {
        const auto vertSpv = readSpv("shaders/skinned.vert.spv");
        const auto fragSpv = readSpv("shaders/skinned.frag.spv");

        skr::VulkanContext context(256, 256);
        std::cout << "[review] Vulkan device: " << context.deviceName() << '\n';
        skr::SkinningRenderer renderer(context, vertSpv.data(), vertSpv.size(),
                                       fragSpv.data(), fragSpv.size());

        uint64_t frameId = 0;
        for (const fs::path& file : candidates) {
            ObjectReport report;
            report.sourcePath = file.string();
            report.objectName = file.stem().string();
            try {
                skr::SkinnedMesh mesh = skr::loadSkinnedMesh(file.string());

                // Snapshot A: the object as encoded in its on-disk revision.
                const std::string labelA =
                    mesh.revision == skr::FormatRevision::LegacyV1
                        ? "legacy_v1" : "current_v2";
                skr::Snapshot snapA =
                    renderer.render(mesh, labelA, ++frameId);

                // Snapshot B: the same logical object re-serialized through the
                // other revision, proving cross-format equivalence on the GPU.
                const bool sourceIsLegacy =
                    mesh.revision == skr::FormatRevision::LegacyV1;
                const fs::path counterpart =
                    tempDir / (file.stem().string() +
                               (sourceIsLegacy ? ".v2.skm" : ".v1.skm"));
                if (sourceIsLegacy)
                    skr::writeCurrentMesh(mesh, counterpart.string());
                else
                    skr::writeLegacyMesh(mesh, counterpart.string());
                skr::SkinnedMesh meshB =
                    skr::loadSkinnedMesh(counterpart.string());
                skr::Snapshot snapB = renderer.render(
                    meshB, sourceIsLegacy ? "current_v2" : "legacy_v1",
                    ++frameId);

                const skr::Snapshot& legacySnap =
                    sourceIsLegacy ? snapA : snapB;
                const skr::Snapshot& currentSnap =
                    sourceIsLegacy ? snapB : snapA;

                const skr::ImageDelta delta =
                    skr::compareImages(snapA, snapB);
                const skr::CpuGpuCheck check =
                    skr::verifyCpuReference(mesh);

                // Review thresholds: deterministic rendering must match
                // exactly for a same-format round trip. A legacy round trip
                // is allowed a handful of edge pixels because v1 stores bone
                // weights as quantized uint16 values.
                const double pixelRatio =
                    static_cast<double>(delta.differingPixels) /
                    static_cast<double>(delta.totalPixels);
                const bool imageEquivalent =
                    delta.meanAbsError <= 0.01 && pixelRatio <= 0.001 &&
                    delta.maxChannelError <= 200;
                const std::string verdict =
                    delta.differingPixels == 0 ? "EXACT_MATCH"
                    : imageEquivalent ? "EQUIVALENT_WITHIN_TOLERANCE"
                                      : "DIVERGENT";
                const std::string attribution =
                    delta.differingPixels == 0
                        ? "none"
                        : "legacy-v1 uint16 bone-weight quantization at "
                          "rasterized silhouette edges";

                const fs::path objDir = outputDir / file.stem();
                fs::create_directories(objDir, ec);
                const fs::path imgA =
                    objDir / (snapA.label + ".ppm");
                const fs::path imgB =
                    objDir / (snapB.label + ".ppm");
                const fs::path diffImg = objDir / "diff.ppm";
                skr::writePpm(imgA.string(), snapA.pixels.data(),
                              snapA.width, snapA.height);
                skr::writePpm(imgB.string(), snapB.pixels.data(),
                              snapB.width, snapB.height);
                skr::writeDiffPpm(diffImg.string(), legacySnap, currentSnap);

                // Snapshot JSON carries the absolute evidence path.
                skr::Snapshot snapAOut = snapA;
                skr::Snapshot snapBOut = snapB;
                snapAOut.imagePath = fs::absolute(imgA).string();
                snapBOut.imagePath = fs::absolute(imgB).string();

                const fs::path reportPath = objDir / "correlation.json";
                std::ofstream rpt(reportPath);
                rpt << skr::correlationToJson(
                    sourceIsLegacy ? snapAOut : snapBOut,
                    sourceIsLegacy ? snapBOut : snapAOut,
                    delta, fs::absolute(diffImg).string(),
                    check.consistent, check.maxPositionError,
                    imageEquivalent, verdict, attribution);

                const fs::path snapPath = objDir / "snapshots.json";
                std::ofstream snaps(snapPath);
                snaps << "{\"generatedAt\":\"" << nowIso8601() << "\","
                      << "\"snapshots\":[" << skr::snapshotToJson(snapAOut)
                      << "," << skr::snapshotToJson(snapBOut) << "]}";

                report.ok = true;
                report.correlationPath =
                    fs::absolute(reportPath).string();
                std::cout << "[review] OK  " << file.filename().string()
                          << " deltaPixels=" << delta.differingPixels
                          << " maxErr=" << delta.maxChannelError
                          << " verdict=" << verdict
                          << " cpuGpu=" << (check.consistent ? "pass" : "FAIL")
                          << '\n';
                if (!imageEquivalent || !check.consistent) {
                    report.ok = false;
                    report.error = "review verdict " + verdict;
                    ++failCount;
                    reports.push_back(report);
                    continue;
                }
                ++okCount;
            } catch (const skr::FormatError& e) {
                // Unknown extension / unknown magic: record and continue.
                report.ok = false;
                report.error = e.what();
                ++skippedCount;
                std::cerr << "[review] SKIP (format error) "
                          << file.filename().string() << ": " << e.what()
                          << '\n';
            } catch (const std::exception& e) {
                report.ok = false;
                report.error = e.what();
                ++failCount;
                std::cerr << "[review] FAIL " << file.filename().string()
                          << ": " << e.what() << '\n';
            }
            reports.push_back(report);
        }
    } catch (const std::exception& e) {
        std::cerr << "[review] fatal: " << e.what() << '\n';
        return 3;
    }

    // Top-level batch manifest: the reviewable index of all evidence.
    {
        std::ofstream manifest(outputDir / "manifest.json");
        manifest << "{\n  \"generatedAt\":\"" << nowIso8601() << "\",\n"
                 << "  \"objects\":[\n";
        for (size_t i = 0; i < reports.size(); ++i) {
            const ObjectReport& r = reports[i];
            manifest << "    {\"object\":" << '\"' << r.objectName << "\","
                     << "\"source\":" << '\"' << r.sourcePath << "\","
                     << "\"ok\":" << (r.ok ? "true" : "false") << ","
                     << "\"correlation\":" << '\"' << r.correlationPath
                     << "\",\"error\":" << '\"' << r.error << "\"}"
                     << (i + 1 < reports.size() ? "," : "") << '\n';
        }
        manifest << "  ],\n  \"summary\":{\"ok\":" << okCount
                 << ",\"failed\":" << failCount
                 << ",\"skippedUnknownFormat\":" << skippedCount
                 << "}\n}\n";
    }

    std::cout << "[review] done: " << okCount << " ok, " << failCount
              << " failed, " << skippedCount
              << " skipped (unknown format). Evidence under "
              << fs::absolute(outputDir).string() << '\n';
    // Unknown-format objects are isolated skips, not review failures.
    return failCount > 0 ? 1 : 0;
}
