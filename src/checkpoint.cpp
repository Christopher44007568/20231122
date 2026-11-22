#include "checkpoint.hpp"

#include <cctype>
#include <fstream>
#include <sstream>

#include "sha256.hpp"

namespace inspect {
namespace {

std::string findJsonValue(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    auto position = json.find(needle);
    if (position == std::string::npos) {
        return {};
    }
    position = json.find(':', position + needle.size());
    if (position == std::string::npos) {
        return {};
    }
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position]))) {
        ++position;
    }
    if (position >= json.size()) {
        return {};
    }
    if (json[position] == '"') {
        ++position;
        std::string value;
        bool escaped = false;
        while (position < json.size()) {
            const char character = json[position++];
            if (escaped) {
                value.push_back(character);
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                break;
            } else {
                value.push_back(character);
            }
        }
        return value;
    }
    const auto end = json.find_first_of(",}\r\n", position);
    return trim(json.substr(position, end - position));
}

std::uint64_t toU64(const std::string& value, std::uint64_t fallback = 0) {
    try {
        return static_cast<std::uint64_t>(std::stoull(value));
    } catch (...) {
        return fallback;
    }
}

std::uint32_t toU32(const std::string& value, std::uint32_t fallback = 0) {
    return static_cast<std::uint32_t>(toU64(value, fallback));
}

bool toBool(const std::string& value, bool fallback = false) {
    if (value == "true") {
        return true;
    }
    if (value == "false") {
        return false;
    }
    return fallback;
}

std::string checkpointDirectory(Anchor anchor) {
    switch (anchor) {
        case Anchor::OriginalInput:
            return "00_original_input";
        case Anchor::GeometryOutput:
            return "01_geometry_output";
        case Anchor::CompositeOutput:
            return "02_composite_output";
    }
    return "unknown";
}

std::string checkpointJson(const RunState& state, const SnapshotInfo& info) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"run_id\": " << quoteJson(state.run_id) << ",\n";
    out << "  \"anchor\": " << static_cast<std::uint32_t>(info.anchor) << ",\n";
    out << "  \"anchor_key\": " << quoteJson(anchorKey(info.anchor)) << ",\n";
    out << "  \"anchor_name\": " << quoteJson(anchorName(info.anchor)) << ",\n";
    out << "  \"captured_at_utc\": " << quoteJson(info.stamp.utc) << ",\n";
    out << "  \"captured_epoch_ms\": " << info.stamp.epoch_ms << ",\n";
    out << "  \"captured_monotonic_ns\": " << info.stamp.monotonic_ns
        << ",\n";
    out << "  \"paused\": " << (info.paused ? "true" : "false") << ",\n";
    out << "  \"backend\": " << quoteJson(info.backend) << ",\n";
    out << "  \"material_path\": "
        << quoteJson(state.material_path.string()) << ",\n";
    out << "  \"material_sha256\": " << quoteJson(state.material_sha256)
        << ",\n";
    out << "  \"source_offset\": " << info.first_source_offset << ",\n";
    out << "  \"source_end_offset\": " << info.source_end_offset << ",\n";
    out << "  \"first_source_offset\": " << info.first_source_offset << ",\n";
    out << "  \"last_source_offset\": " << info.last_source_offset << ",\n";
    out << "  \"payload_path\": " << quoteJson(info.payload_path) << ",\n";
    out << "  \"payload_offset\": " << info.payload_offset << ",\n";
    out << "  \"payload_size\": " << info.payload_size << ",\n";
    out << "  \"payload_sha256\": " << quoteJson(info.sha256) << ",\n";
    out << "  \"element_count\": " << info.element_count << ",\n";
    out << "  \"width\": " << info.width << ",\n";
    out << "  \"height\": " << info.height << "\n";
    out << "}\n";
    return out.str();
}

}  // namespace

const char* anchorKey(Anchor anchor) {
    switch (anchor) {
        case Anchor::OriginalInput:
            return "a0";
        case Anchor::GeometryOutput:
            return "a1";
        case Anchor::CompositeOutput:
            return "a2";
    }
    return "unknown";
}

const char* anchorName(Anchor anchor) {
    switch (anchor) {
        case Anchor::OriginalInput:
            return "original-input";
        case Anchor::GeometryOutput:
            return "geometry-output";
        case Anchor::CompositeOutput:
            return "composite-output";
    }
    return "unknown";
}

std::string anchorDirectory(Anchor anchor) {
    return checkpointDirectory(anchor);
}

