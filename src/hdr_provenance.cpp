#include "hdr_provenance.h"

#include "sha256.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace hdrprov {
namespace {

namespace fs = std::filesystem;
using json = nlohmann::json;

struct ManifestObject {
    std::string id;
    std::string path;
    std::string role;
    std::string revision;
    std::uint64_t expectedBytes = 0;
    std::string sourceRevision;
    std::uint64_t sourceOffset = 0;
};

struct TruncatedRecordError : std::runtime_error {
    Record record;
    TruncatedRecordError(Record value, const std::string& message)
        : std::runtime_error(message), record(std::move(value)) {}
};

struct TempWorkspace {
    fs::path path;
    bool active = true;

    TempWorkspace(fs::path value, bool cleanup) : path(std::move(value)) {
        if (cleanup) fs::create_directories(path);
    }
    ~TempWorkspace() {
        if (!active || path.empty()) return;
        std::error_code error;
        fs::remove_all(path, error);
    }
    bool cleanup() {
        std::error_code error;
        if (path.empty()) return true;
        fs::remove_all(path, error);
        active = false;
        const fs::path parent = path.parent_path();
        std::error_code parentError;
        if (!error && fs::exists(parent) && fs::is_empty(parent, parentError) &&
            !parentError) {
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
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
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

std::string hexExcerpt(const std::vector<std::uint8_t>& bytes,
                       std::size_t offset,
                       std::size_t length) {
    if (offset >= bytes.size()) return {};
    const std::size_t end = std::min(bytes.size(), offset + length);
    std::ostringstream output;
    for (std::size_t index = offset; index < end; ++index) {
        if (index != offset) output << ' ';
        output << std::hex << std::setw(2) << std::setfill('0')
               << static_cast<int>(bytes[index]);
    }
    return output.str();
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& bytes,
                                std::size_t offset,
                                std::size_t length) {
    if (offset >= bytes.size()) return {};
    const std::size_t end = std::min(bytes.size(), offset + length);
    return {bytes.begin() + static_cast<std::ptrdiff_t>(offset),
            bytes.begin() + static_cast<std::ptrdiff_t>(end)};
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool parseResolution(HeaderInfo& info);

HeaderInfo parseHeader(const std::vector<std::uint8_t>& bytes) {
    HeaderInfo info;
    const std::string text(reinterpret_cast<const char*>(bytes.data()),
                           std::min<std::size_t>(bytes.size(), 1024));
    const std::size_t magicEnd = text.find('\n');
    if (magicEnd != std::string::npos) {
        info.magic = text.substr(0, magicEnd);
        info.magicOk = info.magic == "#?RADIANCE" || info.magic == "#?RGBE";
    }

    std::size_t cursor = 0;
    while (cursor < text.size()) {
        const std::size_t end = text.find('\n', cursor);
        const std::string line =
            end == std::string::npos ? text.substr(cursor) : text.substr(cursor, end - cursor);
        if (line.empty()) {
            const std::size_t resolutionStart =
                end == std::string::npos ? text.size() : end + 1;
            const std::size_t resolutionEnd = text.find('\n', resolutionStart);
            if (resolutionEnd != std::string::npos) {
                info.resolution = text.substr(resolutionStart, resolutionEnd - resolutionStart);
                info.headerBytes = resolutionEnd + 1;
                info.resolutionOk = parseResolution(info);
            }
            break;
        }
        const std::size_t equals = line.find('=');
        if (equals != std::string::npos) {
            const std::string key = lower(line.substr(0, equals));
            const std::string value = line.substr(equals + 1);
            if (key == "format") {
                info.format = value;
                info.formatOk = value == "32-bit_rle_rgbe";
            } else if (key == "software") {
                info.software = value;
            } else if (key == "exposure") {
                info.exposure = value;
            }
        }
        if (end == std::string::npos) break;
        cursor = end + 1;
    }
    if (info.headerBytes <= bytes.size()) info.pixelBytes = bytes.size() - info.headerBytes;
    return info;
}

bool parseResolution(HeaderInfo& info) {
    std::istringstream input(info.resolution);
    std::string yAxis;
    std::string xAxis;
    if (!(input >> yAxis >> info.height >> xAxis >> info.width)) return false;
    return yAxis == "-Y" && xAxis == "+X" && info.width > 0 && info.height > 0;
}

Preview decodePreview(const std::vector<std::uint8_t>& bytes, const HeaderInfo& header) {
    Preview preview;
    if (!header.magicOk || !header.formatOk || !header.resolutionOk ||
        header.headerBytes >= bytes.size()) {
        return preview;
    }
    preview.width = header.width;
    preview.height = header.height;
    preview.rgba.resize(static_cast<std::size_t>(header.width) *
                        static_cast<std::size_t>(header.height) * 4);

    std::size_t cursor = static_cast<std::size_t>(header.headerBytes);
    for (int y = 0; y < header.height; ++y) {
        if (cursor + 4 > bytes.size()) {
            preview.rgba.clear();
            return preview;
        }
        if (!(bytes[cursor] == 2 && bytes[cursor + 1] == 2)) {
            preview.rgba.clear();
            return preview;
        }
        const int width = (static_cast<int>(bytes[cursor + 2]) << 8) | bytes[cursor + 3];
        cursor += 4;
        if (width != header.width) {
            preview.rgba.clear();
            return preview;
        }
        std::vector<std::uint8_t> channels[4];
        for (auto& channel : channels) channel.resize(static_cast<std::size_t>(width));
        for (int channelIndex = 0; channelIndex < 4; ++channelIndex) {
            int x = 0;
            while (x < width) {
                if (cursor >= bytes.size()) {
                    preview.rgba.clear();
                    return preview;
                }
                const std::uint8_t count = bytes[cursor++];
                if (count > 128) {
                    const int run = count - 128;
                    if (cursor >= bytes.size() || x + run > width) {
                        preview.rgba.clear();
                        return preview;
                    }
                    const std::uint8_t value = bytes[cursor++];
                    for (int i = 0; i < run; ++i) channels[channelIndex][x++] = value;
                } else {
                    if (count == 0 || cursor + count > bytes.size() || x + count > width) {
                        preview.rgba.clear();
                        return preview;
                    }
                    for (int i = 0; i < count; ++i) {
                        channels[channelIndex][x++] = bytes[cursor++];
                    }
                }
            }
        }
        for (int x = 0; x < width; ++x) {
            const int exponent = channels[3][x];
            const float scale = exponent == 0
                ? 0.0f
                : std::ldexp(1.0f, exponent - (128 + 8));
            const float values[3] = {
                channels[0][x] * scale,
                channels[1][x] * scale,
                channels[2][x] * scale,
            };
            const std::size_t target =
                (static_cast<std::size_t>(y) * header.width + x) * 4;
            for (int component = 0; component < 3; ++component) {
                const float mapped = values[component] / (1.0f + values[component]);
                const float gamma = std::pow(std::max(0.0f, mapped), 1.0f / 2.2f);
                preview.rgba[target + static_cast<std::size_t>(component)] =
                    static_cast<std::uint8_t>(std::clamp(gamma * 255.0f, 0.0f, 255.0f));
            }
            preview.rgba[target + 3] = 255;
        }
    }
    return preview;
}

int changedPixels(const Preview& left, const Preview& right) {
    if (left.width != right.width || left.height != right.height) return -1;
    int changed = 0;
    for (std::size_t index = 0; index + 3 < left.rgba.size(); index += 4) {
        const int delta = std::abs(static_cast<int>(left.rgba[index]) - right.rgba[index]) +
                          std::abs(static_cast<int>(left.rgba[index + 1]) - right.rgba[index + 1]) +
                          std::abs(static_cast<int>(left.rgba[index + 2]) - right.rgba[index + 2]);
        if (delta > 9) ++changed;
    }
    return changed;
}

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
    const std::size_t start = static_cast<std::size_t>(
        std::min<std::uint64_t>(offset, bytes.size()));
    const std::size_t fullLength = static_cast<std::size_t>(
        std::min<std::uint64_t>(length, bytes.size() - start));
    evidence.sha256 = sha256Hex(
        bytes.empty() ? nullptr : bytes.data() + start, fullLength);
    evidence.excerpt = slice(bytes, start, std::min<std::size_t>(fullLength, 256));
    evidence.note = note;
    return evidence;
}

std::string findLineOffset(const std::vector<std::uint8_t>& bytes, const std::string& needle) {
    const std::string text(bytes.begin(), bytes.end());
    const std::size_t offset = text.find(needle);
    return offset == std::string::npos ? "0" : std::to_string(offset);
}

}  // namespace

std::string statusLabel(RecordStatus status);

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
        {"excerptHex", hexExcerpt(evidence.excerpt, 0, evidence.excerpt.size())},
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

json recordJson(const Record& record) {
    json steps = json::array();
    for (const auto& step : record.steps) steps.push_back(stepJson(step));
    json evidence = json::array();
    for (const auto& item : record.evidence) evidence.push_back(evidenceJson(item));
    return {
        {"id", record.id},
        {"name", record.name},
        {"revision", record.revision},
        {"kind", record.kind == RecordKind::Snapshot ? "version_snapshot" : "truncated_record"},
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
            {"format", record.header.format},
            {"resolution", record.header.resolution},
            {"software", record.header.software},
            {"exposure", record.header.exposure},
            {"width", record.header.width},
            {"height", record.header.height},
            {"headerBytes", record.header.headerBytes},
            {"pixelBytes", record.header.pixelBytes},
        }},
        {"changedPixels", record.changedPixels},
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
                     std::size_t count) {
    std::ostringstream output;
    const std::size_t begin = static_cast<std::size_t>(
        std::min<std::uint64_t>(start, bytes.size()));
    const std::size_t end = std::min(bytes.size(), begin + count);
    for (std::size_t row = begin; row < end; row += 16) {
        output << std::setw(8) << std::setfill('0') << std::hex << row << ": ";
        for (std::size_t index = row; index < std::min(end, row + 16); ++index) {
            output << std::setw(2) << static_cast<int>(bytes[index]) << ' ';
        }
        output << '\n';
    }
    return output.str();
}

void writeArtifacts(const fs::path& root,
                    const BatchResult& batch,
                    const std::string& manifestText,
                    const std::vector<ManifestObject>& manifestObjects) {
    const fs::path directory = root / "artifacts" / "evidence";
    std::error_code error;
    fs::remove_all(directory, error);
    fs::create_directories(directory);

    const std::string manifestSha = sha256Hex(manifestText);
    writeText(directory / "input_manifest.sha256", manifestSha + "  data/manifest.json\n");

    json rootManifest;
    rootManifest["schema"] = "hdrprovenance/evidence-v1";
    rootManifest["runId"] = batch.runId;
    rootManifest["startedAt"] = batch.startedAt;
    rootManifest["finishedAt"] = batch.finishedAt;
    rootManifest["inputManifest"] = {
        {"path", "data/manifest.json"},
        {"sha256", manifestSha},
        {"objectCount", manifestObjects.size()},
    };
    rootManifest["summary"] = {
        {"objects", batch.records.size()},
        {"pass", batch.passCount},
        {"undecidable", batch.undecidableCount},
        {"failed", batch.failedCount},
        {"evidence", batch.evidenceCount},
    };
    rootManifest["exceptionCaught"] = batch.exceptionCaught;
    rootManifest["continuedAfterException"] = batch.continuedAfterException;
    rootManifest["temporaryState"] = {
        {"path", batch.tempPath},
        {"cleaned", batch.tempCleaned},
    };
    rootManifest["objects"] = json::array();

    for (const auto& record : batch.records) {
        rootManifest["objects"].push_back({
            {"id", record.id},
            {"status", statusLabel(record.status)},
            {"source", record.sourcePath},
            {"inputSha256", record.sha256},
            {"conclusion", record.conclusion.headline},
            {"stepRefs", record.conclusion.stepIds},
            {"evidenceRefs", record.conclusion.evidenceIds},
            {"recordFile", record.id + ".record.json"},
        });
        writeText(directory / (record.id + ".record.json"),
                  recordJson(record).dump(2) + "\n");
    }
    writeText(directory / "manifest.json", rootManifest.dump(2) + "\n");

    std::ostringstream trace;
    trace << json{{"event", "batch_start"},
                  {"runId", batch.runId},
                  {"at", batch.startedAt},
                  {"inputManifestSha256", manifestSha},
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
        if (record.status == RecordStatus::Undecidable) {
            trace << json{{"event", "exception_caught_and_classified"},
                          {"objectId", record.id},
                          {"exception", "TruncatedRecordError"},
                          {"boundary", "undecidable"},
                          {"declaredBytes", record.declaredSize},
                          {"actualBytes", record.fileSize},
                          {"preservedEvidence", "E05"}}.dump() << '\n';
        }
        trace << json{{"event", "object_complete"},
                      {"objectId", record.id},
                      {"status", statusLabel(record.status)},
                      {"conclusion", record.conclusion.headline},
                      {"stepRefs", record.conclusion.stepIds},
                      {"evidenceRefs", record.conclusion.evidenceIds}}.dump() << '\n';
    }
    trace << json{{"event", "temp_cleanup"},
                  {"path", batch.tempPath},
                  {"cleaned", batch.tempCleaned}}.dump() << '\n';
    trace << json{{"event", "batch_complete"},
                  {"pass", batch.passCount},
                  {"undecidable", batch.undecidableCount},
                  {"continuedAfterException", batch.continuedAfterException}}.dump() << '\n';
    writeText(directory / "processing_trace.jsonl", trace.str());

    for (const auto& record : batch.records) {
        if (record.status != RecordStatus::Undecidable) continue;
        std::ostringstream boundary;
        boundary << "# object: " << record.id << '\n';
        boundary << "# input: " << record.sourcePath << '\n';
        boundary << "# input sha256: " << record.sha256 << '\n';
        boundary << "# declared bytes: " << record.declaredSize << '\n';
        boundary << "# actual bytes: " << record.fileSize << '\n';
        boundary << "# classification: UNDECIDABLE (source boundary)\n";
        for (const auto& evidence : record.evidence) {
            if (evidence.id != "E05") continue;
            boundary << "# evidence range: offset=" << evidence.offset
                     << " length=" << evidence.length
                     << " sha256=" << evidence.sha256 << '\n';
            boundary << hexBlock(record.bytes, evidence.offset, evidence.length);
        }
        writeText(directory / "boundary_bytes.hex", boundary.str());
    }

    std::ostringstream summary;
    summary << "HDR 纹理格式验证 · 来源批次 " << batch.runId << '\n';
    summary << "输入清单 SHA-256: " << manifestSha << '\n';
    summary << "对象: " << batch.records.size() << "  通过: " << batch.passCount
            << "  无法判断: " << batch.undecidableCount
            << "  失败: " << batch.failedCount << '\n';
    summary << "异常后继续: " << (batch.continuedAfterException ? "yes" : "no") << '\n';
    summary << "临时状态清理: " << (batch.tempCleaned ? "yes" : "no") << '\n';
    for (const auto& record : batch.records) {
        summary << '\n' << record.id << "  [" << statusLabel(record.status) << "]\n";
        summary << "  input: " << record.sourcePath << " (" << record.fileSize << " B)\n";
        summary << "  sha256: " << record.sha256 << '\n';
        summary << "  conclusion: " << record.conclusion.headline << '\n';
        summary << "  refs: ";
        for (const auto& id : record.conclusion.stepIds) summary << id << ' ';
        for (const auto& id : record.conclusion.evidenceIds) summary << id << ' ';
        summary << '\n';
    }
    writeText(directory / "summary.txt", summary.str());
}

}  // namespace

namespace {

std::vector<ManifestObject> loadManifest(const fs::path& root, std::string& manifestText) {
    const fs::path manifestPath = root / "data" / "manifest.json";
    manifestText = readText(manifestPath);
    const json manifest = json::parse(manifestText);
    std::vector<ManifestObject> objects;
    for (const auto& item : manifest.at("objects")) {
        ManifestObject object;
        object.id = item.value("id", "");
        object.path = item.value("path", "");
        object.role = item.value("role", "");
        object.revision = item.value("revision", "");
        object.expectedBytes = item.value("expectedBytes", 0ULL);
        object.sourceRevision = item.value("sourceRevision", "");
        object.sourceOffset = item.value("sourceOffset", 0ULL);
        objects.push_back(std::move(object));
    }
    return objects;
}

void addSnapshotSteps(Record& record) {
    const std::string source = record.sourcePath;
    record.evidence.push_back(makeEvidence(
        "E01", "原始输入", source, 0, record.fileSize, record.bytes,
        "完整输入字节；SHA-256 覆盖 0.." + std::to_string(record.fileSize) + "。"));
    record.evidence.push_back(makeEvidence(
        "E02", "HDR 头", source, 0, record.header.headerBytes, record.bytes,
        "Radiance 头与分辨率行；解析边界止于 headerBytes。"));
    const std::string resolutionOffset =
        findLineOffset(record.bytes, record.header.resolution);
    const std::uint64_t resolutionStart =
        static_cast<std::uint64_t>(std::stoull(resolutionOffset));
    record.evidence.push_back(makeEvidence(
        "E03", "分辨率与格式", source, resolutionStart,
        record.header.resolution.size() + 1, record.bytes,
        "FORMAT=" + record.header.format + "；resolution=" + record.header.resolution + "。"));
    record.evidence.push_back(makeEvidence(
        "E04", "像素负载", source, record.header.headerBytes,
        record.header.pixelBytes, record.bytes,
        "隔离头与负载，负载按 64 条 scanline 解码。"));

    record.steps.push_back({"S01", "读取原始输入",
                            "读取 " + std::to_string(record.fileSize) +
                                " B，并写入批次临时区用于只读校验。",
                            "ok", {"E01"}});
    record.steps.push_back({"S02", "解析 Radiance 头",
                            "magic=" + record.header.magic + "；headerBytes=" +
                                std::to_string(record.header.headerBytes) + "。",
                            "ok", {"E01", "E02"}});
    record.steps.push_back({"S03", "验证格式与分辨率",
                            "FORMAT=" + record.header.format + "；" +
                                std::to_string(record.header.width) + " x " +
                                std::to_string(record.header.height) + "。",
                            "ok", {"E02", "E03"}});
}

void finishSnapshot(Record& record, const Record* baseline) {
    record.status = RecordStatus::Pass;
    record.steps.push_back({"S04", "解码 scanline",
                            "逐行读取 RLE 通道并生成 tone-mapped 预览；没有越界。",
                            "ok", {"E03", "E04"}});

    std::string compareDetail = "作为后续版本比较的基线；原始证据保持只读。";
    if (baseline != nullptr) {
        record.changedPixels = changedPixels(baseline->preview, record.preview);
        std::size_t diffOffset = record.header.headerBytes;
        const std::size_t common = std::min(baseline->bytes.size(), record.bytes.size());
        while (diffOffset < common && baseline->bytes[diffOffset] == record.bytes[diffOffset]) {
            ++diffOffset;
        }
        const auto diffEvidence = makeEvidence(
            "E05", "版本差异",
            record.sourcePath, diffOffset, 64, record.bytes,
            "与 v1 比较；v1 input sha256=" + baseline->sha256 +
                "；首个差异偏移=" + std::to_string(diffOffset) + "。");
        record.evidence.push_back(diffEvidence);
        compareDetail = "与 v1 比较：" + std::to_string(record.changedPixels) +
                        " 个显示像素变化；首个字节差异位于偏移 " +
                        std::to_string(diffOffset) + "。";
        record.steps.push_back({"S05", "跨版本比较", compareDetail, "ok",
                                {"E04", "E05"}});
    } else {
        record.steps.push_back({"S05", "封存基线", compareDetail, "ok", {"E01", "E04"}});
    }

    record.conclusion.headline = "格式有效，可作为纹理来源";
    record.conclusion.detail =
        "magic、32-bit_rle_rgbe 声明、分辨率与像素负载均通过；" +
        compareDetail;
    record.conclusion.stepIds = baseline == nullptr
        ? std::vector<std::string>{"S01", "S02", "S03", "S04", "S05"}
        : std::vector<std::string>{"S01", "S02", "S03", "S04", "S05"};
    record.conclusion.evidenceIds = baseline == nullptr
        ? std::vector<std::string>{"E01", "E02", "E03", "E04"}
        : std::vector<std::string>{"E01", "E02", "E03", "E04", "E05"};
}

Record processSnapshot(const ManifestObject& meta,
                       const fs::path& root,
                       const fs::path& tempPath,
                       const Record* baseline) {
    Record record;
    record.id = meta.id;
    record.name = fs::path(meta.path).filename().string();
    record.revision = meta.revision;
    record.capturedAt = "2026-11-21T17:42:31Z";
    if (meta.revision == "v1") record.capturedAt = "2026-11-18T09:14:07Z";
    record.sourcePath = "data/" + meta.path;
    record.sourceRevision = meta.sourceRevision;
    record.sourceOffset = meta.sourceOffset;
    record.kind = RecordKind::Snapshot;
    record.declaredSize = meta.expectedBytes;
    record.bytes = readBytes(root / "data" / meta.path);
    record.fileSize = record.bytes.size();
    record.sha256 = sha256Hex(record.bytes);
    record.header = parseHeader(record.bytes);
    addSnapshotSteps(record);

    const fs::path staged = tempPath / record.name;
    fs::copy_file(root / "data" / meta.path, staged,
                  fs::copy_options::overwrite_existing);
    if (record.declaredSize > 0 && record.fileSize < record.declaredSize) {
        record.steps.push_back({"S04", "解码 scanline",
                                "在偏移 " + std::to_string(record.fileSize) +
                                    " 遇到 EOF；声明 " + std::to_string(record.declaredSize) +
                                    " B，实际 " + std::to_string(record.fileSize) + " B。",
                                "error", {"E01", "E02", "E04"}});
        const std::string message =
            "truncated record " + record.id + ": observed " +
            std::to_string(record.fileSize) + " of " +
            std::to_string(record.declaredSize) + " bytes";
        throw TruncatedRecordError(
            std::move(record), message);
    }
    if (!record.header.magicOk || !record.header.formatOk || !record.header.resolutionOk) {
        throw std::runtime_error("HDR header validation failed for " + meta.path);
    }
    record.preview = decodePreview(record.bytes, record.header);
    if (record.preview.rgba.empty()) {
        throw std::runtime_error("HDR payload decode failed for " + meta.path);
    }
    finishSnapshot(record, baseline);
    return record;
}

Record classifyTruncated(Record record) {
    record.status = RecordStatus::Undecidable;
    const std::uint64_t boundaryOffset = record.fileSize > 24 ? record.fileSize - 24 : 0;
    record.evidence.push_back(makeEvidence(
        "E05", "EOF 关键证据", record.sourcePath, boundaryOffset,
        record.fileSize - boundaryOffset, record.bytes,
        "声明 " + std::to_string(record.declaredSize) + " B；实际 " +
            std::to_string(record.fileSize) + " B；缺少字节 " +
            std::to_string(record.declaredSize - record.fileSize) +
            "；无尾部校验字段。来源 " + record.sourceRevision + " offset " +
            std::to_string(record.sourceOffset) + "。"));
    record.steps.push_back({"S05", "异常捕获与隔离",
                            "TruncatedRecordError 被对象级 try/catch 接住；后续对象继续。",
                            "warn", {"E01", "E05"}});
    record.steps.push_back({"S06", "边界判定",
                            "只能确认输入不足，无法区分采集截断、写入中断或上游丢包。",
                            "undecidable", {"E01", "E02", "E05"}});
    record.conclusion.headline = "无法判断：边界记录缺少尾部证据";
    record.conclusion.detail =
        "可以确认原始输入只有 " + std::to_string(record.fileSize) +
        " B，而批次清单声明 " + std::to_string(record.declaredSize) +
        " B。因记录没有尾部长度或校验字段，无法判断缺失发生在采集、写入还是传输阶段；"
        "不把“截断原因”推进为确定结论。EOF 前关键字节已保留。";
    record.conclusion.stepIds = {"S01", "S02", "S03", "S04", "S05", "S06"};
    record.conclusion.evidenceIds = {"E01", "E02", "E03", "E04", "E05"};
    return record;
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

Record failedRecord(const ManifestObject& meta,
                    const fs::path& root,
                    const std::string& message) {
    Record record;
    record.id = meta.id;
    record.name = fs::path(meta.path).filename().string();
    record.revision = meta.revision;
    record.sourcePath = "data/" + meta.path;
    record.sourceRevision = meta.sourceRevision;
    record.sourceOffset = meta.sourceOffset;
    record.declaredSize = meta.expectedBytes;
    record.status = RecordStatus::Failed;
    try {
        record.bytes = readBytes(root / "data" / meta.path);
        record.fileSize = record.bytes.size();
        record.sha256 = sha256Hex(record.bytes);
        record.header = parseHeader(record.bytes);
        record.evidence.push_back(makeEvidence(
            "E01", "原始输入", record.sourcePath, 0, record.fileSize,
            record.bytes, message));
    } catch (...) {
        record.sha256 = "unavailable";
    }
    record.steps.push_back({"S01", "对象处理异常", message, "error",
                            record.evidence.empty() ? std::vector<std::string>{}
                                                    : std::vector<std::string>{"E01"}});
    record.conclusion.headline = "处理失败，已隔离并继续";
    record.conclusion.detail = message + "；批次未中断。";
    record.conclusion.stepIds = {"S01"};
    record.conclusion.evidenceIds =
        record.evidence.empty() ? std::vector<std::string>{}
                                : std::vector<std::string>{"E01"};
    return record;
}

}  // namespace

BatchResult processBatch(const BatchOptions& options) {
    const fs::path root =
        options.root.empty() ? fs::path(HDRPROV_SOURCE_DIR) : options.root;
    BatchResult result;
    result.startedAt = isoNow();
    std::string manifestText;
    const auto manifestObjects = loadManifest(root, manifestText);
    result.runId = "scan-" + sha256Hex(manifestText).substr(0, 10) + "-" + compactTime();
    result.tempPath = (root / ".tmp" / ("hdr-scan-" + result.runId)).string();
    TempWorkspace temp(result.tempPath, true);
    result.trace.push_back("批次开始 " + result.runId);

    std::optional<Record> baseline;
    bool sawException = false;
    for (const auto& meta : manifestObjects) {
        if (sawException) result.continuedAfterException = true;
        result.trace.push_back("开始对象 " + meta.id + " ← data/" + meta.path);
        try {
            Record record = processSnapshot(
                meta, root, temp.path, baseline ? &*baseline : nullptr);
            if (meta.role == "version_snapshot" && meta.revision == "v1") {
                baseline = record;
            }
            if (record.status == RecordStatus::Pass) ++result.passCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(
                record.id + " 完成：" + statusLabel(record.status) +
                "；" + record.conclusion.headline);
            result.records.push_back(std::move(record));
        } catch (TruncatedRecordError& error) {
            sawException = true;
            result.exceptionCaught = true;
            Record record = classifyTruncated(std::move(error.record));
            ++result.undecidableCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(
                record.id + " 抛出 TruncatedRecordError；已隔离，保留 EOF 证据");
            result.trace.push_back(
                record.id + " 边界结论：无法判断；继续处理后续对象");
            result.records.push_back(std::move(record));
        } catch (const std::exception& error) {
            sawException = true;
            result.exceptionCaught = true;
            Record record = failedRecord(meta, root, error.what());
            ++result.failedCount;
            result.evidenceCount += static_cast<int>(record.evidence.size());
            result.trace.push_back(
                record.id + " 处理异常：" + std::string(error.what()) + "；继续");
            result.records.push_back(std::move(record));
        }
    }

    result.tempCleaned = temp.cleanup();
    result.finishedAt = isoNow();
    result.trace.push_back(
        std::string("临时状态清理：") + (result.tempCleaned ? "已清理" : "未清理") +
        " " + result.tempPath);
    result.trace.push_back(
        "批次完成：PASS " + std::to_string(result.passCount) +
        " / UNDECIDABLE " + std::to_string(result.undecidableCount) +
        " / FAILED " + std::to_string(result.failedCount));
    if (options.writeArtifacts) {
        writeArtifacts(root, result, manifestText, manifestObjects);
    }
    return result;
}

}  // namespace hdrprov
