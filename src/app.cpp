#define GLFW_INCLUDE_NONE
#include "app.hpp"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "capture.hpp"
#include "checkpoint.hpp"
#include "sha256.hpp"
#include "util.hpp"
#include "vulkan_engine.hpp"

namespace inspect {
namespace {

constexpr int kExitPaused = 20;

class GlfwScope {
public:
    GlfwScope() = default;
    ~GlfwScope() {
        if (window_ != nullptr) {
            glfwDestroyWindow(window_);
        }
        if (initialized_) {
            glfwTerminate();
        }
    }

    bool initialize(bool create_window, std::string& error) {
        if (!create_window) {
            glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_NULL);
        }
        if (glfwInit() != GLFW_TRUE) {
            const char* description = nullptr;
            glfwGetError(&description);
            error = std::string("GLFW initialization failed") +
                    (description != nullptr ? ": " + std::string(description)
                                            : "");
            return false;
        }
        initialized_ = true;
        if (create_window) {
            glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
            glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
            window_ = glfwCreateWindow(800, 560,
                                       "Geometry Shader Output Inspector",
                                       nullptr, nullptr);
            if (window_ == nullptr) {
                error = "cannot create GLFW window";
                return false;
            }
        }
        return true;
    }

    GLFWwindow* window() const { return window_; }

private:
    bool initialized_ = false;
    GLFWwindow* window_ = nullptr;
};

struct Backend {
    VulkanEngine vulkan;
    ResourcePlan plan;
    bool ready = false;
    bool vulkan_active = false;
    std::filesystem::path plan_path;

    bool initialize(const AppOptions& options, const Material& material,
                    const std::string& requested_mode,
                    const std::filesystem::path& plan_output,
                    std::string& error) {
        EngineOptions engine_options;
        engine_options.requested_mode = requested_mode;
        engine_options.shader_dir = GS_SHADER_DIR;
        engine_options.requested_width = options.width;
        engine_options.requested_height = options.height;
        engine_options.memory_budget_bytes = options.memory_budget_bytes;
        engine_options.force_low_memory = options.force_low_memory;
        engine_options.allow_cpu_fallback = true;
        engine_options.verbose = options.verbose;
        plan_path = plan_output;

        std::string diagnostic;
        const bool initialized =
            vulkan.initialize(engine_options, material, plan, diagnostic);
        if (!initialized) {
            plan = makeCpuFallbackPlan(engine_options, material, diagnostic);
        }
        ready = true;
        vulkan_active = plan.backend == "vulkan";
        if (!writePlan(error)) {
            return false;
        }
        if (!diagnostic.empty() && options.verbose) {
            std::cerr << "resource decision: " << diagnostic << '\n';
        }
        return true;
    }

    bool writePlan(std::string& error) {
        if (!writeTextFile(plan_path, plan.toJson())) {
            error = "cannot write resource plan: " + plan_path.string();
            return false;
        }
        return true;
    }

    void degrade(const std::string& reason) {
        plan.reasons.push_back(reason);
        plan.backend = "cpu";
        plan.selected_mode = "cpu";
        plan.degraded = true;
        vulkan.shutdown();
        vulkan_active = false;
    }

    bool geometry(const Material& material, std::vector<std::uint8_t>& output,
                  std::string& error) {
        if (vulkan_active) {
            if (vulkan.geometryPass(material, output, error)) {
                return true;
            }
            degrade("runtime geometry pass failed: " + error);
            std::string plan_error;
            writePlan(plan_error);
        }
        output = packRecords(emulateGeometryOutput(material));
        return true;
    }

