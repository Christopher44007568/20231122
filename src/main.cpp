#include "cache_format.hpp"
#include "hash.hpp"
#include "image.hpp"
#include "platform.hpp"
#include "report.hpp"
#include "vulkan_review.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace lshc;

struct Options {
    fs::path legacyPath;
    fs::path currentPath;
    fs::path outputDirectory = "artifacts/shadow-review";
    fs::path fixtureDirectory = "fixtures";
    uint32_t width = 256;
    uint32_t height = 256;
    bool makeFixtures = false;
    bool selfTest = false;
    bool keepTemp = false;
    bool verbose = false;
};

void printUsage()
{
    std::cout
        << "lshc_review " LSHC_VERSION " - reviewable light-shadow cache audit\n\n"
        << "Usage:\n"
        << "  lshc_review [options]\n\n"
        << "Options:\n"
        << "  --legacy PATH       legacy/v1 shadow cache snapshot\n"
        << "  --current PATH      current/v2 shadow cache snapshot\n"
        << "  --out DIR           evidence directory (default artifacts/shadow-review)\n"
        << "  --fixtures DIR      fixture directory (default fixtures)\n"
        << "  --make-fixtures     write deterministic v1/v2 fixtures and exit\n"
        << "  --self-test         run parser, Vulkan, diff and cleanup checks\n"
        << "  --width N           offscreen framebuffer width (default 256)\n"
        << "  --height N          offscreen framebuffer height (default 256)\n"
        << "  --keep-temp         keep the temporary state for debugging\n"
        << "  --verbose           print per-object progress\n"
        << "  --help              show this help\n";
}

uint32_t parseExtent(const std::string& value, const char* option)
{
    try {
        const unsigned long parsed = std::stoul(value);
        if (parsed == 0ul || parsed > 2048ul) {
            throw std::out_of_range("extent");
        }
        return static_cast<uint32_t>(parsed);
    } catch (const std::exception&) {
        throw std::runtime_error(std::string("invalid value for ") + option +
                                 ": " + value);
    }
}

Options parseOptions(int argc, char** argv)
{
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        auto requireValue = [&](const char* option) -> std::string {
            if (index + 1 >= argc) {
                throw std::runtime_error(std::string("missing value for ") +
                                         option);
            }
            return argv[++index];
        };
        if (argument == "--legacy") {
            options.legacyPath = requireValue("--legacy");
        } else if (argument == "--current") {
            options.currentPath = requireValue("--current");
        } else if (argument == "--out") {
            options.outputDirectory = requireValue("--out");
        } else if (argument == "--fixtures") {
            options.fixtureDirectory = requireValue("--fixtures");
        } else if (argument == "--width") {
            options.width = parseExtent(requireValue("--width"), "--width");
        } else if (argument == "--height") {
            options.height = parseExtent(requireValue("--height"), "--height");
        } else if (argument == "--make-fixtures") {
            options.makeFixtures = true;
        } else if (argument == "--self-test") {
            options.selfTest = true;
        } else if (argument == "--keep-temp") {
            options.keepTemp = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--help" || argument == "-h") {
            printUsage();
            std::exit(0);
        } else {
            throw std::runtime_error("unknown option: " + argument);
        }
    }
    if (options.legacyPath.empty() != options.currentPath.empty()) {
        throw std::runtime_error("--legacy and --current must be supplied together");
    }
    return options;
}

std::string objectSlug(uint32_t objectId)
{
    std::ostringstream out;
    out << "object-" << std::setw(4) << std::setfill('0') << objectId;
    return out.str();
}

void copyFile(const fs::path& source, const fs::path& destination)
{
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error) {
        throw std::runtime_error("cannot create directory for " +
                                 destination.string() + ": " + error.message());
    }
    fs::copy_file(source, destination, fs::copy_options::overwrite_existing,
                  error);
    if (error) {
        throw std::runtime_error("cannot preserve snapshot " +
                                 source.string() + ": " + error.message());
    }
}