bool initializeRun(RunState& state, const Material& material,
                   const std::string& requested_run_dir,
                   const std::string& mode, std::string& error) {
    state.run_id = makeRunId();
    state.run_dir = requested_run_dir.empty()
                        ? std::filesystem::path("evidence") / state.run_id
                        : std::filesystem::path(requested_run_dir);
    state.material_path = material.path;
    state.material_sha256 = material.sha256;
    state.material_size = material.byte_size;
    state.next_anchor = 0;
    state.mode = mode;
    state.backend = "vulkan";
    state.paused = false;
    state.completed = false;

    std::error_code filesystem_error;
    std::filesystem::create_directories(state.run_dir / "original",
                                        filesystem_error);
    std::filesystem::create_directories(state.run_dir / "checkpoints",
                                        filesystem_error);
    if (filesystem_error) {
        error = "cannot create run directory: " + filesystem_error.message();
        return false;
    }

    const std::filesystem::path original_copy =
        state.run_dir / "original" / material.path.filename();
    if (!copyFileExact(material.path, original_copy)) {
        error = "cannot preserve original material";
        return false;
    }
    const std::string copy_hash = sha256FileHex(original_copy.string());
    if (copy_hash != material.sha256) {
        error = "original material copy hash mismatch";
        return false;
    }
    if (!writeTextFile(state.run_dir / "original" / "sha256.txt",
                       material.sha256 + "  " + material.path.filename().string() +
                           "\n")) {
        error = "cannot write original hash evidence";
        return false;
    }
    return saveRunState(state, error);
}

bool saveRunState(const RunState& state, std::string& error) {
    const ClockStamp now = nowStamp();
    std::ostringstream out;
    out << "{\n";
    out << "  \"run_id\": " << quoteJson(state.run_id) << ",\n";
    out << "  \"run_dir\": " << quoteJson(state.run_dir.string()) << ",\n";
    out << "  \"material_path\": "
        << quoteJson(state.material_path.string()) << ",\n";
    out << "  \"material_sha256\": " << quoteJson(state.material_sha256)
        << ",\n";
    out << "  \"material_size\": " << state.material_size << ",\n";
    out << "  \"next_anchor\": " << state.next_anchor << ",\n";
    out << "  \"mode\": " << quoteJson(state.mode) << ",\n";
    out << "  \"backend\": " << quoteJson(state.backend) << ",\n";
    out << "  \"width\": " << state.width << ",\n";
    out << "  \"height\": " << state.height << ",\n";
    out << "  \"paused\": " << (state.paused ? "true" : "false") << ",\n";
    out << "  \"completed\": " << (state.completed ? "true" : "false")
        << ",\n";
    out << "  \"updated_at_utc\": " << quoteJson(now.utc) << ",\n";
    out << "  \"updated_monotonic_ns\": " << now.monotonic_ns << "\n";
    out << "}\n";
    if (!writeTextFile(state.run_dir / "state.json", out.str())) {
        error = "cannot write state.json";
        return false;
    }
    return true;
}

bool loadRunState(const std::filesystem::path& run_dir, RunState& state,
                  std::string& error) {
    std::string json;
    if (!readTextFile(run_dir / "state.json", json)) {
        error = "cannot read state.json from " + run_dir.string();
        return false;
    }
    state.run_id = findJsonValue(json, "run_id");
    state.run_dir = run_dir;
    state.material_path = findJsonValue(json, "material_path");
    state.material_sha256 = findJsonValue(json, "material_sha256");
    state.material_size = toU64(findJsonValue(json, "material_size"));
    state.next_anchor =
        toU32(findJsonValue(json, "next_anchor"), kAnchorCount);
    state.mode = findJsonValue(json, "mode");
    state.backend = findJsonValue(json, "backend");
    state.width = toU32(findJsonValue(json, "width"), 512);
    state.height = toU32(findJsonValue(json, "height"), 512);
    state.paused = toBool(findJsonValue(json, "paused"));
    state.completed = toBool(findJsonValue(json, "completed"));
    if (state.run_id.empty() || state.material_path.empty()) {
        error = "state.json is incomplete";
        return false;
    }
    return true;
}

std::string checkpointMetaPath(const std::filesystem::path& run_dir,
                               Anchor anchor) {
    return (run_dir / "checkpoints" / anchorDirectory(anchor) /
            "checkpoint.json")
        .string();
}

bool appendTimeline(const RunState& state, const std::string& event,
                    const std::string& extra_fields, std::string& error) {
    const ClockStamp stamp = nowStamp();
    std::ostringstream line;
    line << "{\"event\":" << quoteJson(event);
    line << ",\"run_id\":" << quoteJson(state.run_id);
    line << ",\"timestamp_utc\":" << quoteJson(stamp.utc);
    line << ",\"epoch_ms\":" << stamp.epoch_ms;
    line << ",\"monotonic_ns\":" << stamp.monotonic_ns;
    line << ",\"mode\":" << quoteJson(state.mode);
    line << ",\"backend\":" << quoteJson(state.backend);
    if (!extra_fields.empty()) {
        line << ',' << extra_fields;
    }
    line << "}\n";
    if (!appendTextFile(state.run_dir / "timeline.jsonl", line.str())) {
        error = "cannot append timeline.jsonl";
        return false;
    }
    return true;
}