    bool composite(const std::vector<std::uint8_t>& capture,
                   std::vector<std::uint8_t>& rgba, std::string& error) {
        if (vulkan_active) {
            if (vulkan.compositePass(capture, rgba, error)) {
                return true;
            }
            degrade("runtime composite pass failed: " + error);
            std::string plan_error;
            writePlan(plan_error);
        }
        std::vector<GsRecord> records;
        if (!unpackRecords(capture, records)) {
            error = "cannot unpack geometry capture for CPU fallback";
            return false;
        }
        rgba = rasterizeRecords(records, plan.width, plan.height);
        return true;
    }
};

struct Offsets {
    std::uint64_t first = 0;
    std::uint64_t last = 0;
    std::uint64_t end = 0;
};

Offsets recordOffsets(const std::vector<GsRecord>& records,
                      std::uint64_t material_size) {
    Offsets offsets;
    if (records.empty()) {
        offsets.end = material_size;
        return offsets;
    }
    offsets.first = records.front().source_offset;
    offsets.last = records.front().source_offset;
    for (const GsRecord& record : records) {
        offsets.first = std::min<std::uint64_t>(offsets.first,
                                                record.source_offset);
        offsets.last = std::max<std::uint64_t>(offsets.last,
                                               record.source_offset);
    }
    offsets.end = material_size;
    return offsets;
}

bool verifyOriginal(const RunState& state, Material& material,
                    std::string& error) {
    if (!loadMaterial(state.material_path, material, error)) {
        return false;
    }
    if (material.sha256 != state.material_sha256 ||
        material.byte_size != state.material_size) {
        error = "original material changed; expected " +
                state.material_sha256 + " but found " + material.sha256;
        return false;
    }
    return true;
}

bool directoryHasState(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::exists(path / "state.json", error);
}

std::string anchorFields(Anchor anchor) {
    return "\"anchor\":" + std::to_string(static_cast<std::uint32_t>(anchor)) +
           ",\"anchor_key\":" + quoteJson(anchorKey(anchor)) +
           ",\"anchor_name\":" + quoteJson(anchorName(anchor));
}

std::uint64_t firstDifference(const std::vector<std::uint8_t>& before,
                              const std::vector<std::uint8_t>& after) {
    const std::size_t limit = std::min(before.size(), after.size());
    for (std::size_t index = 0; index < limit; ++index) {
        if (before[index] != after[index]) {
            return index;
        }
    }
    return before.size() == after.size() ? UINT64_MAX
                                         : static_cast<std::uint64_t>(limit);
}

bool initializeBackend(const AppOptions& options, RunState& state,
                       const Material& material, Backend& backend,
                       const std::filesystem::path& plan_output,
                       std::string& error) {
    AppOptions effective = options;
    effective.width = state.width;
    effective.height = state.height;
    if (!backend.initialize(effective, material, state.mode, plan_output,
                            error)) {
        return false;
    }
    state.backend = backend.plan.backend;
    state.width = backend.plan.width;
    state.height = backend.plan.height;
    state.mode = backend.plan.selected_mode;
    if (!saveRunState(state, error)) {
        return false;
    }
    std::ostringstream fields;
    fields << "\"plan_path\":" << quoteJson(plan_output.string());
    fields << ",\"plan_backend\":" << quoteJson(backend.plan.backend);
    fields << ",\"selected_mode\":" << quoteJson(backend.plan.selected_mode);
    fields << ",\"degraded\":"
           << (backend.plan.degraded ? "true" : "false");
    fields << ",\"heap_bytes\":" << backend.plan.heap_bytes;
    fields << ",\"budget_bytes\":" << backend.plan.memory_budget_bytes;
    fields << ",\"required_bytes\":" << backend.plan.required_bytes;
    return appendTimeline(state, "resource_plan_written", fields.str(),
                          error);
}

bool runAnchor0(const AppOptions&, RunState& state, const Material& material,
                std::string& error) {
    SnapshotInfo info;
    info.anchor = Anchor::OriginalInput;
    info.first_source_offset = 0;
    info.last_source_offset =
        material.byte_size == 0 ? 0 : material.byte_size - 1;
    info.source_end_offset = material.byte_size;
    info.element_count = material.vertices.size();
    info.width = state.width;
    info.height = state.height;
    info.backend = state.backend;
    if (!saveCheckpoint(state, info, material.bytes, error)) {
        return false;
    }
    state.next_anchor = 1;
    state.paused = true;
    if (!saveRunState(state, error)) {
        return false;
    }
    std::ostringstream fields;
    fields << anchorFields(Anchor::OriginalInput);
    fields << ",\"source_offset\":0";
    fields << ",\"source_end_offset\":" << material.byte_size;
    fields << ",\"next_anchor\":1";
    return appendTimeline(state, "run_paused", fields.str(), error);
}

bool runAnchor1(const AppOptions& options, RunState& state,
                const Material& material, Backend& backend,
                const std::filesystem::path& plan_output,
                std::string& error) {
    if (!initializeBackend(options, state, material, backend, plan_output,
                           error)) {
        return false;
    }
    std::vector<std::uint8_t> capture;
    if (!backend.geometry(material, capture, error)) {
        return false;
    }
    std::vector<GsRecord> records;
    if (!unpackRecords(capture, records)) {
        error = "geometry output is not aligned to 64-byte records";
        return false;
    }
    const Offsets offsets = recordOffsets(records, material.byte_size);
    state.backend = backend.plan.backend;
    state.width = backend.plan.width;
    state.height = backend.plan.height;
    SnapshotInfo info;
    info.anchor = Anchor::GeometryOutput;
    info.first_source_offset = offsets.first;
    info.last_source_offset = offsets.last;
    info.source_end_offset = offsets.end;
    info.element_count = records.size();
    info.width = state.width;
    info.height = state.height;
    info.backend = state.backend;
    if (!saveCheckpoint(state, info, capture, error)) {
        return false;
    }
    state.next_anchor = 2;
    state.paused = true;
    if (!saveRunState(state, error)) {
        return false;
    }
    std::ostringstream fields;
    fields << anchorFields(Anchor::GeometryOutput);
    fields << ",\"source_offset\":" << offsets.first;
    fields << ",\"last_source_offset\":" << offsets.last;
    fields << ",\"records\":" << records.size();
    fields << ",\"next_anchor\":2";
    return appendTimeline(state, "run_paused", fields.str(), error);
}

bool runAnchor2(const AppOptions& options, RunState& state,
                const Material& material, Backend& backend,
                const std::filesystem::path& plan_output,
                std::string& error) {
    if (!initializeBackend(options, state, material, backend, plan_output,
                           error)) {
        return false;
    }
    std::vector<std::uint8_t> capture;
    SnapshotInfo geometry_info;
    if (!readCheckpointPayload(state.run_dir, Anchor::GeometryOutput, capture,
                               geometry_info, error)) {
        return false;
    }
    std::vector<GsRecord> records;
    if (!unpackRecords(capture, records)) {
        error = "saved geometry output is not record-aligned";
        return false;
    }
    std::vector<std::uint8_t> rgba;
    if (!backend.composite(capture, rgba, error)) {
        return false;
    }
    const std::size_t expected =
        static_cast<std::size_t>(backend.plan.width) *
        backend.plan.height * 4ULL;
    if (rgba.size() != expected) {
        error = "composite output size does not match planned extent";
        return false;
    }
    const Offsets offsets = recordOffsets(records, material.byte_size);
    state.backend = backend.plan.backend;
    state.width = backend.plan.width;
    state.height = backend.plan.height;
    SnapshotInfo info;
    info.anchor = Anchor::CompositeOutput;
    info.first_source_offset = offsets.first;
    info.last_source_offset = offsets.last;
    info.source_end_offset = offsets.end;
    info.element_count =
        static_cast<std::uint64_t>(backend.plan.width) * backend.plan.height;
    info.width = backend.plan.width;
    info.height = backend.plan.height;
    info.backend = state.backend;
    if (!saveCheckpoint(state, info, rgba, error)) {
        return false;
    }
    const std::filesystem::path preview_path =
        state.run_dir / "checkpoints" / anchorDirectory(Anchor::CompositeOutput) /
        "preview.ppm";
    if (!writePpm(preview_path, rgba, backend.plan.width,
                  backend.plan.height)) {
        error = "cannot write composite preview.ppm";
        return false;
    }
    state.next_anchor = 3;
    state.paused = false;
    state.completed = true;
    if (!saveRunState(state, error)) {
        return false;
    }
    std::ostringstream fields;
    fields << anchorFields(Anchor::CompositeOutput);
    fields << ",\"source_offset\":" << offsets.first;
    fields << ",\"last_source_offset\":" << offsets.last;
    fields << ",\"pixels\":"
           << static_cast<std::uint64_t>(backend.plan.width) *
                  backend.plan.height;
    fields << ",\"preview_path\":" << quoteJson(preview_path.string());
    return appendTimeline(state, "run_completed", fields.str(), error);
}

std::string checkpointSummaryJson(const SnapshotInfo& info) {
    std::ostringstream out;
    out << "{";
    out << "\"anchor\":" << static_cast<std::uint32_t>(info.anchor);
    out << ",\"anchor_key\":" << quoteJson(anchorKey(info.anchor));
    out << ",\"anchor_name\":" << quoteJson(anchorName(info.anchor));
    out << ",\"captured_at_utc\":" << quoteJson(info.stamp.utc);
    out << ",\"captured_epoch_ms\":" << info.stamp.epoch_ms;
    out << ",\"captured_monotonic_ns\":" << info.stamp.monotonic_ns;
    out << ",\"source_offset\":" << info.first_source_offset;
    out << ",\"last_source_offset\":" << info.last_source_offset;
    out << ",\"source_end_offset\":" << info.source_end_offset;
    out << ",\"payload_path\":" << quoteJson(info.payload_path);
    out << ",\"payload_offset\":" << info.payload_offset;
    out << ",\"payload_bytes\":" << info.payload_size;
    out << ",\"payload_sha256\":" << quoteJson(info.sha256);
    out << ",\"element_count\":" << info.element_count;
    out << ",\"width\":" << info.width;
    out << ",\"height\":" << info.height;
    out << ",\"backend\":" << quoteJson(info.backend);
    out << "}";
    return out.str();
}

bool compareRun(const AppOptions& options, const RunState& state,
                bool& identical, bool& original_unchanged,
                std::string& error) {
    identical = false;
    original_unchanged = false;
    Material material;
    if (!verifyOriginal(state, material, error)) {
        return false;
    }

    std::vector<std::uint8_t> original_payload;
    std::vector<std::uint8_t> before_geometry;
    std::vector<std::uint8_t> before_composite;
    SnapshotInfo original_info;
    SnapshotInfo before_geometry_info;
    SnapshotInfo before_composite_info;
    if (!readCheckpointPayload(state.run_dir, Anchor::OriginalInput,
                               original_payload, original_info, error) ||
        !readCheckpointPayload(state.run_dir, Anchor::GeometryOutput,
                               before_geometry, before_geometry_info, error) ||
        !readCheckpointPayload(state.run_dir, Anchor::CompositeOutput,
                               before_composite, before_composite_info,
                               error)) {
        return false;
    }
    if (original_payload != material.bytes) {
        error = "A0 payload does not match the unmodified original material";
        return false;
    }

    AppOptions effective = options;
    effective.width = state.width;
    effective.height = state.height;
    Backend backend;
    const std::filesystem::path compare_plan_path =
        state.run_dir / "resource_plan_comparison.json";
    if (!backend.initialize(effective, material, state.mode, compare_plan_path,
                            error)) {
        return false;
    }
    std::vector<std::uint8_t> after_geometry;
    if (!backend.geometry(material, after_geometry, error)) {
        return false;
    }
    const ClockStamp after_geometry_stamp = nowStamp();
    std::vector<GsRecord> after_records;
    if (!unpackRecords(after_geometry, after_records)) {
        error = "replayed geometry output is not record-aligned";
        return false;
    }
    std::vector<std::uint8_t> after_composite;
    if (!backend.composite(after_geometry, after_composite, error)) {
        return false;
    }
    const ClockStamp after_composite_stamp = nowStamp();

    const std::uint64_t geometry_difference =
        firstDifference(before_geometry, after_geometry);
    const std::uint64_t composite_difference =
        firstDifference(before_composite, after_composite);
    const bool geometry_identical =
        before_geometry.size() == after_geometry.size() &&
        geometry_difference == UINT64_MAX;
    const bool composite_identical =
        before_composite.size() == after_composite.size() &&
        composite_difference == UINT64_MAX;
    identical = geometry_identical && composite_identical;
    const std::string current_hash =
        sha256FileHex(state.material_path.string());
    original_unchanged = current_hash == state.material_sha256;
    const ClockStamp generated = nowStamp();
    const Offsets after_offsets = recordOffsets(after_records,
                                                 material.byte_size);

    std::ostringstream json;
    json << "{\n";
    json << "  \"run_id\": " << quoteJson(state.run_id) << ",\n";
    json << "  \"generated_at_utc\": " << quoteJson(generated.utc) << ",\n";
    json << "  \"original_sha256_at_capture\": "
         << quoteJson(state.material_sha256) << ",\n";
    json << "  \"original_sha256_after_resume\": "
         << quoteJson(current_hash) << ",\n";
    json << "  \"original_unchanged\": "
         << (original_unchanged ? "true" : "false") << ",\n";
    json << "  \"backend_before\": " << quoteJson(state.backend) << ",\n";
    json << "  \"backend_after\": " << quoteJson(backend.plan.backend)
         << ",\n";
    json << "  \"comparison_resource_plan\": "
         << quoteJson(compare_plan_path.string()) << ",\n";
    json << "  \"before\": {\n";
    json << "    \"a1\": " << checkpointSummaryJson(before_geometry_info)
         << ",\n";
    json << "    \"a2\": " << checkpointSummaryJson(before_composite_info)
         << "\n";
    json << "  },\n";
    json << "  \"after\": {\n";
    json << "    \"a1\": {\"captured_at_utc\": "
         << quoteJson(after_geometry_stamp.utc)
         << ", \"captured_epoch_ms\": " << after_geometry_stamp.epoch_ms
         << ", \"captured_monotonic_ns\": "
         << after_geometry_stamp.monotonic_ns
         << ", \"source_offset\": " << after_offsets.first
         << ", \"last_source_offset\": " << after_offsets.last
         << ", \"payload_bytes\": " << after_geometry.size()
         << ", \"payload_sha256\": " << quoteJson(sha256Hex(after_geometry))
         << "},\n";
    json << "    \"a2\": {\"captured_at_utc\": "
         << quoteJson(after_composite_stamp.utc)
         << ", \"captured_epoch_ms\": " << after_composite_stamp.epoch_ms
         << ", \"captured_monotonic_ns\": "
         << after_composite_stamp.monotonic_ns
         << ", \"source_offset\": " << after_offsets.first
         << ", \"last_source_offset\": " << after_offsets.last
         << ", \"payload_bytes\": " << after_composite.size()
         << ", \"payload_sha256\": " << quoteJson(sha256Hex(after_composite))
         << "}\n";
    json << "  },\n";
    json << "  \"a1\": {\"before_sha256\": "
         << quoteJson(before_geometry_info.sha256)
         << ", \"after_sha256\": " << quoteJson(sha256Hex(after_geometry))
         << ", \"identical\": " << (geometry_identical ? "true" : "false")
         << ", \"first_difference_offset\": ";
    if (geometry_identical) {
        json << "null";
    } else {
        json << geometry_difference;
    }
    json << ", \"source_offset\": " << before_geometry_info.first_source_offset
         << "},\n";
    json << "  \"a2\": {\"before_sha256\": "
         << quoteJson(before_composite_info.sha256)
         << ", \"after_sha256\": " << quoteJson(sha256Hex(after_composite))
         << ", \"identical\": " << (composite_identical ? "true" : "false")
         << ", \"first_difference_offset\": ";
    if (composite_identical) {
        json << "null";
    } else {
        json << composite_difference;
    }
    json << ", \"source_offset\": " << before_composite_info.first_source_offset
         << "},\n";
    json << "  \"identical\": " << (identical ? "true" : "false") << "\n";
    json << "}\n";

    if (!writeTextFile(state.run_dir / "comparison.json", json.str())) {
        error = "cannot write comparison.json";
        return false;
    }
    std::ostringstream fields;
    fields << "\"identical\":" << (identical ? "true" : "false");
    fields << ",\"original_unchanged\":"
           << (original_unchanged ? "true" : "false");
    fields << ",\"a1_sha256\":" << quoteJson(sha256Hex(after_geometry));
    fields << ",\"a2_sha256\":" << quoteJson(sha256Hex(after_composite));
    if (!appendTimeline(state, "comparison_written", fields.str(), error)) {
        return false;
    }
    backend.vulkan.shutdown();
    return true;
}

bool writeAcceptance(const RunState& state, bool comparison_identical,
                     bool& accepted, std::string& error) {
    accepted = false;
    Material material;
    if (!verifyOriginal(state, material, error)) {
        return false;
    }
    const std::string current_hash =
        sha256FileHex(state.material_path.string());
    const std::filesystem::path preserved =
        state.run_dir / "original" / state.material_path.filename();
    std::uint64_t preserved_size = 0;
    const std::string preserved_hash =
        sha256FileHex(preserved.string(), &preserved_size);
    const bool original_unchanged =
        current_hash == state.material_sha256 &&
        preserved_hash == state.material_sha256 &&
        preserved_size == state.material_size;

    std::vector<SnapshotInfo> infos(3);
    std::vector<std::uint8_t> payload;
    if (!readCheckpointPayload(state.run_dir, Anchor::OriginalInput, payload,
                               infos[0], error) ||
        !readCheckpointPayload(state.run_dir, Anchor::GeometryOutput, payload,
                               infos[1], error) ||
        !readCheckpointPayload(state.run_dir, Anchor::CompositeOutput, payload,
                               infos[2], error)) {
        return false;
    }

    const ClockStamp accepted_at = nowStamp();
    std::ostringstream json;
    json << "{\n";
    json << "  \"run_id\": " << quoteJson(state.run_id) << ",\n";
    json << "  \"accepted_at_utc\": " << quoteJson(accepted_at.utc) << ",\n";
    json << "  \"technical_stack\": [\"C++17\", \"Vulkan\", \"GLFW\"],\n";
    json << "  \"original\": {\n";
    json << "    \"path\": " << quoteJson(state.material_path.string())
         << ",\n";
    json << "    \"preserved_path\": " << quoteJson(preserved.string())
         << ",\n";
    json << "    \"original_offset\": 0,\n";
    json << "    \"size\": " << state.material_size << ",\n";
    json << "    \"sha256_at_capture\": "
         << quoteJson(state.material_sha256) << ",\n";
    json << "    \"sha256_now\": " << quoteJson(current_hash) << ",\n";
    json << "    \"preserved_sha256\": " << quoteJson(preserved_hash)
         << ",\n";
    json << "    \"unchanged\": "
         << (original_unchanged ? "true" : "false") << "\n";
    json << "  },\n";
    json << "  \"checkpoints\": [\n";
    for (std::size_t index = 0; index < infos.size(); ++index) {
        json << "    " << checkpointSummaryJson(infos[index]);
        json << (index + 1U == infos.size() ? "\n" : ",\n");
    }
    json << "  ],\n";
    json << "  \"comparison_file\": "
         << quoteJson((state.run_dir / "comparison.json").string()) << ",\n";
    json << "  \"comparison_identical\": "
         << (comparison_identical ? "true" : "false") << ",\n";
    accepted = original_unchanged && comparison_identical;
    json << "  \"accepted\": " << (accepted ? "true" : "false") << "\n";
    json << "}\n";
    if (!writeTextFile(state.run_dir / "acceptance.json", json.str())) {
        error = "cannot write acceptance.json";
        return false;
    }
    std::ostringstream fields;
    fields << "\"accepted\":" << (accepted ? "true" : "false");
    fields << ",\"original_unchanged\":"
           << (original_unchanged ? "true" : "false");
    fields << ",\"comparison_identical\":"
           << (comparison_identical ? "true" : "false");
    return appendTimeline(state, "acceptance_written", fields.str(), error);
}

bool beginNewRun(const AppOptions& options, RunState& state,
                 Material& material, std::string& error) {
    if (directoryHasState(options.run_dir)) {
        error = "run directory already contains state.json: " +
                options.run_dir.string();
        return false;
    }
    if (!loadMaterial(options.material, material, error)) {
        return false;
    }
    if (!initializeRun(state, material, options.run_dir.string(),
                       options.requested_mode, error)) {
        return false;
    }
    state.width = options.width;
    state.height = options.height;
    state.mode = options.requested_mode;
    if (!saveRunState(state, error)) {
        return false;
    }
    std::ostringstream fields;
    fields << "\"material_path\":" << quoteJson(material.path.string());
    fields << ",\"material_offset\":0";
    fields << ",\"material_bytes\":" << material.byte_size;
    fields << ",\"material_sha256\":" << quoteJson(material.sha256);
    fields << ",\"requested_mode\":" << quoteJson(options.requested_mode);
    return appendTimeline(state, "run_created", fields.str(), error);
}

bool appendResume(RunState& state, std::string& error) {
    const std::uint32_t from_anchor = state.next_anchor;
    std::ostringstream fields;
    fields << "\"from_anchor\":" << from_anchor;
    return appendTimeline(state, "run_resumed", fields.str(), error);
}

bool completeAfterComposite(const AppOptions& options, RunState& state,
                            bool& accepted, std::string& error) {
    bool identical = false;
    bool original_unchanged = false;
    if (!compareRun(options, state, identical, original_unchanged, error)) {
        return false;
    }
    if (!writeAcceptance(state, identical, accepted, error)) {
        return false;
    }
    if (options.verbose) {
        std::cerr << "comparison identical=" << (identical ? "true" : "false")
                  << " original_unchanged="
                  << (original_unchanged ? "true" : "false") << '\n';
    }
    return true;
}

int commandRun(const AppOptions& options) {
    RunState state;
    Material material;
    std::string error;
    if (!beginNewRun(options, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Backend backend;
    if (!initializeBackend(options, state, material, backend,
                           state.run_dir / "resource_plan.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    backend.vulkan.shutdown();
    if (!runAnchor0(options, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::cout << "paused at A0; run directory: " << state.run_dir << '\n';
    std::cout << "resume with: gs_inspect --resume " << state.run_dir << '\n';
    return kExitPaused;
}

int commandResume(const AppOptions& options) {
    RunState state;
    std::string error;
    if (!loadRunState(options.run_dir, state, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Material material;
    if (!verifyOriginal(state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    if (!appendResume(state, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    if (state.next_anchor == 0) {
        if (!runAnchor0(options, state, material, error)) {
            std::cerr << error << '\n';
            return 2;
        }
        return kExitPaused;
    }
    if (state.next_anchor == 1) {
        Backend backend;
        if (!runAnchor1(options, state, material, backend,
                        state.run_dir / "resource_plan_resume_a1.json",
                        error)) {
            std::cerr << error << '\n';
            return 2;
        }
        std::cout << "paused at A1; run directory: " << state.run_dir << '\n';
        std::cout << "resume with: gs_inspect --resume " << state.run_dir
                  << '\n';
        return kExitPaused;
    }
    if (state.next_anchor == 2) {
        Backend backend;
        if (!runAnchor2(options, state, material, backend,
                        state.run_dir / "resource_plan_resume_a2.json",
                        error)) {
            std::cerr << error << '\n';
            return 2;
        }
    }

    bool accepted = false;
    if (!completeAfterComposite(options, state, accepted, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::cout << "acceptance: " << (accepted ? "passed" : "failed") << '\n';
    std::cout << "evidence: " << state.run_dir / "acceptance.json" << '\n';
    return accepted ? 0 : 1;
}

int commandAcceptance(const AppOptions& options) {
    AppOptions effective = options;
    if (directoryHasState(effective.run_dir)) {
        effective.run_dir =
            effective.run_dir.string() + "-" + makeRunId();
        std::cout << "previous evidence preserved; new run: "
                  << effective.run_dir << '\n';
    }
    RunState state;
    Material material;
    std::string error;
    if (!beginNewRun(effective, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Backend backend;
    if (!initializeBackend(effective, state, material, backend,
                           state.run_dir / "resource_plan.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    backend.vulkan.shutdown();

    if (!runAnchor0(effective, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    if (!loadRunState(state.run_dir, state, error) ||
        !appendResume(state, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    Backend geometry_backend;
    if (!runAnchor1(effective, state, material, geometry_backend,
                    state.run_dir / "resource_plan_resume_a1.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    geometry_backend.vulkan.shutdown();
    if (!loadRunState(state.run_dir, state, error) ||
        !appendResume(state, error)) {
        std::cerr << error << '\n';
        return 2;
    }

    Backend composite_backend;
    if (!runAnchor2(effective, state, material, composite_backend,
                    state.run_dir / "resource_plan_resume_a2.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    composite_backend.vulkan.shutdown();
    bool accepted = false;
    if (!completeAfterComposite(effective, state, accepted, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::cout << "acceptance: " << (accepted ? "passed" : "failed") << '\n';
    std::cout << "evidence: " << state.run_dir / "acceptance.json" << '\n';
    return accepted ? 0 : 1;
}

int commandCompare(const AppOptions& options) {
    RunState state;
    std::string error;
    if (!loadRunState(options.run_dir, state, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    bool accepted = false;
    if (!completeAfterComposite(options, state, accepted, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    std::cout << "acceptance: " << (accepted ? "passed" : "failed") << '\n';
    std::cout << "evidence: " << state.run_dir / "acceptance.json" << '\n';
    return accepted ? 0 : 1;
}

bool waitForSpace(GLFWwindow* window, const std::string& title) {
    glfwSetWindowTitle(window, title.c_str());
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwWaitEventsTimeout(0.05);
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS ||
            glfwGetKey(window, GLFW_KEY_ENTER) == GLFW_PRESS) {
            return true;
        }
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return false;
        }
    }
    return false;
}

int commandGui(const AppOptions& options, GLFWwindow* window) {
    if (window == nullptr) {
        std::cerr << "GUI mode requires a GLFW window\n";
        return 2;
    }
    RunState state;
    Material material;
    std::string error;
    if (!beginNewRun(options, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Backend initial_backend;
    if (!initializeBackend(options, state, material, initial_backend,
                           state.run_dir / "resource_plan.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    initial_backend.vulkan.shutdown();

    if (!runAnchor0(options, state, material, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    if (!waitForSpace(window, "A0 saved - press SPACE to resume")) {
        return 0;
    }
    if (!loadRunState(state.run_dir, state, error) ||
        !appendResume(state, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Backend geometry_backend;
    if (!runAnchor1(options, state, material, geometry_backend,
                    state.run_dir / "resource_plan_resume_a1.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    geometry_backend.vulkan.shutdown();
    if (!waitForSpace(window, "A1 saved - press SPACE to resume")) {
        return 0;
    }
    if (!loadRunState(state.run_dir, state, error) ||
        !appendResume(state, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    Backend composite_backend;
    if (!runAnchor2(options, state, material, composite_backend,
                    state.run_dir / "resource_plan_resume_a2.json", error)) {
        std::cerr << error << '\n';
        return 2;
    }
    composite_backend.vulkan.shutdown();
    bool accepted = false;
    if (!completeAfterComposite(options, state, accepted, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    glfwSetWindowTitle(window,
                       accepted ? "acceptance PASS - close window"
                                : "acceptance FAIL - close window");
    while (glfwWindowShouldClose(window) == GLFW_FALSE) {
        glfwWaitEventsTimeout(0.1);
    }
    return accepted ? 0 : 1;
}

bool initializeGlfwForCommand(const AppOptions& options, GlfwScope& glfw,
                              std::string& error) {
    const bool create_window = options.command == Command::Gui;
    if (create_window && options.headless) {
        error = "--headless cannot be combined with --gui";
        return false;
    }
    return glfw.initialize(create_window, error);
}

}  // namespace

std::string commandHelp() {
    return
        "Geometry Shader Output Inspector\n"
        "  C++17 / Vulkan / GLFW, resumable three-anchor inspection\n\n"
        "Usage:\n"
        "  gs_inspect --acceptance DIR [options]\n"
        "  gs_inspect --run DIR [options]\n"
        "  gs_inspect --resume DIR [options]\n"
        "  gs_inspect --compare DIR [options]\n"
        "  gs_inspect --gui DIR [options]\n\n"
        "Options:\n"
        "  --material PATH       Immutable .gsi material (default bundled)\n"
        "  --degrade MODE        auto, full, compact, or cpu\n"
        "  --budget-mib N        Admit at most N MiB of device memory\n"
        "  --force-low-memory    Force a compact/CPU resource decision\n"
        "  --width N             Requested offscreen width (default 512)\n"
        "  --height N            Requested offscreen height (default 512)\n"
        "  --headless            Use GLFW's null platform and no window\n"
        "  --verbose             Print resource and comparison decisions\n"
        "  -h, --help            Show this help\n\n"
        "Anchors:\n"
        "  A0 original-input      original bytes and immutable copy\n"
        "  A1 geometry-output     raw 64-byte geometry capture records\n"
        "  A2 composite-output    raw RGBA pixels and preview.ppm\n";
}

int runApplication(const AppOptions& options) {
    if (options.command == Command::Help) {
        std::cout << commandHelp();
        return 0;
    }
    if (options.run_dir.empty()) {
        std::cerr << "a run directory is required\n";
        return 2;
    }
    GlfwScope glfw;
    std::string error;
    if (!initializeGlfwForCommand(options, glfw, error)) {
        std::cerr << error << '\n';
        return 2;
    }
    try {
        switch (options.command) {
            case Command::Run:
                return commandRun(options);
            case Command::Resume:
                return commandResume(options);
            case Command::Compare:
                return commandCompare(options);
            case Command::Acceptance:
                return commandAcceptance(options);
            case Command::Gui:
                return commandGui(options, glfw.window());
            case Command::Help:
                break;
        }
    } catch (const std::exception& exception) {
        std::cerr << "fatal: " << exception.what() << '\n';
        return 2;
    }
    return 0;
}

}  // namespace inspect