void writeBytes(const fs::path& path, const std::vector<uint8_t>& bytes)
{
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error("cannot write " + path.string());
    }
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    if (!output) {
        throw std::runtime_error("failed writing " + path.string());
    }
}

class TempDirectory {
public:
    explicit TempDirectory(const fs::path& parent)
    {
        static std::atomic<unsigned> sequence{0};
        const auto stamp = std::chrono::high_resolution_clock::now()
                               .time_since_epoch()
                               .count();
        m_path = parent / (".lshc-tmp-" + std::to_string(stamp) + "-" +
                           std::to_string(sequence.fetch_add(1)));
        std::error_code error;
        fs::create_directories(m_path, error);
        if (error) {
            throw std::runtime_error("cannot create temporary directory: " +
                                     error.message());
        }
        std::ofstream marker(m_path / "state.tmp");
        marker << "temporary review state\n";
        m_active = true;
    }

    ~TempDirectory() { remove(); }

    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;

    bool remove()
    {
        if (!m_active) {
            return !fs::exists(m_path);
        }
        std::error_code error;
        fs::remove_all(m_path, error);
        m_active = false;
        return !error && !fs::exists(m_path);
    }

    void keep() { m_active = false; }

    const fs::path& path() const { return m_path; }

private:
    fs::path m_path;
    bool m_active = false;
};

}  // namespace

namespace {

int runReview(const Options& options, ReviewResult& result);

void requireTrue(bool condition, const std::string& message)
{
    if (!condition) {
        throw std::runtime_error("self-test assertion failed: " + message);
    }
}

const ObjectReview* findObject(const ReviewResult& result, uint32_t objectId)
{
    for (const ObjectReview& object : result.objects) {
        if (object.objectId == objectId) {
            return &object;
        }
    }
    return nullptr;
}

std::string readText(const fs::path& path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("cannot read " + path.string());
    }
    return std::string((std::istreambuf_iterator<char>(input)),
                       std::istreambuf_iterator<char>());
}

void verifyChecksums(const fs::path& output)
{
    std::ifstream input(output / "checksums.sha256");
    if (!input) {
        throw std::runtime_error("self-test: checksums.sha256 missing");
    }
    std::string line;
    std::size_t entries = 0;
    while (std::getline(input, line)) {
        if (line.size() < 67u) {
            throw std::runtime_error("self-test: malformed checksum line");
        }
        const std::string expected = line.substr(0u, 64u);
        const std::string relative = line.substr(66u);
        const std::string actual = sha256File((output / relative).string());
        requireTrue(expected == actual,
                    "checksum mismatch for " + relative);
        ++entries;
    }
    requireTrue(entries >= 7u, "checksum manifest is unexpectedly small");
}

