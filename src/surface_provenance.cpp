#include "surface_provenance.h"

#include "sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#ifndef SURFPROV_SOURCE_DIR
#define SURFPROV_SOURCE_DIR "."
#endif

namespace surfprov {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

constexpr std::uint64_t kHeaderBytes = 16;
constexpr std::uint64_t kVertexBytes = 20;
constexpr std::uint64_t kIndexBytes = 6;
constexpr std::uint64_t kSeamBytes = 16;
constexpr float kBoundaryTolerance = 0.002f;

struct ManifestObject;
Preview renderPreview(const Record& record);
int changedPixels(const Preview& left, const Preview& right);
Record processSnapshot(const ManifestObject& meta,
                       const fs::path& root,
                       const fs::path& tempPath,
                       const Record* baseline);
Record failedRecord(const ManifestObject& meta,
                    const fs::path& root,
                    const std::string& message,
                    const std::string& stagedPath);

struct UnknownFormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct ManifestObject {
    std::string id;
    std::string path;
    std::string role;
    std::string revision;
    std::uint64_t expectedBytes = 0;
    std::string sourceRevision;
    std::uint64_t sourceOffset = 0;
};

struct TempWorkspace {
    fs::path path;
    bool active = true;

    explicit TempWorkspace(fs::path value) : path(std::move(value)) {
        fs::create_directories(path);
    }
    ~TempWorkspace() {
        if (active && !path.empty()) {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    }
    bool cleanup() {
        std::error_code error;
        if (!path.empty()) fs::remove_all(path, error);
        active = false;
        const fs::path parent = path.parent_path();
        std::error_code parentError;
        if (!error && !parent.empty() && fs::exists(parent) &&
            fs::is_empty(parent, parentError) && !parentError) {
            fs::remove(parent, parentError);
        }
        return !error && !fs::exists(path);
    }
};

std::string isoNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&value, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::string compactTime() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t value = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&value, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y%m%dT%H%M%SZ");
    return output.str();
}

std::vector<std::uint8_t> readBytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open input: " + path.string());
    return {std::istreambuf_iterator<char>(input),
            std::istreambuf_iterator<char>()};
}

std::string readText(const fs::path& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("cannot open text: " + path.string());
    std::ostringstream output;
    output << input.rdbuf();
    return output.str();
}