bool saveCheckpoint(const RunState& state, const SnapshotInfo& info,
                    const std::vector<std::uint8_t>& payload,
                    std::string& error) {
    SnapshotInfo saved = info;
    saved.stamp = nowStamp();
    saved.payload_size = payload.size();
    saved.sha256 = sha256Hex(payload);
    if (saved.source_end_offset == 0) {
        saved.source_end_offset = saved.first_source_offset;
    }

    const std::filesystem::path relative_directory =
        std::filesystem::path("checkpoints") / anchorDirectory(saved.anchor);
    const std::filesystem::path checkpoint_dir =
        state.run_dir / relative_directory;
    std::error_code filesystem_error;
    std::filesystem::create_directories(checkpoint_dir, filesystem_error);
    if (filesystem_error) {
        error = "cannot create checkpoint directory: " +
                filesystem_error.message();
        return false;
    }

    const std::filesystem::path temporary_payload =
        checkpoint_dir / "snapshot.bin.tmp";
    const std::filesystem::path payload_path = checkpoint_dir / "snapshot.bin";
    if (!writeBinaryFile(temporary_payload, payload)) {
        error = "cannot write checkpoint payload";
        return false;
    }
    std::filesystem::rename(temporary_payload, payload_path, filesystem_error);
    if (filesystem_error) {
        error = "cannot publish checkpoint payload: " +
                filesystem_error.message();
        return false;
    }

    saved.payload_path = (relative_directory / "snapshot.bin").generic_string();
    saved.payload_offset = 0;
    if (!writeTextFile(checkpoint_dir / "checkpoint.json",
                       checkpointJson(state, saved))) {
        error = "cannot write checkpoint.json";
        return false;
    }

    std::ostringstream fields;
    fields << "\"anchor\":" << static_cast<std::uint32_t>(saved.anchor);
    fields << ",\"anchor_key\":" << quoteJson(anchorKey(saved.anchor));
    fields << ",\"anchor_name\":" << quoteJson(anchorName(saved.anchor));
    fields << ",\"source_path\":" << quoteJson(state.material_path.string());
    fields << ",\"source_offset\":" << saved.first_source_offset;
    fields << ",\"last_source_offset\":" << saved.last_source_offset;
    fields << ",\"source_end_offset\":" << saved.source_end_offset;
    fields << ",\"payload_path\":" << quoteJson(saved.payload_path);
    fields << ",\"payload_offset\":" << saved.payload_offset;
    fields << ",\"payload_size\":" << saved.payload_size;
    fields << ",\"sha256\":" << quoteJson(saved.sha256);
    fields << ",\"paused\":true";
    return appendTimeline(state, "checkpoint_saved", fields.str(), error);
}

bool readCheckpointPayload(const std::filesystem::path& run_dir, Anchor anchor,
                           std::vector<std::uint8_t>& payload,
                           SnapshotInfo& info, std::string& error) {
    std::string json;
    const std::filesystem::path meta_path =
        run_dir / "checkpoints" / anchorDirectory(anchor) / "checkpoint.json";
    if (!readTextFile(meta_path, json)) {
        error = "cannot read checkpoint metadata: " + meta_path.string();
        return false;
    }
    info.anchor = anchor;
    info.stamp.utc = findJsonValue(json, "captured_at_utc");
    info.stamp.epoch_ms = toU64(findJsonValue(json, "captured_epoch_ms"));
    info.stamp.monotonic_ns =
        toU64(findJsonValue(json, "captured_monotonic_ns"));
    info.payload_path = findJsonValue(json, "payload_path");
    info.sha256 = findJsonValue(json, "payload_sha256");
    info.payload_size = toU64(findJsonValue(json, "payload_size"));
    info.payload_offset = toU64(findJsonValue(json, "payload_offset"));
    info.first_source_offset =
        toU64(findJsonValue(json, "first_source_offset"));
    info.last_source_offset =
        toU64(findJsonValue(json, "last_source_offset"));
    info.source_end_offset = toU64(findJsonValue(json, "source_end_offset"));
    info.element_count = toU64(findJsonValue(json, "element_count"));
    info.width = toU32(findJsonValue(json, "width"));
    info.height = toU32(findJsonValue(json, "height"));
    info.backend = findJsonValue(json, "backend");
    info.paused = toBool(findJsonValue(json, "paused"), true);
    if (info.payload_path.empty()) {
        error = "checkpoint payload path is missing";
        return false;
    }
    if (!readBinaryFile(run_dir / info.payload_path, payload)) {
        error = "cannot read checkpoint payload: " + info.payload_path;
        return false;
    }
    if (payload.size() != info.payload_size ||
        sha256Hex(payload) != info.sha256) {
        error = "checkpoint payload integrity check failed for " +
                std::string(anchorKey(anchor));
        return false;
    }
    return true;
}

}  // namespace inspect