int runSelfTest(const Options& options)
{
    std::error_code error;
    fs::remove_all(options.outputDirectory, error);
    if (error) {
        throw std::runtime_error("cannot reset self-test directory: " +
                                 error.message());
    }
    const fs::path root = options.outputDirectory;
    const DemoFiles fixtures =
        writeDemoSnapshots(root / "fixtures");

    Options reviewOptions = options;
    reviewOptions.legacyPath = fixtures.legacyPath;
    reviewOptions.currentPath = fixtures.currentPath;
    reviewOptions.outputDirectory = root / "evidence";
    reviewOptions.width = 192;
    reviewOptions.height = 192;
    reviewOptions.selfTest = false;

    ReviewResult result;
    runReview(reviewOptions, result);
    requireTrue(result.legacySnapshot.formatVersion == 1u &&
                    result.legacySnapshot.recordStride == 48u,
                "legacy format must parse as v1/48-byte records");
    requireTrue(result.currentSnapshot.formatVersion == 2u &&
                    result.currentSnapshot.recordStride == 64u,
                "current format must parse as v2/64-byte records");
    requireTrue(result.objects.size() == 6u,
                "expected six reviewed object ids");

    const ObjectReview* object1 = findObject(result, 1u);
    const ObjectReview* object2 = findObject(result, 2u);
    const ObjectReview* object4 = findObject(result, 4u);
    const ObjectReview* object5 = findObject(result, 5u);
    const ObjectReview* object6 = findObject(result, 6u);
    requireTrue(object1 && object1->status == "compared",
                "object 1 should compare successfully");
    requireTrue(object1 && object1->diff &&
                    object1->diff->changedPixels == 0u,
                "format-only changes must not create false pixel differences");
    requireTrue(object2 && object2->diff &&
                    object2->diff->changedPixels > 0u,
                "object 2 should expose a pixel difference");
    requireTrue(object4 && object4->status == "failed" &&
                    object4->currentError.find("center/radius") !=
                        std::string::npos,
                "object 4 should fail validation for its finite radius");
    requireTrue(object4 && object4->continuedAfterFailure,
                "object 4 must not stop object 5");
    requireTrue(object5 && object5->status == "compared",
                "object 5 must be processed after object 4 fails");
    requireTrue(object5 && object5->diff &&
                    object5->diff->changedPixels == 0u,
                "object 5 should be pixel-identical across snapshots");
    requireTrue(object6 && object6->status == "truncated" &&
                    !object6->currentRecord,
                "object 6 should retain the truncated current record");
    requireTrue(result.truncated && result.truncated->hasObjectId &&
                    result.truncated->partialObjectId == 6u,
                "truncated tail must preserve the partial object id");
    requireTrue(result.cleanup.tempRemoved,
                "temporary state must be removed");
    requireTrue(result.cleanup.vulkanShutdown &&
                    result.cleanup.liveTransientObjects == 0u,
                "Vulkan resources must be released");
    requireTrue(fs::exists(reviewOptions.outputDirectory /
                           "snapshots/legacy.cache"),
                "legacy snapshot evidence must be retained");
    requireTrue(fs::exists(reviewOptions.outputDirectory /
                           "snapshots/current-truncated-tail.bin"),
                "truncated tail evidence must be retained");
    requireTrue(readText(reviewOptions.outputDirectory / "cleanup.json")
                    .find("\"temp_removed\":true") != std::string::npos,
                "cleanup evidence must confirm temp removal");

    const std::string records =
        readText(reviewOptions.outputDirectory / "records.jsonl");
    requireTrue(records.find("\"status\":\"truncated\"") != std::string::npos,
                "records.jsonl must include the truncated record");
    requireTrue(records.find("\"continued_after_failure\":true") !=
                    std::string::npos,
                "records.jsonl must prove continuation after failure");
    requireTrue(readText(reviewOptions.outputDirectory / "review.md")
                    .find("Processing continued") != std::string::npos,
                "review.md must describe continuation");
    verifyChecksums(reviewOptions.outputDirectory);

    std::cout << "SELF-TEST PASS\n"
              << "  legacy records: " << result.legacySnapshot.declaredRecordCount
              << "\n  current records: "
              << result.currentSnapshot.declaredRecordCount
              << "\n  truncated tail: " << result.truncated->availableBytes
              << " bytes\n  compared: "
              << std::count_if(
                     result.objects.begin(), result.objects.end(),
                     [](const ObjectReview& object) {
                         return object.status == "compared";
                     })
              << "\n  backend: " << result.runtime.backend << " / "
              << result.runtime.deviceName << "\n  evidence: "
              << reviewOptions.outputDirectory.generic_string() << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    try {
        Options options = parseOptions(argc, argv);
        if (options.makeFixtures) {
            const DemoFiles files =
                writeDemoSnapshots(options.fixtureDirectory);
            std::cout << "fixtures written:\n  "
                      << files.legacyPath.generic_string() << "\n  "
                      << files.currentPath.generic_string() << '\n';
            return 0;
        }
        if (options.selfTest) {
            return runSelfTest(options);
        }

        if (options.legacyPath.empty()) {
            options.legacyPath = options.fixtureDirectory /
                                 "snapshot-v1.4.2-legacy.cache";
            options.currentPath = options.fixtureDirectory /
                                  "snapshot-v2.0.0-current.cache";
            if (!fs::exists(options.legacyPath) ||
                !fs::exists(options.currentPath)) {
                writeDemoSnapshots(options.fixtureDirectory);
            }
        }

        ReviewResult result;
        const int status = runReview(options, result);
        std::size_t compared = 0;
        std::size_t failed = 0;
        std::size_t truncated = 0;
        for (const ObjectReview& object : result.objects) {
            compared += object.status == "compared" ? 1u : 0u;
            failed += object.status == "failed" ? 1u : 0u;
            truncated += object.status == "truncated" ? 1u : 0u;
        }
        std::cout << "review complete: " << compared << " compared, "
                  << failed << " failed, " << truncated << " truncated\n"
                  << "evidence: " << options.outputDirectory.generic_string()
                  << "\nreport: "
                  << (options.outputDirectory / "review.md").generic_string()
                  << '\n';
        if (result.truncated) {
            std::cout << "truncated record preserved at: "
                      << (options.outputDirectory /
                          result.truncatedTailRelative)
                             .generic_string()
                      << '\n';
        }
        return status;
    } catch (const std::exception& error) {
        std::cerr << "lshc_review: " << error.what() << '\n';
        return 2;
    }
}