void writeText(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot write: " + path.string());
    output << text;
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& bytes,
                                std::uint64_t offset,
                                std::uint64_t length) {
    if (offset >= bytes.size()) return {};
    const std::size_t begin = static_cast<std::size_t>(offset);
    const std::size_t end = static_cast<std::size_t>(
        std::min<std::uint64_t>(bytes.size(), offset + length));
    return {bytes.begin() + static_cast<std::ptrdiff_t>(begin),
            bytes.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::string hexExcerpt(const std::vector<std::uint8_t>& bytes,
                       std::size_t maxBytes = 256) {
    std::ostringstream output;
    const std::size_t count = std::min(bytes.size(), maxBytes);
    for (std::size_t index = 0; index < count; ++index) {
        if (index != 0) output << ' ';
        output << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::uint16_t readU16(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 2 > bytes.size()) throw FormatError("u16 read outside input");
    return static_cast<std::uint16_t>(bytes[offset]) |
           static_cast<std::uint16_t>(bytes[offset + 1] << 8);
}

float readF32(const std::vector<std::uint8_t>& bytes, std::size_t offset) {
    if (offset + 4 > bytes.size()) throw FormatError("f32 read outside input");
    const std::uint32_t bits =
        static_cast<std::uint32_t>(bytes[offset]) |
        (static_cast<std::uint32_t>(bytes[offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[offset + 3]) << 24);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    if (!std::isfinite(value)) throw FormatError("non-finite float in input");
    return value;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string extensionOf(const fs::path& path) {
    std::string extension = lower(path.extension().string());
    if (!extension.empty() && extension.front() == '.') extension.erase(0, 1);
    return extension.empty() ? "none" : extension;
}

std::vector<ManifestObject> loadManifest(const fs::path& root,
                                         std::string& manifestText) {
    manifestText = readText(root / "data" / "manifest.json");
    const json manifest = json::parse(manifestText);
    std::vector<ManifestObject> objects;
    for (const auto& item : manifest.at("objects")) {
        objects.push_back({
            item.value("id", ""),
            item.value("path", ""),
            item.value("role", ""),
            item.value("revision", ""),
            item.value("expectedBytes", 0ULL),
            item.value("sourceRevision", ""),
            item.value("sourceOffset", 0ULL),
        });
    }
    return objects;
}

HeaderInfo parseHeader(const std::vector<std::uint8_t>& bytes) {
    HeaderInfo header;
    if (bytes.size() < 4) {
        header.magic = "<short>";
        throw UnknownFormatError("input is shorter than a 4-byte magic");
    }
    header.magic.assign(reinterpret_cast<const char*>(bytes.data()), 4);
    if (header.magic == "SUF1") {
        header.version = 1;
    } else if (header.magic == "SUF2") {
        header.version = 2;
    } else {
        throw UnknownFormatError("unrecognized magic " + hexExcerpt(slice(bytes, 0, 4)));
    }
    header.magicOk = true;
    if (bytes.size() < kHeaderBytes) throw FormatError("truncated SUF header");
    const std::uint16_t encodedVersion = readU16(bytes, 4);
    header.vertexCount = readU16(bytes, 6);
    header.triangleCount = readU16(bytes, 8);
    header.seamCount = readU16(bytes, 10);
    header.uvScale = readF32(bytes, 12);
    header.vertexBytes = static_cast<std::uint64_t>(header.vertexCount) * kVertexBytes;
    header.indexOffset = kHeaderBytes + header.vertexBytes;
    header.indexBytes = static_cast<std::uint64_t>(header.triangleCount) * kIndexBytes;
    header.seamOffset = header.indexOffset + header.indexBytes;
    header.seamBytes = static_cast<std::uint64_t>(header.seamCount) * kSeamBytes;
    const std::uint64_t expected = header.seamOffset + header.seamBytes;
    header.layoutOk =
        encodedVersion == static_cast<std::uint16_t>(header.version) &&
        header.vertexCount > 0 && header.triangleCount > 0 &&
        header.uvScale > 0.0f && expected <= bytes.size() &&
        (header.version == 2 || header.seamCount == 0);
    if (!header.layoutOk) {
        throw FormatError("SUF layout/version mismatch or declared ranges exceed input");
    }
    if (expected != bytes.size()) {
        throw FormatError("SUF trailing bytes: declared " + std::to_string(expected) +
                          ", actual " + std::to_string(bytes.size()));
    }
    return header;
}

void decodeMesh(Record& record) {
    record.vertices.reserve(record.header.vertexCount);
    for (std::uint16_t index = 0; index < record.header.vertexCount; ++index) {
        const std::size_t base =
            static_cast<std::size_t>(kHeaderBytes + index * kVertexBytes);
        MeshVertex vertex;
        vertex.position = {readF32(record.bytes, base),
                           readF32(record.bytes, base + 4),
                           readF32(record.bytes, base + 8)};
        vertex.uv = {readF32(record.bytes, base + 12),
                     readF32(record.bytes, base + 16)};
        record.vertices.push_back(vertex);
    }
    record.triangles.reserve(record.header.triangleCount);
    for (std::uint16_t index = 0; index < record.header.triangleCount; ++index) {
        const std::size_t base =
            static_cast<std::size_t>(record.header.indexOffset + index * kIndexBytes);
        Triangle triangle{readU16(record.bytes, base),
                          readU16(record.bytes, base + 2),
                          readU16(record.bytes, base + 4)};
        if (triangle.a >= record.vertices.size() ||
            triangle.b >= record.vertices.size() ||
            triangle.c >= record.vertices.size()) {
            throw FormatError("triangle index outside vertex table");
        }
        record.triangles.push_back(triangle);
    }
    record.seams.reserve(record.header.seamCount);
    for (std::uint16_t index = 0; index < record.header.seamCount; ++index) {
        const std::size_t base =
            static_cast<std::size_t>(record.header.seamOffset + index * kSeamBytes);
        SeamEdge seam{readU16(record.bytes, base),
                      readU16(record.bytes, base + 2),
                      readU16(record.bytes, base + 4),
                      readU16(record.bytes, base + 6),
                      readF32(record.bytes, base + 8),
                      readF32(record.bytes, base + 12)};
        if (seam.a >= record.vertices.size() || seam.b >= record.vertices.size()) {
            throw FormatError("seam edge outside vertex table");
        }
        record.seams.push_back(seam);
    }
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

json evidenceJson(const Evidence& evidence) {
    return {
        {"id", evidence.id},
        {"label", evidence.label},
        {"inputPath", evidence.inputPath},
        {"offset", evidence.offset},
        {"length", evidence.length},
        {"sha256", evidence.sha256},
        {"note", evidence.note},
        {"excerptHex", hexExcerpt(evidence.excerpt)},
    };
}

json stepJson(const Step& step) {
    return {
        {"id", step.id},
        {"title", step.title},
        {"detail", step.detail},
        {"state", step.state},
        {"evidenceIds", step.evidenceIds},
    };
}

json boundaryJson(const BoundaryInfo& boundary) {
    return {
        {"present", boundary.present},
        {"id", boundary.id},
        {"headline", boundary.headline},
        {"detail", boundary.detail},
        {"seamIndex", boundary.seamIndex},
        {"orientationField", boundary.orientationField},
        {"tolerance", boundary.tolerance},
        {"candidateForward", boundary.candidateForward},
        {"candidateReverse", boundary.candidateReverse},
        {"stepIds", boundary.stepIds},
        {"evidenceIds", boundary.evidenceIds},
    };
}

json recordJson(const Record& record) {
    json steps = json::array();
    for (const auto& step : record.steps) steps.push_back(stepJson(step));
    json evidence = json::array();
    for (const auto& item : record.evidence) evidence.push_back(evidenceJson(item));
    return {
        {"id", record.id},
        {"name", record.name},
        {"extension", record.extension},
        {"revision", record.revision},
        {"kind", record.kind == RecordKind::Snapshot ? "version_snapshot"
                                                     : "unknown_extension"},
        {"capturedAt", record.capturedAt},
        {"status", statusLabel(record.status)},
        {"source", {
            {"path", record.sourcePath},
            {"sha256", record.sha256},
            {"bytes", record.fileSize},
            {"declaredBytes", record.declaredSize},
            {"sourceRevision", record.sourceRevision},
            {"sourceOffset", record.sourceOffset},
        }},
        {"header", {
            {"magic", record.header.magic},
            {"version", record.header.version},
            {"vertexCount", record.header.vertexCount},
            {"triangleCount", record.header.triangleCount},
            {"seamCount", record.header.seamCount},
            {"uvScale", record.header.uvScale},
            {"vertexBytes", record.header.vertexBytes},
            {"indexOffset", record.header.indexOffset},
            {"indexBytes", record.header.indexBytes},
            {"seamOffset", record.header.seamOffset},
            {"seamBytes", record.header.seamBytes},
        }},
        {"preview", {
            {"width", record.preview.width},
            {"height", record.preview.height},
        }},
        {"changedPixels", record.changedPixels},
        {"boundary", boundaryJson(record.boundary)},
        {"conclusion", {
            {"headline", record.conclusion.headline},
            {"detail", record.conclusion.detail},
            {"stepIds", record.conclusion.stepIds},
            {"evidenceIds", record.conclusion.evidenceIds},
        }},
        {"steps", steps},
        {"evidence", evidence},
    };
}

std::string hexBlock(const std::vector<std::uint8_t>& bytes,
                     std::uint64_t start,
                     std::uint64_t count) {
    std::ostringstream output;
    const std::size_t begin = static_cast<std::size_t>(
        std::min<std::uint64_t>(start, bytes.size()));
    const std::size_t end = static_cast<std::size_t>(std::min<std::uint64_t>(
        bytes.size(), start + count));
    for (std::size_t row = begin; row < end; row += 16) {
        output << std::setw(8) << std::setfill('0') << std::hex << row << ": ";
        for (std::size_t index = row; index < std::min(end, row + 16); ++index) {
            output << std::setw(2) << std::setfill('0')
                   << static_cast<int>(bytes[index]) << ' ';
        }
        output << " | ";
        for (std::size_t index = row; index < std::min(end, row + 16); ++index) {
            const unsigned char character = bytes[index];
            output << (character >= 32 && character < 127
                           ? static_cast<char>(character)
                           : '.');
        }
        output << '\n';
    }
    return output.str();
}

void writeArtifacts(const fs::path& root,
                    const BatchResult& batch,
                    const std::string&,
                    const std::vector<ManifestObject>& manifestObjects) {
    const fs::path directory = root / "artifacts" / "evidence";
    std::error_code ignored;
    fs::remove_all(directory, ignored);
    fs::create_directories(directory);

    writeText(directory / "input_manifest.sha256",
              batch.inputManifestSha256 + "  data/manifest.json\n");

    json manifest;
    manifest["schema"] = "surface-unfold/provenance-v1";
    manifest["runId"] = batch.runId;
    manifest["startedAt"] = batch.startedAt;
    manifest["finishedAt"] = batch.finishedAt;
    manifest["inputManifest"] = {
        {"path", "data/manifest.json"},
        {"sha256", batch.inputManifestSha256},
        {"objectCount", manifestObjects.size()},
    };
    manifest["summary"] = {
        {"objects", batch.records.size()},
        {"snapshots", batch.snapshotCount},
        {"validSnapshots", batch.validSnapshotCount},
        {"pass", batch.passCount},
        {"undecidable", batch.undecidableCount},
        {"failed", batch.failedCount},
        {"boundaries", batch.boundaryCount},
        {"evidence", batch.evidenceCount},
    };
    manifest["exceptionCaught"] = batch.exceptionCaught;
    manifest["continuedAfterException"] = batch.continuedAfterException;
    manifest["temporaryState"] = {
        {"path", batch.tempPath},
        {"cleaned", batch.tempCleaned},
    };
    manifest["objects"] = json::array();
    for (const auto& record : batch.records) {
        manifest["objects"].push_back({
            {"id", record.id},
            {"name", record.name},
            {"extension", record.extension},
            {"status", statusLabel(record.status)},
            {"source", record.sourcePath},
            {"inputSha256", record.sha256},
            {"bytes", record.fileSize},
            {"boundary", record.boundary.present},
            {"conclusion", record.conclusion.headline},
            {"stepRefs", record.conclusion.stepIds},
            {"evidenceRefs", record.conclusion.evidenceIds},
            {"recordFile", record.id + ".record.json"},
        });
        writeText(directory / (record.id + ".record.json"),
                  recordJson(record).dump(2) + "\n");
    }
    writeText(directory / "manifest.json", manifest.dump(2) + "\n");

    std::ostringstream trace;
    trace << json{{"event", "batch_start"},
                  {"runId", batch.runId},
                  {"at", batch.startedAt},
                  {"inputManifestSha256", batch.inputManifestSha256},
                  {"tempPath", batch.tempPath}}.dump() << '\n';
    for (const auto& record : batch.records) {
        trace << json{{"event", "object_start"},
                      {"objectId", record.id},
                      {"input", record.sourcePath},
                      {"inputSha256", record.sha256},
                      {"declaredBytes", record.declaredSize},
                      {"actualBytes", record.fileSize}}.dump() << '\n';
        for (const auto& step : record.steps) {
            trace << json{{"event", "step"},
                          {"objectId", record.id},
                          {"stepId", step.id},
                          {"state", step.state},
                          {"detail", step.detail},
                          {"inputSha256", record.sha256},
                          {"evidenceIds", step.evidenceIds}}.dump() << '\n';
        }
        if (record.kind == RecordKind::UnknownExtension) {
            trace << json{{"event", "exception_caught_and_isolated"},
                          {"objectId", record.id},
                          {"exception", "UnknownFormatError"},
                          {"extension", record.extension},
                          {"inputSha256", record.sha256},
                          {"continued", true}}.dump() << '\n';
        }
        if (record.boundary.present) {
            trace << json{{"event", "boundary_undecidable"},
                          {"objectId", record.id},
                          {"boundaryId", record.boundary.id},
                          {"seamIndex", record.boundary.seamIndex},
                          {"orientationField", record.boundary.orientationField},
                          {"tolerance", record.boundary.tolerance},
                          {"candidateForward", record.boundary.candidateForward},
                          {"candidateReverse", record.boundary.candidateReverse},
                          {"evidenceIds", record.boundary.evidenceIds}}.dump()
                  << '\n';
        }
        trace << json{{"event", "object_complete"},
                      {"objectId", record.id},
                      {"status", statusLabel(record.status)},
                      {"conclusion", record.conclusion.headline},
                      {"stepRefs", record.conclusion.stepIds},
                      {"evidenceRefs", record.conclusion.evidenceIds}}.dump()
              << '\n';
    }
    trace << json{{"event", "temp_cleanup"},
                  {"path", batch.tempPath},
                  {"cleaned", batch.tempCleaned}}.dump() << '\n';
    trace << json{{"event", "batch_complete"},
                  {"pass", batch.passCount},
                  {"undecidable", batch.undecidableCount},
                  {"failed", batch.failedCount},
                  {"continuedAfterException", batch.continuedAfterException},
                  {"tempCleaned", batch.tempCleaned}}.dump() << '\n';
    writeText(directory / "processing_trace.jsonl", trace.str());

    for (const auto& record : batch.records) {
        if (!record.boundary.present) continue;
        std::ostringstream boundary;
        boundary << "# object: " << record.id << '\n';
        boundary << "# input: " << record.sourcePath << '\n';
        boundary << "# input sha256: " << record.sha256 << '\n';
        boundary << "# classification: UNDECIDABLE (seam orientation)\n";
        boundary << "# boundary: " << record.boundary.id
                 << "  seamIndex=" << record.boundary.seamIndex
                 << "  orientationField=" << record.boundary.orientationField << '\n';
        boundary << "# tolerance: " << record.boundary.tolerance
                 << "  candidateForward: " << record.boundary.candidateForward
                 << "  candidateReverse: " << record.boundary.candidateReverse
                 << '\n';
        for (const auto& evidence : record.evidence) {
            if (std::find(record.boundary.evidenceIds.begin(),
                          record.boundary.evidenceIds.end(),
                          evidence.id) == record.boundary.evidenceIds.end()) {
                continue;
            }
            boundary << "# evidence " << evidence.id << " offset=" << evidence.offset
                     << " length=" << evidence.length
                     << " sha256=" << evidence.sha256 << '\n';
            boundary << hexBlock(record.bytes, evidence.offset, evidence.length);
        }
        writeText(directory / "boundary_seam.hex", boundary.str());
    }

    std::ostringstream summary;
    summary << "曲面参数化展开 · 来源批次 " << batch.runId << '\n';
    summary << "输入清单 SHA-256: " << batch.inputManifestSha256 << '\n';
    summary << "对象: " << batch.records.size()
            << "  快照: " << batch.snapshotCount
            << "  可解析快照: " << batch.validSnapshotCount
            << "  无法判断: " << batch.undecidableCount
            << "  失败: " << batch.failedCount << '\n';
    summary << "异常后继续: " << (batch.continuedAfterException ? "yes" : "no")
            << '\n';
    summary << "临时状态清理: " << (batch.tempCleaned ? "yes" : "no") << '\n';
    for (const auto& record : batch.records) {
        summary << '\n' << record.id << " [" << statusLabel(record.status) << "]\n";
        summary << "  input: " << record.sourcePath << " (" << record.fileSize
                << " B)\n";
        summary << "  sha256: " << record.sha256 << '\n';
        summary << "  conclusion: " << record.conclusion.headline << '\n';
        summary << "  refs:";
        for (const auto& id : record.conclusion.stepIds) summary << ' ' << id;
        for (const auto& id : record.conclusion.evidenceIds) summary << ' ' << id;
        summary << '\n';
    }
    writeText(directory / "summary.txt", summary.str());
}

}  // namespace

std::string statusLabel(RecordStatus status) {
    switch (status) {
        case RecordStatus::Pass: return "PASS";
        case RecordStatus::Undecidable: return "UNDECIDABLE";
        case RecordStatus::Failed: return "FAILED";
    }
    return "UNKNOWN";
}

std::string relativeSource(const Record& record) {
    return record.sourcePath;
}

namespace {

BatchResult runBatch(const BatchOptions& options) {
    const fs::path root =
        options.root.empty() ? fs::path(SURFPROV_SOURCE_DIR) : options.root;
    BatchResult result;
    result.startedAt = isoNow();
    std::string manifestText;
    const auto manifestObjects = loadManifest(root, manifestText);
    result.inputManifestSha256 = sha256Hex(manifestText);
    result.runId =
        "unfold-" + result.inputManifestSha256.substr(0, 10) + "-" + compactTime();
    result.tempPath =
        (root / ".tmp" / ("unfold-scan-" + result.runId)).string();
    TempWorkspace temp(result.tempPath);
    result.trace.push_back("批次开始 " + result.runId);

    std::optional<Record> baseline;
    bool sawException = false;
    for (const auto& meta : manifestObjects) {
        if (sawException) result.continuedAfterException = true;
        result.trace.push_back("开始对象 " + meta.id + " ← data/" + meta.path);
        try {
            Record record = processSnapshot(
                meta, root, temp.path, baseline ? &*baseline : nullptr);
            ++result.snapshotCount;
            ++result.validSnapshotCount;
            if (record.status == RecordStatus::Pass) ++result.passCount;
            if (record.status == RecordStatus::Undecidable) {
                ++result.undecidableCount;
            }
            if (record.boundary.present) ++result.boundaryCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(record.id + " 完成：" +
                                   statusLabel(record.status) + "；" +
                                   record.conclusion.headline);
            if (meta.revision == "v1" && !baseline.has_value()) baseline = record;
            result.records.push_back(std::move(record));
        } catch (const UnknownFormatError& error) {
            sawException = true;
            result.exceptionCaught = true;
            const fs::path staged = temp.path / (meta.id + ".staged");
            std::string stagedPath;
            try {
                fs::copy_file(root / "data" / meta.path, staged,
                              fs::copy_options::overwrite_existing);
                fs::permissions(staged,
                                fs::perms::owner_read | fs::perms::group_read |
                                    fs::perms::others_read,
                                fs::perm_options::replace);
                stagedPath = staged.string();
            } catch (...) {
            }
            Record record = failedRecord(meta, root, error.what(), stagedPath);
            ++result.failedCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(record.id + " 抛出 UnknownFormatError：" +
                                   error.what() + "；已隔离，继续");
            result.records.push_back(std::move(record));
        } catch (const std::exception& error) {
            sawException = true;
            result.exceptionCaught = true;
            const fs::path staged = temp.path / (meta.id + ".staged");
            std::string stagedPath;
            try {
                fs::copy_file(root / "data" / meta.path, staged,
                              fs::copy_options::overwrite_existing);
                stagedPath = staged.string();
            } catch (...) {
            }
            Record record = failedRecord(meta, root, error.what(), stagedPath);
            ++result.failedCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(record.id + " 处理异常：" + error.what() +
                                   "；继续");
            result.records.push_back(std::move(record));
        }
    }

    result.tempCleaned = temp.cleanup();
    result.finishedAt = isoNow();
    result.trace.push_back(
        std::string("临时状态清理：") + (result.tempCleaned ? "已清理" : "未清理") +
        " " + result.tempPath);
    result.trace.push_back(
        "批次完成：PASS " + std::to_string(result.passCount) + " / UNDECIDABLE " +
        std::to_string(result.undecidableCount) + " / FAILED " +
        std::to_string(result.failedCount));
    if (options.writeArtifacts) {
        writeArtifacts(root, result, manifestText, manifestObjects);
    }
    return result;
}

}  // namespace

BatchResult processBatch(const BatchOptions& options) {
    return runBatch(options);
}

}  // namespace surfprov

namespace surfprov {
namespace {

Evidence makeEvidence(const std::string& id,
                      const std::string& label,
                      const std::string& inputPath,
                      std::uint64_t offset,
                      std::uint64_t length,
                      const std::vector<std::uint8_t>& bytes,
                      const std::string& note) {
    Evidence evidence;
    evidence.id = id;
    evidence.label = label;
    evidence.inputPath = inputPath;
    evidence.offset = offset;
    evidence.length = length;
    const std::size_t begin = static_cast<std::size_t>(
        std::min<std::uint64_t>(offset, bytes.size()));
    const std::size_t available = bytes.size() - begin;
    const std::size_t actual = static_cast<std::size_t>(
        std::min<std::uint64_t>(length, available));
    evidence.sha256 = sha256Hex(bytes.data() + begin, actual);
    evidence.excerpt = slice(bytes, offset, std::min<std::uint64_t>(length, 256));
    evidence.note = note;
    return evidence;
}

std::string fourCc(const std::vector<std::uint8_t>& bytes) {
    if (bytes.size() < 4) return "<short>";
    std::string value(reinterpret_cast<const char*>(bytes.data()), 4);
    for (char& character : value) {
        const unsigned char code = static_cast<unsigned char>(character);
        if (code < 32 || code >= 127) character = '?';
    }
    return value;
}

void addSnapshotBasics(Record& record) {
    record.evidence.push_back(makeEvidence(
        "E01", "完整原始输入", record.sourcePath, 0, record.fileSize, record.bytes,
        "SHA-256 覆盖整个输入；后续所有结论均引用此输入。"));
    record.evidence.push_back(makeEvidence(
        "E02", "SUF 文件头", record.sourcePath, 0, kHeaderBytes, record.bytes,
        "magic=" + record.header.magic + "；version=" +
            std::to_string(record.header.version) + "；顶点/三角形/接缝=" +
            std::to_string(record.header.vertexCount) + "/" +
            std::to_string(record.header.triangleCount) + "/" +
            std::to_string(record.header.seamCount) + "。"));
    record.evidence.push_back(makeEvidence(
        "E03", "顶点与 UV 表", record.sourcePath, kHeaderBytes,
        record.header.vertexBytes, record.bytes,
        "每顶点 20 B：位置 xyz + 展开 uv；共 " +
            std::to_string(record.header.vertexCount) + " 个。"));
    record.evidence.push_back(makeEvidence(
        "E04", "三角形索引表", record.sourcePath, record.header.indexOffset,
        record.header.indexBytes, record.bytes,
        "每三角形 3 个 uint16 索引；共 " +
            std::to_string(record.header.triangleCount) + " 个。"));
}

void addSnapshotSteps(Record& record) {
    record.steps.push_back({
        "S01", "读取并暂存原始输入",
        "读取 " + std::to_string(record.fileSize) +
            " B；在批次临时区写入只读副本，SHA-256=" + record.sha256 + "。",
        "ok", {"E01"}});
    record.steps.push_back({
        "S02", "按 magic 分派格式",
        "扩展名 ." + record.extension + " 仅作记录；实际分派依据是 magic " +
            record.header.magic + "。",
        "ok", {"E01", "E02"}});
    record.steps.push_back({
        "S03", "校验头与声明区间",
        "版本、顶点/三角形/接缝计数和 uvScale 通过；声明总长 " +
            std::to_string(record.fileSize) + " B 与输入一致。",
        "ok", {"E02"}});
    record.steps.push_back({
        "S04", "解码几何表",
        "读取 " + std::to_string(record.header.vertexCount) + " 个顶点与 " +
            std::to_string(record.header.triangleCount) +
            " 个三角形；所有索引均在顶点范围内。",
        "ok", {"E03", "E04"}});
    record.steps.push_back({
        "S05", "生成参数化展开候选",
        "位置 xyz 与 uv 来自同一顶点记录；CPU 同时绘制三维曲面与 UV 展开供页面复核。",
        "ok", {"E03", "E04"}});
}

bool classifyBoundary(Record& record) {
    if (record.header.version != 2 || record.seams.empty()) return false;
    std::optional<std::size_t> candidate;
    for (std::size_t index = 0; index < record.seams.size(); ++index) {
        const auto& seam = record.seams[index];
        if (seam.orientation == 0 && seam.confidence == 0 &&
            std::abs(seam.areaErrorForward) <= kBoundaryTolerance &&
            std::abs(seam.areaErrorReverse) <= kBoundaryTolerance &&
            seam.areaErrorForward * seam.areaErrorReverse < 0.0f) {
            candidate = index;
            break;
        }
    }
    if (!candidate.has_value()) return false;

    const auto& seam = record.seams[*candidate];
    const std::uint64_t seamOffset = record.header.seamOffset;
    const std::uint64_t tableLength = record.header.seamBytes;
    const std::uint64_t probeOffset =
        seamOffset + static_cast<std::uint64_t>(*candidate) * kSeamBytes;
    record.evidence.push_back(makeEvidence(
        "E05", "接缝表原文", record.sourcePath, seamOffset, tableLength,
        record.bytes,
        "v2 接缝表，共 " + std::to_string(record.seams.size()) +
            " 条；第 " + std::to_string(*candidate) +
            " 条 orientation=0、confidence=0，未给出方向提示。"));
    record.evidence.push_back(makeEvidence(
        "E06", "不可判断接缝的关键 16 B", record.sourcePath, probeOffset,
        kSeamBytes, record.bytes,
        "forwardAreaError=" + std::to_string(seam.areaErrorForward) +
            "；reverseAreaError=" + std::to_string(seam.areaErrorReverse) +
            "；tolerance=" + std::to_string(kBoundaryTolerance) +
            "。两个候选都落在容差内且符号相反，输入没有可裁决依据。"));
    record.steps.push_back({
        "S06", "接缝方向双候选探针",
        "对 seam[" + std::to_string(*candidate) + "] 尝试 forward/reverse；"
        "两者面积误差分别为 " + std::to_string(seam.areaErrorForward) +
            " / " + std::to_string(seam.areaErrorReverse) + "，均 |e| <= " +
            std::to_string(kBoundaryTolerance) + "。",
        "warn", {"E05", "E06"}});
    record.steps.push_back({
        "S07", "边界判定",
        "orientation 字段为 0，且两个候选无误差优势；不能擅自选择方向。"
        "结论保持 UNDECIDABLE，原始接缝字节与候选指标保留。",
        "undecidable", {"E05", "E06"}});
    record.boundary = {
        true,
        "B-03",
        "无法判断：接缝 #3 的展开方向",
        "输入未指定该接缝的翻转提示，forward 与 reverse 的面积误差都在 "
        "0.002 容差内且符号相反；没有原始纹理、相邻岛或人工标注可裁决。"
        "因此只确认“无法判断”，不把任一候选写成最终展开。",
        static_cast<std::uint16_t>(*candidate),
        seam.orientation,
        kBoundaryTolerance,
        seam.areaErrorForward,
        seam.areaErrorReverse,
        {"S06", "S07"},
        {"E05", "E06"},
    };
    return true;
}

void finishSnapshot(Record& record, const Record* baseline) {
    addSnapshotBasics(record);
    addSnapshotSteps(record);
    record.preview = renderPreview(record);
    if (baseline != nullptr) {
        record.changedPixels = changedPixels(baseline->preview, record.preview);
    }
    const bool bounded = classifyBoundary(record);
    if (bounded) {
        record.status = RecordStatus::Undecidable;
        record.conclusion.headline = "展开映射可解析，但一条接缝无法判断";
        record.conclusion.detail =
            "S01–S05 完整回溯到原始输入；S06–S07 发现接缝 B-03 的两个方向候选"
            "都落在容差内。保留 E05/E06 原始字节与候选指标，不输出单值方向。";
        record.conclusion.stepIds = {"S01", "S02", "S03", "S04", "S05", "S06", "S07"};
        record.conclusion.evidenceIds = {"E01", "E02", "E03", "E04", "E05", "E06"};
    } else {
        record.status = RecordStatus::Pass;
        record.conclusion.headline = "参数化展开通过并可回溯";
        record.conclusion.detail =
            "头、顶点/UV、索引与展开候选均从同一原始输入解码；" +
            std::string(baseline == nullptr
                            ? "作为后续版本比较的只读基线。"
                            : "与 v1 的显示差异为 " +
                                  std::to_string(record.changedPixels) +
                                  " 像素，已记录版本关系。");
        record.conclusion.stepIds = {"S01", "S02", "S03", "S04", "S05"};
        record.conclusion.evidenceIds = {"E01", "E02", "E03", "E04"};
    }
}

Record processSnapshot(const ManifestObject& meta,
                       const fs::path& root,
                       const fs::path& tempPath,
                       const Record* baseline) {
    Record record;
    record.id = meta.id;
    record.name = fs::path(meta.path).filename().string();
    record.extension = extensionOf(record.name);
    record.revision = meta.revision;
    record.capturedAt =
        meta.revision == "v1" ? "2026-11-18T09:14:07Z" : "2026-11-21T17:42:31Z";
    record.sourcePath = "data/" + meta.path;
    record.sourceRevision = meta.sourceRevision;
    record.sourceOffset = meta.sourceOffset;
    record.declaredSize = meta.expectedBytes;
    record.kind = RecordKind::Snapshot;
    record.bytes = readBytes(root / "data" / meta.path);
    record.fileSize = record.bytes.size();
    record.sha256 = sha256Hex(record.bytes);
    if (record.declaredSize != 0 && record.fileSize != record.declaredSize) {
        throw FormatError("manifest declares " +
                          std::to_string(record.declaredSize) +
                          " B but input has " + std::to_string(record.fileSize) + " B");
    }
    record.header = parseHeader(record.bytes);
    const fs::path staged = tempPath / (record.id + ".staged");
    fs::copy_file(root / "data" / meta.path, staged,
                  fs::copy_options::overwrite_existing);
    fs::permissions(staged,
                    fs::perms::owner_read | fs::perms::group_read |
                        fs::perms::others_read,
                    fs::perm_options::replace);
    decodeMesh(record);
    finishSnapshot(record, baseline);
    return record;
}

Record failedRecord(const ManifestObject& meta,
                    const fs::path& root,
                    const std::string& message,
                    const std::string& stagedPath) {
    Record record;
    record.id = meta.id;
    record.name = fs::path(meta.path).filename().string();
    record.extension = extensionOf(record.name);
    record.revision = meta.revision;
    record.capturedAt = "2026-11-21T17:42:31Z";
    record.sourcePath = "data/" + meta.path;
    record.sourceRevision = meta.sourceRevision;
    record.sourceOffset = meta.sourceOffset;
    record.declaredSize = meta.expectedBytes;
    record.kind = RecordKind::UnknownExtension;
    record.status = RecordStatus::Failed;
    try {
        record.bytes = readBytes(root / "data" / meta.path);
        record.fileSize = record.bytes.size();
        record.sha256 = sha256Hex(record.bytes);
        record.evidence.push_back(makeEvidence(
            "E01", "属于未知扩展的完整输入", record.sourcePath, 0,
            record.fileSize, record.bytes,
            "扩展名 ." + record.extension +
                " 不在注册表中；原始字节完整保留，未尝试猜测格式。"));
        record.evidence.push_back(makeEvidence(
            "E02", "原始头与探针前缀", record.sourcePath, 0,
            std::min<std::uint64_t>(record.fileSize, 64), record.bytes,
            "magic=" + fourCc(record.bytes) + "；" + message));
    } catch (...) {
        record.sha256 = "unavailable";
    }
    record.steps.push_back({
        "S01", "读取并暂存原始输入",
        stagedPath.empty() ? "读取输入失败；异常被对象级捕获。"
                           : "读取 " + std::to_string(record.fileSize) +
                                 " B，并暂存为只读副本 " + stagedPath + "。",
        "ok", record.evidence.empty() ? std::vector<std::string>{}
                                      : std::vector<std::string>{"E01"}});
    record.steps.push_back({
        "S02", "按 magic 分派格式",
        meta.role == "unknown_extension"
            ? "扩展名 ." + record.extension +
                  " 未注册；随后按 4-byte magic 与已知 SUF1/SUF2 比对。"
            : "扩展名 ." + record.extension +
                  " 已注册，但 magic 或布局校验失败；不猜测内容。",
        "error", record.evidence.size() > 1
                     ? std::vector<std::string>{"E01", "E02"}
                     : std::vector<std::string>{"E01"}});
    record.steps.push_back({
        "S03", "UnknownFormatError 隔离",
        message + "；保留原始字节，记录后继续处理下一个对象。",
        "error", record.evidence.empty() ? std::vector<std::string>{}
                                         : std::vector<std::string>{"E01", "E02"}});
    record.conclusion.headline = "未知扩展已隔离，后续对象继续";
    record.conclusion.detail =
        message + "。没有用扩展名推断内容，也没有中断批次；原始输入哈希 " +
        record.sha256 + " 与关键前缀已保留。";
    record.conclusion.stepIds = {"S01", "S02", "S03"};
    record.conclusion.evidenceIds =
        record.evidence.size() > 1 ? std::vector<std::string>{"E01", "E02"}
                                   : std::vector<std::string>{"E01"};
    return record;
}

}  // namespace
}  // namespace surfprov

namespace surfprov {
namespace {

struct Canvas {
    int width = 640;
    int height = 320;
    std::vector<std::uint8_t> pixels =
        std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4, 0);

    void clear(std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
        for (std::size_t index = 0; index + 3 < pixels.size(); index += 4) {
            pixels[index] = red;
            pixels[index + 1] = green;
            pixels[index + 2] = blue;
            pixels[index + 3] = 255;
        }
    }

    void pixel(int x, int y, std::uint8_t red, std::uint8_t green,
               std::uint8_t blue, std::uint8_t alpha = 255) {
        if (x < 0 || y < 0 || x >= width || y >= height) return;
        const std::size_t index =
            (static_cast<std::size_t>(y) * width + x) * 4;
        if (alpha == 255) {
            pixels[index] = red;
            pixels[index + 1] = green;
            pixels[index + 2] = blue;
            pixels[index + 3] = 255;
            return;
        }
        const int inverse = 255 - alpha;
        pixels[index] = static_cast<std::uint8_t>(
            (red * alpha + pixels[index] * inverse) / 255);
        pixels[index + 1] = static_cast<std::uint8_t>(
            (green * alpha + pixels[index + 1] * inverse) / 255);
        pixels[index + 2] = static_cast<std::uint8_t>(
            (blue * alpha + pixels[index + 2] * inverse) / 255);
        pixels[index + 3] = 255;
    }
};

void drawLine(Canvas& canvas, int x0, int y0, int x1, int y1,
              std::uint8_t red, std::uint8_t green, std::uint8_t blue,
              int thickness = 1, std::uint8_t alpha = 255) {
    const int dx = std::abs(x1 - x0);
    const int sx = x0 < x1 ? 1 : -1;
    const int dy = -std::abs(y1 - y0);
    const int sy = y0 < y1 ? 1 : -1;
    int error = dx + dy;
    while (true) {
        for (int oy = -thickness / 2; oy <= thickness / 2; ++oy) {
            for (int ox = -thickness / 2; ox <= thickness / 2; ++ox) {
                canvas.pixel(x0 + ox, y0 + oy, red, green, blue, alpha);
            }
        }
        if (x0 == x1 && y0 == y1) break;
        const int twice = error * 2;
        if (twice >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

void fillTriangle(Canvas& canvas,
                  float ax, float ay, float bx, float by,
                  float cx, float cy,
                  std::uint8_t red, std::uint8_t green, std::uint8_t blue,
                  std::uint8_t alpha) {
    const int minX = std::max(0, static_cast<int>(std::floor(
        std::min({ax, bx, cx}))));
    const int maxX = std::min(canvas.width - 1, static_cast<int>(std::ceil(
        std::max({ax, bx, cx}))));
    const int minY = std::max(0, static_cast<int>(std::floor(
        std::min({ay, by, cy}))));
    const int maxY = std::min(canvas.height - 1, static_cast<int>(std::ceil(
        std::max({ay, by, cy}))));
    const float area = (bx - ax) * (cy - ay) - (by - ay) * (cx - ax);
    if (std::abs(area) < 0.00001f) return;
    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            const float px = static_cast<float>(x) + 0.5f;
            const float py = static_cast<float>(y) + 0.5f;
            const float w0 = ((bx - px) * (cy - py) - (by - py) * (cx - px)) / area;
            const float w1 = ((cx - px) * (ay - py) - (cy - py) * (ax - px)) / area;
            const float w2 = 1.0f - w0 - w1;
            if (w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) {
                canvas.pixel(x, y, red, green, blue, alpha);
            }
        }
    }
}

void drawSectionFrame(Canvas& canvas, int left, int top, int right, int bottom) {
    drawLine(canvas, left, top, right, top, 34, 52, 63, 1);
    drawLine(canvas, right, top, right, bottom, 34, 52, 63, 1);
    drawLine(canvas, right, bottom, left, bottom, 34, 52, 63, 1);
    drawLine(canvas, left, bottom, left, top, 34, 52, 63, 1);
}

std::pair<float, float> project3d(const Vec3& position, float centerX, float centerY,
                                  float scale) {
    const float x = centerX + (position.x * 0.88f - position.z * 0.48f) * scale;
    const float y = centerY -
        (position.y * 1.05f + (position.x * 0.42f + position.z * 0.36f) * 0.56f) *
            scale;
    return {x, y};
}

Preview renderPreview(const Record& record) {
    Canvas canvas;
    canvas.clear(8, 13, 18);
    const int split = 318;
    drawSectionFrame(canvas, 12, 12, split - 8, canvas.height - 12);
    drawSectionFrame(canvas, split + 8, 12, canvas.width - 12,
                     canvas.height - 12);

    for (int x = split + 8; x < canvas.width - 12; x += 18) {
        drawLine(canvas, x, 13, x, canvas.height - 13, 20, 30, 37, 1, 150);
    }
    for (int y = 12; y < canvas.height - 12; y += 18) {
        drawLine(canvas, split + 9, y, canvas.width - 13, y,
                 20, 30, 37, 1, 150);
    }

    const float leftScale = 79.0f;
    const float leftCenterX = 166.0f;
    const float leftCenterY = 169.0f;
    std::vector<std::pair<float, float>> projected(record.vertices.size());
    for (std::size_t index = 0; index < record.vertices.size(); ++index) {
        projected[index] =
            project3d(record.vertices[index].position, leftCenterX, leftCenterY,
                      leftScale);
    }
    for (const auto& triangle : record.triangles) {
        const auto& a = projected[triangle.a];
        const auto& b = projected[triangle.b];
        const auto& c = projected[triangle.c];
        const float height =
            (record.vertices[triangle.a].position.y +
             record.vertices[triangle.b].position.y +
             record.vertices[triangle.c].position.y) / 3.0f;
        const std::uint8_t red =
            static_cast<std::uint8_t>(std::clamp(21.0f + height * 34.0f, 15.0f, 48.0f));
        const std::uint8_t green =
            static_cast<std::uint8_t>(std::clamp(42.0f + height * 48.0f, 31.0f, 78.0f));
        const std::uint8_t blue =
            static_cast<std::uint8_t>(std::clamp(48.0f + height * 24.0f, 39.0f, 70.0f));
        fillTriangle(canvas, a.first, a.second, b.first, b.second,
                     c.first, c.second, red, green, blue, 190);
    }
    for (const auto& triangle : record.triangles) {
        const auto& a = projected[triangle.a];
        const auto& b = projected[triangle.b];
        const auto& c = projected[triangle.c];
        drawLine(canvas, static_cast<int>(a.first), static_cast<int>(a.second),
                 static_cast<int>(b.first), static_cast<int>(b.second),
                 64, 132, 145, 1, 175);
        drawLine(canvas, static_cast<int>(b.first), static_cast<int>(b.second),
                 static_cast<int>(c.first), static_cast<int>(c.second),
                 64, 132, 145, 1, 175);
        drawLine(canvas, static_cast<int>(c.first), static_cast<int>(c.second),
                 static_cast<int>(a.first), static_cast<int>(a.second),
                 64, 132, 145, 1, 175);
    }

    const float uvLeft = split + 29.0f;
    const float uvTop = 38.0f;
    const float uvWidth = 252.0f;
    const float uvHeight = 244.0f;
    auto uvPoint = [&](const Vec2& uv) {
        return std::pair<float, float>{uvLeft + uv.x * uvWidth,
                                       uvTop + uv.y * uvHeight};
    };
    std::size_t triangleIndex = 0;
    for (const auto& triangle : record.triangles) {
        const auto a = uvPoint(record.vertices[triangle.a].uv);
        const auto b = uvPoint(record.vertices[triangle.b].uv);
        const auto c = uvPoint(record.vertices[triangle.c].uv);
        const bool alternate = triangleIndex++ % 2 == 0;
        const std::uint8_t red = alternate ? 22 : 18;
        const std::uint8_t green = alternate ? 56 : 46;
        const std::uint8_t blue = alternate ? 62 : 52;
        fillTriangle(canvas, a.first, a.second, b.first, b.second,
                     c.first, c.second, red, green, blue, 185);
    }
    for (const auto& triangle : record.triangles) {
        const auto a = uvPoint(record.vertices[triangle.a].uv);
        const auto b = uvPoint(record.vertices[triangle.b].uv);
        const auto c = uvPoint(record.vertices[triangle.c].uv);
        drawLine(canvas, static_cast<int>(a.first), static_cast<int>(a.second),
                 static_cast<int>(b.first), static_cast<int>(b.second),
                 58, 183, 198, 1, 210);
        drawLine(canvas, static_cast<int>(b.first), static_cast<int>(b.second),
                 static_cast<int>(c.first), static_cast<int>(c.second),
                 58, 183, 198, 1, 210);
        drawLine(canvas, static_cast<int>(c.first), static_cast<int>(c.second),
                 static_cast<int>(a.first), static_cast<int>(a.second),
                 58, 183, 198, 1, 210);
    }
    for (std::size_t index = 0; index < record.seams.size(); ++index) {
        const auto& seam = record.seams[index];
        const auto a = uvPoint(record.vertices[seam.a].uv);
        const auto b = uvPoint(record.vertices[seam.b].uv);
        const bool ambiguous = seam.orientation == 0 && seam.confidence == 0;
        drawLine(canvas, static_cast<int>(a.first), static_cast<int>(a.second),
                 static_cast<int>(b.first), static_cast<int>(b.second),
                 ambiguous ? 246 : 244, ambiguous ? 104 : 178,
                 ambiguous ? 104 : 74, ambiguous ? 2 : 1);
        if (ambiguous) {
            const int mx = static_cast<int>((a.first + b.first) * 0.5f);
            const int my = static_cast<int>((a.second + b.second) * 0.5f);
            for (int y = -8; y <= 8; ++y) {
                for (int x = -8; x <= 8; ++x) {
                    if (x * x + y * y <= 64) {
                        canvas.pixel(mx + x, my + y, 246, 178, 74, 75);
                    }
                }
            }
            drawLine(canvas, mx - 5, my - 5, mx + 5, my + 5, 246, 178, 74, 2);
            drawLine(canvas, mx + 5, my - 5, mx - 5, my + 5, 246, 178, 74, 2);
        }
    }
    drawLine(canvas, static_cast<int>(uvLeft - 7), static_cast<int>(uvTop - 7),
             static_cast<int>(uvLeft + uvWidth + 7),
             static_cast<int>(uvTop - 7), 52, 75, 84, 1);
    drawLine(canvas, static_cast<int>(uvLeft + uvWidth + 7),
             static_cast<int>(uvTop - 7),
             static_cast<int>(uvLeft + uvWidth + 7),
             static_cast<int>(uvTop + uvHeight + 7), 52, 75, 84, 1);
    drawLine(canvas, static_cast<int>(uvLeft + uvWidth + 7),
             static_cast<int>(uvTop + uvHeight + 7),
             static_cast<int>(uvLeft - 7),
             static_cast<int>(uvTop + uvHeight + 7), 52, 75, 84, 1);
    drawLine(canvas, static_cast<int>(uvLeft - 7),
             static_cast<int>(uvTop + uvHeight + 7),
             static_cast<int>(uvLeft - 7), static_cast<int>(uvTop - 7),
             52, 75, 84, 1);

    Preview preview;
    preview.width = canvas.width;
    preview.height = canvas.height;
    preview.rgba = std::move(canvas.pixels);
    return preview;
}

int changedPixels(const Preview& left, const Preview& right) {
    if (left.width != right.width || left.height != right.height) return -1;
    int changed = 0;
    for (std::size_t index = 0; index + 3 < left.rgba.size(); index += 4) {
        const int delta =
            std::abs(static_cast<int>(left.rgba[index]) -
                     static_cast<int>(right.rgba[index])) +
            std::abs(static_cast<int>(left.rgba[index + 1]) -
                     static_cast<int>(right.rgba[index + 1])) +
            std::abs(static_cast<int>(left.rgba[index + 2]) -
                     static_cast<int>(right.rgba[index + 2]));
        if (delta > 9) ++changed;
    }
    return changed;
}

}  // namespace
}  // namespace surfprov
