#include "pipeline.h"

#include "renderer.h"
#include "sha256.h"

#include <algorithm>
#include <sstream>

namespace ph {
namespace {

int profileIndex(const std::string& name) {
    const auto& profiles = qualityProfiles();
    for (size_t i = 0; i < profiles.size(); ++i) {
        if (profiles[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int64_t estimateBytes(const QualityProfile& profile, int frame_count, int probe_count) {
    const int retained = std::min(profile.retained_frames, frame_count);
    const int64_t pixels = static_cast<int64_t>(profile.width) * profile.height;
    const int64_t retained_history = 2 * retained * pixels * 4;
    const int64_t active_targets = 2 * pixels * 4;
    const int64_t evidence = static_cast<int64_t>(frame_count) * probe_count * 2 * 160;
    return retained_history + active_targets + evidence;
}

void applyProfile(ResourcePlan& plan, const QualityProfile& profile, const AppConfig& config) {
    plan.effective_quality = profile.name;
    plan.width = profile.width;
    plan.height = profile.height;
    plan.retained_frames = std::min(profile.retained_frames, config.clock.frame_count);
    plan.estimated_bytes =
        estimateBytes(profile, config.clock.frame_count, config.clock.probe_count);
}

std::string digestOfFields(const std::vector<std::string>& fields) {
    std::string canonical;
    for (const std::string& field : fields) {
        canonical += field;
        canonical.push_back('\n');
    }
    return sha256Hex(canonical);
}

}  // namespace

const std::vector<QualityProfile>& qualityProfiles() {
    static const std::vector<QualityProfile> profiles = {
        {"high", 320, 200, 8},
        {"balanced", 240, 150, 6},
        {"low", 160, 100, 4},
        {"minimal", 96, 60, 2},
        {"emergency", 64, 40, 1},
    };
    return profiles;
}

ResourcePlan chooseResourcePlan(const AppConfig& config) {
    ResourcePlan plan;
    plan.requested_quality = "high";
    plan.max_quality = profileIndex(config.max_quality) >= 0 ? config.max_quality : "high";
    plan.budget_bytes = config.memory_budget_bytes;
    plan.status = "ok";
    plan.reason = "requested profile fits budget";

    const auto& profiles = qualityProfiles();
    applyProfile(plan, profiles.front(), config);

    const int cap = profileIndex(plan.max_quality);
    if (cap > 0) {
        DegradationStep step;
        step.from_quality = plan.effective_quality;
        step.to_quality = profiles[static_cast<size_t>(cap)].name;
        step.reason = "operator_quality_cap";
        step.estimated_bytes = plan.estimated_bytes;
        step.budget_bytes = plan.budget_bytes;
        step.effect = "cap render resolution and retained history frames";
        plan.degradations.push_back(std::move(step));
        applyProfile(plan, profiles[static_cast<size_t>(cap)], config);
        plan.status = "degraded";
        plan.reason = "quality capped by operator";
    }

    while (plan.estimated_bytes > plan.budget_bytes) {
        const int current = profileIndex(plan.effective_quality);
        if (current < 0 || static_cast<size_t>(current + 1) >= profiles.size()) {
            plan.blocked = true;
            plan.status = "resource_blocked";
            plan.reason = "even the emergency profile exceeds the memory budget";
            break;
        }
        DegradationStep step;
        step.from_quality = plan.effective_quality;
        step.to_quality = profiles[static_cast<size_t>(current + 1)].name;
        step.reason = "memory_budget_exceeded";
        step.estimated_bytes = plan.estimated_bytes;
        step.budget_bytes = plan.budget_bytes;
        step.effect = "reduce render extent, probe workload and retained history frames";
        plan.degradations.push_back(std::move(step));
        applyProfile(plan, profiles[static_cast<size_t>(current + 1)], config);
        plan.status = "degraded";
        plan.reason = "resource budget forced a lower quality profile";
    }
    return plan;
}

bool degradeResourcePlan(ResourcePlan& plan, const AppConfig& config, const std::string& reason) {
    const auto& profiles = qualityProfiles();
    const int current = profileIndex(plan.effective_quality);
    if (current < 0 || static_cast<size_t>(current + 1) >= profiles.size()) {
        plan.blocked = true;
        plan.status = "resource_blocked";
        plan.reason = reason + "; no lower quality profile remains";
        return false;
    }
    DegradationStep step;
    step.from_quality = plan.effective_quality;
    step.to_quality = profiles[static_cast<size_t>(current + 1)].name;
    step.reason = reason;
    step.estimated_bytes = plan.estimated_bytes;
    step.budget_bytes = plan.budget_bytes;
    step.effect = "retry with a smaller Vulkan target and shorter retained history";
    plan.degradations.push_back(std::move(step));
    applyProfile(plan, profiles[static_cast<size_t>(current + 1)], config);
    plan.status = "degraded";
    plan.reason = reason + "; runtime fallback applied";
    return true;
}

RunResult makeRunResult(const AppConfig& config, const ResourcePlan& plan) {
    RunResult result;
    result.mode = config.mode;
    result.clock = config.clock;
    result.plan = plan;
    result.frame_count = config.clock.frame_count;
    result.next_step = 0;
    result.pause_after = config.pause_after;
    result.status = "processing";

    DeviceStream a;
    a.id = "device_a";
    a.device_index = 0;
    a.epoch_us = config.clock.device_a_epoch_us;
    DeviceStream b;
    b.id = "device_b";
    b.device_index = 1;
    b.epoch_us = config.clock.device_b_epoch_us;
    result.devices.push_back(std::move(a));
    result.devices.push_back(std::move(b));

    std::ostringstream identity;
    identity << config.clock.frame_count << '|' << config.clock.frame_period_us << '|'
             << config.clock.device_a_epoch_us << '|' << config.clock.device_b_epoch_us << '|'
             << config.clock.device_b_initial_phase_us << '|' << config.clock.skew_ppm << '|'
             << config.clock.object_period_us << '|' << plan.width << 'x' << plan.height << '|'
             << plan.effective_quality;
    result.run_id = "ph-" + sha256Hex(identity.str()).substr(0, 16);
    return result;
}

std::string frameDigest(const FrameRecord& frame) {
    std::vector<std::string> fields;
    fields.reserve(8 + frame.probes.size() * 8);
    fields.push_back(std::to_string(frame.step));
    fields.push_back(frame.device);
    fields.push_back(std::to_string(frame.raw_clock_us));
    fields.push_back(std::to_string(frame.truth_tick_us));
    fields.push_back(std::to_string(frame.scene_tick_us));
    fields.push_back(std::to_string(frame.calibrated_tick_us));
    fields.push_back(std::to_string(frame.clock_delta_vs_a_us));
    for (const ProbeSample& probe : frame.probes) {
        fields.push_back(probe.probe);
        fields.push_back(std::to_string(probe.x));
        fields.push_back(std::to_string(probe.y));
        fields.push_back(std::to_string(probe.rgba[0]));
        fields.push_back(std::to_string(probe.rgba[1]));
        fields.push_back(std::to_string(probe.rgba[2]));
        fields.push_back(std::to_string(probe.rgba[3]));
        fields.push_back(std::to_string(probe.object_id));
    }
    return digestOfFields(fields);
}

FrameRecord renderAndSampleFrame(const AppConfig& config, const ResourcePlan& plan,
                                 VulkanRenderer& renderer, int device_index, int step,
                                 std::string& error) {
    const ClockStep clocks = clockStep(config.clock, step);
    FrameRecord frame;
    frame.step = step;
    frame.device = device_index == 0 ? "device_a" : "device_b";
    frame.raw_clock_us = device_index == 0 ? clocks.device_a_raw_us : clocks.device_b_raw_us;
    frame.truth_tick_us = clocks.truth_tick_us;
    frame.scene_tick_us = device_index == 0 ? clocks.device_a_scene_us : clocks.device_b_scene_us;
    frame.calibrated_tick_us =
        device_index == 0 ? clocks.device_a_scene_us : clocks.device_b_calibrated_scene_us;
    frame.clock_delta_vs_a_us = device_index == 0 ? 0 : clocks.device_b_minus_a_us;

    std::vector<uint8_t> pixels;
    if (!renderer.renderFrame(frame.scene_tick_us, config.clock,
                              static_cast<uint32_t>(device_index), pixels, error)) {
        error = std::string("Vulkan render failed at step ") + std::to_string(step) + " (" +
                frame.device + "): " + error;
        return FrameRecord{};
    }
    const std::vector<Probe> probes = makeProbes(config.clock, plan.width, plan.height);
    frame.probes.reserve(probes.size());
    for (const Probe& probe : probes) {
        const size_t offset =
            (static_cast<size_t>(probe.y) * static_cast<size_t>(plan.width) +
             static_cast<size_t>(probe.x)) *
            4u;
        if (offset + 3 >= pixels.size()) {
            error = "Vulkan readback buffer is too small";
            return FrameRecord{};
        }
        ProbeSample sample;
        sample.probe = probe.id;
        sample.x = probe.x;
        sample.y = probe.y;
        sample.rgba[0] = pixels[offset];
        sample.rgba[1] = pixels[offset + 1];
        sample.rgba[2] = pixels[offset + 2];
        sample.rgba[3] = pixels[offset + 3];
        sample.object_id =
            objectIdFromRgba(static_cast<uint8_t>(sample.rgba[0]),
                             static_cast<uint8_t>(sample.rgba[1]),
                             static_cast<uint8_t>(sample.rgba[2]),
                             static_cast<uint8_t>(sample.rgba[3]));
        sample.object_name = objectName(sample.object_id);
        const PixelValue model =
            evaluatePixel(config.clock, probe.x, probe.y, plan.width, plan.height,
                          frame.scene_tick_us);
        sample.model_rgba[0] = model.r;
        sample.model_rgba[1] = model.g;
        sample.model_rgba[2] = model.b;
        sample.model_rgba[3] = model.a;
        if (sample.rgba[0] != sample.model_rgba[0] ||
            sample.rgba[1] != sample.model_rgba[1] ||
            sample.rgba[2] != sample.model_rgba[2] ||
            sample.rgba[3] != sample.model_rgba[3] ||
            sample.object_id != model.object_id) {
            std::ostringstream message;
            message << "GPU/CPU pixel mismatch at step " << step << " device " << frame.device
                    << " probe " << probe.id << " actual rgba=(" << sample.rgba[0] << ","
                    << sample.rgba[1] << "," << sample.rgba[2] << "," << sample.rgba[3]
                    << ") expected rgba=(" << sample.model_rgba[0] << ","
                    << sample.model_rgba[1] << "," << sample.model_rgba[2] << ","
                    << sample.model_rgba[3] << ")";
            error = message.str();
            return FrameRecord{};
        }
        frame.probes.push_back(std::move(sample));
    }
    frame.frame_digest = frameDigest(frame);
    frame.cached_rgba = std::move(pixels);
    return frame;
}

bool processFrames(const AppConfig& config, RunResult& result, VulkanRenderer& renderer,
                   int start_step, int end_step, std::string& error) {
    if (start_step < 0 || end_step > config.clock.frame_count || start_step > end_step) {
        error = "invalid frame processing range";
        return false;
    }
    for (int step = start_step; step < end_step; ++step) {
        for (int device = 0; device < 2; ++device) {
            FrameRecord frame =
                renderAndSampleFrame(config, result.plan, renderer, device, step, error);
            if (frame.frame_digest.empty()) {
                return false;
            }
            DeviceStream& stream = result.devices[static_cast<size_t>(device)];
            const size_t retained = static_cast<size_t>(std::max(1, result.plan.retained_frames));
            if (stream.frames.size() >= retained) {
                const size_t evict_count = stream.frames.size() - retained + 1;
                for (size_t i = 0; i < evict_count; ++i) {
                    stream.frames[i].cached_rgba.clear();
                    stream.frames[i].cached_rgba.shrink_to_fit();
                }
            }
            stream.frames.push_back(std::move(frame));
        }
        result.next_step = step + 1;
    }
    if (result.next_step == config.clock.frame_count) {
        result.status = "complete";
        result.paused = false;
    } else {
        result.status = "paused";
        result.paused = true;
    }
    return true;
}

RunSummary summarize(const RunResult& result, int first_step, int limit_step) {
    RunSummary summary;
    std::vector<std::string> digests;
    for (const DeviceStream& stream : result.devices) {
        for (const FrameRecord& frame : stream.frames) {
            if (frame.step < first_step || (limit_step >= 0 && frame.step >= limit_step)) {
                continue;
            }
            ++summary.frames;
            summary.probe_samples += static_cast<int>(frame.probes.size());
            for (const ProbeSample& probe : frame.probes) {
                if (probe.object_id > 0) {
                    ++summary.object_hits;
                }
            }
            digests.push_back(stream.id + "|" + std::to_string(frame.step) + "|" +
                              frame.frame_digest);
        }
    }
    summary.sample_digest = digestOfFields(digests);
    return summary;
}

std::string streamDigest(const RunResult& result) {
    std::vector<std::string> digests;
    for (const DeviceStream& stream : result.devices) {
        for (const FrameRecord& frame : stream.frames) {
            digests.push_back(stream.id + "|" + std::to_string(frame.step) + "|" +
                              frame.frame_digest);
        }
    }
    return digestOfFields(digests);
}

bool verifyResumeFrame(const AppConfig& config, const ResourcePlan& plan,
                       VulkanRenderer& renderer, const RunResult& prefix,
                       ResumeEvidence& evidence, std::string& error) {
    if (prefix.next_step <= 0 || prefix.devices.size() != 2) {
        error = "checkpoint contains no frame to verify";
        return false;
    }
    const int step = prefix.next_step - 1;
    evidence.checkpoint_step = step;
    evidence.compared_pixels = 0;
    evidence.mismatches = 0;
    evidence.frame_digest_match = true;
    for (int device = 0; device < 2; ++device) {
        const DeviceStream& stream = prefix.devices[static_cast<size_t>(device)];
        const auto it = std::find_if(stream.frames.begin(), stream.frames.end(),
                                     [&](const FrameRecord& frame) { return frame.step == step; });
        if (it == stream.frames.end()) {
            error = "checkpoint is missing the last frame for " + stream.id;
            return false;
        }
        FrameRecord rerendered =
            renderAndSampleFrame(config, plan, renderer, device, step, error);
        if (rerendered.frame_digest.empty()) {
            return false;
        }
        if (device == 0) {
            evidence.rerender_digest_device_a = rerendered.frame_digest;
        } else {
            evidence.rerender_digest_device_b = rerendered.frame_digest;
        }
        if (rerendered.frame_digest != it->frame_digest) {
            evidence.frame_digest_match = false;
            ++evidence.mismatches;
        }
        const size_t count = std::min(rerendered.probes.size(), it->probes.size());
        for (size_t i = 0; i < count; ++i) {
            ++evidence.compared_pixels;
            const ProbeSample& before = it->probes[i];
            const ProbeSample& after = rerendered.probes[i];
            if (before.probe != after.probe || before.x != after.x || before.y != after.y ||
                before.rgba[0] != after.rgba[0] || before.rgba[1] != after.rgba[1] ||
                before.rgba[2] != after.rgba[2] || before.rgba[3] != after.rgba[3] ||
                before.object_id != after.object_id) {
                ++evidence.mismatches;
            }
        }
        if (rerendered.probes.size() != it->probes.size()) {
            ++evidence.mismatches;
        }
    }
    evidence.match = evidence.frame_digest_match && evidence.mismatches == 0;
    return true;
}

}  // namespace ph