namespace {

int runReview(const Options& options, ReviewResult& result)
{
    const fs::path output = options.outputDirectory;
    const fs::path frames = output / "frames";
    const fs::path snapshots = output / "snapshots";
    std::error_code directoryError;
    fs::remove_all(frames, directoryError);
    directoryError.clear();
    fs::remove_all(snapshots, directoryError);
    directoryError.clear();
    fs::create_directories(frames, directoryError);
    if (directoryError) {
        throw std::runtime_error("cannot create output directory: " +
                                 directoryError.message());
    }
    fs::create_directories(snapshots, directoryError);
    if (directoryError) {
        throw std::runtime_error("cannot create snapshot directory: " +
                                 directoryError.message());
    }

    TempDirectory temporary(output);
    result.startedAtUtc = utcNow();
    result.outputDirectory = output.generic_string();
    result.cleanup.tempDirectory = temporary.path().generic_string();

    copyFile(options.legacyPath, snapshots / "legacy.cache");
    copyFile(options.currentPath, snapshots / "current.cache");

    std::optional<ParseResult> legacyParse;
    std::optional<ParseResult> currentParse;
    try {
        legacyParse = parseShadowCache(options.legacyPath.string(), "v1.4.2");
    } catch (const std::exception& error) {
        result.warnings.push_back(std::string("legacy snapshot failed: ") +
                                  error.what());
    }
    try {
        currentParse = parseShadowCache(options.currentPath.string(), "v2.0.0");
    } catch (const std::exception& error) {
        result.warnings.push_back(std::string("current snapshot failed: ") +
                                  error.what());
    }

    if (legacyParse) {
        result.legacySnapshot = legacyParse->snapshot;
        result.legacyAnomalies = legacyParse->anomalies;
    } else {
        result.legacySnapshot.label = "v1.4.2";
        result.legacySnapshot.path = options.legacyPath.generic_string();
        result.legacySnapshot.formatName = "unavailable";
    }
    if (currentParse) {
        result.currentSnapshot = currentParse->snapshot;
        result.currentAnomalies = currentParse->anomalies;
    } else {
        result.currentSnapshot.label = "v2.0.0";
        result.currentSnapshot.path = options.currentPath.generic_string();
        result.currentSnapshot.formatName = "unavailable";
    }

    if (currentParse && currentParse->truncated) {
        result.truncated = currentParse->truncated;
        result.truncatedTailRelative =
            "snapshots/current-truncated-tail.bin";
        writeBytes(output / result.truncatedTailRelative,
                   currentParse->truncated->rawTail);
    } else if (legacyParse && legacyParse->truncated) {
        result.truncated = legacyParse->truncated;
        result.truncatedTailRelative = "snapshots/legacy-truncated-tail.bin";
        writeBytes(output / result.truncatedTailRelative,
                   legacyParse->truncated->rawTail);
    }

    const PlatformInfo platform = initializePlatform();
    result.runtime.glfwInitialized = platform.glfwInitialized;
    result.runtime.glfwVulkanSupported = platform.glfwVulkanSupported;
    result.runtime.glfwVersion = platform.glfwVersion;
    result.runtime.glfwError = platform.error;
    result.runtime.instanceExtensions = platform.requiredInstanceExtensions;
    if (!platform.glfwInitialized) {
        result.warnings.push_back(
            "GLFW unavailable; Vulkan review runs headless without a surface");
    }

    std::unique_ptr<VulkanReviewRenderer> renderer;
    try {
        renderer = std::make_unique<VulkanReviewRenderer>(
            options.width, options.height, platform.requiredInstanceExtensions);
    } catch (const std::exception& error) {
        result.warnings.push_back(std::string("Vulkan renderer unavailable: ") +
                                  error.what());
        result.runtime.backend = "unavailable";
    }
    if (renderer) {
        result.runtime.backend = "vulkan-offscreen";
        result.runtime.vulkanApi = renderer->vulkanApiVersion();
        result.runtime.deviceName = renderer->deviceName();
        result.runtime.driverName = renderer->driverName();
    } else {
        result.runtime.vulkanApi = "unavailable";
        result.runtime.deviceName = "unavailable";
        result.runtime.driverName = "unavailable";
    }

    std::map<uint32_t, const ShadowRecord*> legacyRecords;
    std::map<uint32_t, const ShadowRecord*> currentRecords;
    auto indexRecords = [&result](
                            const std::vector<ShadowRecord>& records,
                            std::map<uint32_t, const ShadowRecord*>& index,
                            const std::string& side) {
        for (const ShadowRecord& record : records) {
            const auto inserted = index.emplace(record.objectId, &record);
            if (!inserted.second) {
                result.warnings.push_back(
                    side + " snapshot contains duplicate object id " +
                    std::to_string(record.objectId) + "; first record wins");
            }
        }
    };
    if (legacyParse) {
        indexRecords(legacyParse->records, legacyRecords, "legacy");
    }
    if (currentParse) {
        indexRecords(currentParse->records, currentRecords, "current");
    }

    std::set<uint32_t> objectIds;
    for (const auto& entry : legacyRecords) {
        objectIds.insert(entry.first);
    }
    for (const auto& entry : currentRecords) {
        objectIds.insert(entry.first);
    }
    const std::optional<uint32_t> truncatedObjectId =
        result.truncated && result.truncated->hasObjectId
            ? std::optional<uint32_t>(result.truncated->partialObjectId)
            : std::nullopt;
    if (truncatedObjectId) {
        objectIds.insert(*truncatedObjectId);
    }

    for (uint32_t objectId : objectIds) {
        ObjectReview review;
        review.objectId = objectId;
        const auto legacyIt = legacyRecords.find(objectId);
        const auto currentIt = currentRecords.find(objectId);
        if (legacyIt != legacyRecords.end()) {
            review.legacyRecord = *legacyIt->second;
        }
        if (currentIt != currentRecords.end()) {
            review.currentRecord = *currentIt->second;
        }

        if (options.verbose) {
            std::cout << "review object " << objectId << "\n";
        }

        auto renderSide = [&](const ShadowRecord& record,
                              const std::string& side,
                              std::optional<RenderEvidence>& evidence,
                              std::string& error,
                              std::string& relativeFrame) {
            if (!renderer) {
                error = "Vulkan backend is unavailable";
                return;
            }
            if (!record.integrityOk) {
                error = "record failed CRC/content-hash validation";
                return;
            }
            try {
                auto rendered = renderer->renderShadow(record, side);
                evidence = std::move(rendered);
                const std::string fileName =
                    objectSlug(objectId) + "-" + side + ".ppm";
                const fs::path framePath = frames / fileName;
                try {
                    writePpm(framePath.string(), evidence->image);
                    relativeFrame = "frames/" + fileName;
                } catch (const std::exception& exception) {
                    error = std::string("frame readback retained, artifact "
                                        "write failed: ") +
                            exception.what();
                }
            } catch (const std::exception& exception) {
                error = exception.what();
                evidence.reset();
            } catch (...) {
                error = "unknown exception while rendering " + side + " object";
                evidence.reset();
            }
        };

        if (review.legacyRecord) {
            renderSide(*review.legacyRecord, "legacy", review.legacyRender,
                       review.legacyError, review.legacyFrame);
        } else {
            review.legacyError = "object is absent from the legacy snapshot";
        }
        if (review.currentRecord) {
            renderSide(*review.currentRecord, "current", review.currentRender,
                       review.currentError, review.currentFrame);
        } else if (truncatedObjectId && *truncatedObjectId == objectId) {
            review.currentError =
                "declared record is truncated at byte offset " +
                std::to_string(result.truncated->offset);
        } else {
            review.currentError = "object is absent from the current snapshot";
        }

        if (review.legacyRender && review.currentRender) {
            try {
                const DiffStats diff = compareImages(
                    review.legacyRender->image, review.currentRender->image);
                const std::string fileName = objectSlug(objectId) + "-diff.ppm";
                writeDiffPpm((frames / fileName).string(),
                             review.legacyRender->image,
                             review.currentRender->image);
                review.diff = diff;
                review.diffFrame = "frames/" + fileName;
            } catch (const std::exception& exception) {
                review.currentError =
                    std::string("image comparison failed: ") + exception.what();
            }
        }

        const bool truncatedHere =
            truncatedObjectId && *truncatedObjectId == objectId &&
            !review.currentRecord;
        if (truncatedHere) {
            review.status = "truncated";
        } else if (!review.legacyError.empty() ||
                   !review.currentError.empty()) {
            review.status = "failed";
        } else if (!review.legacyRecord) {
            review.status = "missing_legacy";
        } else if (!review.currentRecord) {
            review.status = "missing_current";
        } else if (review.diff) {
            review.status = "compared";
        } else {
            review.status = "failed";
        }
        result.objects.push_back(std::move(review));
    }

    for (std::size_t index = 0; index < result.objects.size(); ++index) {
        ObjectReview& review = result.objects[index];
        if (review.legacyError.empty() && review.currentError.empty()) {
            continue;
        }
        for (std::size_t later = index + 1; later < result.objects.size();
             ++later) {
            if (result.objects[later].legacyRender ||
                result.objects[later].currentRender) {
                review.continuedAfterFailure = true;
                break;
            }
        }
    }

    uint32_t liveTransientObjects = 0;
    if (renderer) {
        liveTransientObjects = renderer->liveTransientObjects();
        renderer->shutdown();
        liveTransientObjects = renderer->liveTransientObjects();
        result.cleanup.vulkanShutdown = !renderer->initialized() &&
                                        liveTransientObjects == 0;
        renderer.reset();
    } else {
        result.cleanup.vulkanShutdown = true;
    }
    shutdownPlatform();
    result.cleanup.liveTransientObjects = liveTransientObjects;

    if (options.keepTemp) {
        temporary.keep();
        result.cleanup.tempRemoved = false;
        result.warnings.push_back(
            "temporary state retained by --keep-temp for debugging");
    } else {
        result.cleanup.tempRemoved = temporary.remove();
        if (!result.cleanup.tempRemoved) {
            result.warnings.push_back("temporary state could not be removed");
        }
    }

    result.completedAtUtc = utcNow();
    writeReviewArtifacts(result);
    return 0;
}

}  // namespace


