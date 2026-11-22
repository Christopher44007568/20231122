#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "capture.hpp"
#include "util.hpp"

namespace inspect {

enum class Anchor : std::uint32_t {
    OriginalInput = 0,
    GeometryOutput = 1,
    CompositeOutput = 2,
};

constexpr std::uint32_t kAnchorCount = 3;

const char* anchorKey(Anchor anchor);
const char* anchorName(Anchor anchor);
std::string anchorDirectory(Anchor anchor);

struct RunState {
    std::string run_id;
    std::filesystem::path run_dir;
    std::filesystem::path material_path;
    std::string material_sha256;
    std::uint64_t material_size = 0;
    std::uint32_t next_anchor = 0;
    std::string mode = "auto";
    std::string backend = "vulkan";
    std::uint32_t width = 512;
    std::uint32_t height = 512;
    bool paused = false;
    bool completed = false;
};

struct SnapshotInfo {
    Anchor anchor = Anchor::OriginalInput;
    ClockStamp stamp;
    std::string payload_path;
    std::string sha256;
    std::uint64_t payload_size = 0;
    std::uint64_t payload_offset = 0;
    std::uint64_t first_source_offset = 0;
    std::uint64_t last_source_offset = 0;
    std::uint64_t source_end_offset = 0;
    std::uint64_t element_count = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::string backend;
    bool paused = true;
};

bool initializeRun(RunState& state, const Material& material,
                   const std::string& requested_run_dir,
                   const std::string& mode, std::string& error);
bool loadRunState(const std::filesystem::path& run_dir, RunState& state,
                  std::string& error);
bool saveRunState(const RunState& state, std::string& error);

bool saveCheckpoint(const RunState& state, const SnapshotInfo& info,
                    const std::vector<std::uint8_t>& payload,
                    std::string& error);
bool appendTimeline(const RunState& state, const std::string& event,
                    const std::string& extra_fields, std::string& error);

std::string checkpointMetaPath(const std::filesystem::path& run_dir,
                               Anchor anchor);
bool readCheckpointPayload(const std::filesystem::path& run_dir, Anchor anchor,
                           std::vector<std::uint8_t>& payload,
                           SnapshotInfo& info, std::string& error);

}  // namespace inspect
