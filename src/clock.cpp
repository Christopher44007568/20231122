#include "clock.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

#include "util.h"

namespace {

std::vector<DeviceSpec> deviceSpecs() {
    return {
        {"cam-alpha", "reference-capture", 1000000000LL, 0.0},
        {"cam-beta", "skewed-capture", 875000000LL, 1200.0},
    };
}

int64_t scaleReference(int64_t referenceNs, const DeviceSpec& spec) {
    double scaled = static_cast<double>(referenceNs) * (1.0 + spec.driftPpm / 1000000.0);
    return spec.offsetNs + static_cast<int64_t>(std::llround(scaled));
}

int64_t jitterNs(uint32_t seed, int sequence, int deviceIndex) {
    uint32_t masked = seed ^ (static_cast<uint32_t>(sequence) * 0x9e3779b9U) ^
                      (static_cast<uint32_t>(deviceIndex) * 0x85ebca6bU);
    int32_t spread = static_cast<int32_t>(hash32(masked) % 40001U) - 20000;
    return static_cast<int64_t>(spread);
}

}  // namespace

int64_t referenceTimeForSequence(const Config& config, int sequence) {
    (void)config;
    return static_cast<int64_t>(sequence) * static_cast<int64_t>(framePeriodNs());
}

ClockModel buildClockModel(const Config& config) {
    ClockModel model;
    std::vector<DeviceSpec> specs = deviceSpecs();
    int64_t horizon = referenceTimeForSequence(config, config.frames);
    int64_t probes[2] = {0, horizon};

    for (const DeviceSpec& spec : specs) {
        DeviceCalibration calibration;
        calibration.spec = spec;
        for (int i = 0; i < 2; ++i) {
            calibration.probeReferenceNs[i] = probes[i];
            calibration.probeDeviceNs[i] = scaleReference(probes[i], spec);
        }
        int64_t referenceDelta = calibration.probeReferenceNs[1] - calibration.probeReferenceNs[0];
        int64_t deviceDelta = calibration.probeDeviceNs[1] - calibration.probeDeviceNs[0];
        calibration.deviceNsPerReferenceNs = static_cast<double>(deviceDelta) /
                                             static_cast<double>(referenceDelta);
        calibration.referenceNsPerDeviceNs = static_cast<double>(referenceDelta) /
                                             static_cast<double>(deviceDelta);
        calibration.originReferenceNs = calibration.probeReferenceNs[0];
        calibration.originDeviceNs = calibration.probeDeviceNs[0];
        model.devices.push_back(calibration);
    }

    int taskIndex = 0;
    for (int sequence = 0; sequence < config.frames; ++sequence) {
        int64_t referenceNs = referenceTimeForSequence(config, sequence);
        for (size_t deviceIndex = 0; deviceIndex < model.devices.size(); ++deviceIndex) {
            DeviceCalibration& calibration = model.devices[deviceIndex];
            int64_t deviceTime = scaleReference(referenceNs, calibration.spec) +
                                 jitterNs(config.seed, sequence, static_cast<int>(deviceIndex));
            double estimated = static_cast<double>(calibration.originReferenceNs) +
                               static_cast<double>(deviceTime - calibration.originDeviceNs) *
                                   calibration.referenceNsPerDeviceNs;
            int64_t estimatedNs = static_cast<int64_t>(std::llround(estimated));
            CaptureObservation observation;
            observation.taskIndex = taskIndex++;
            observation.sequence = sequence;
            observation.deviceIndex = static_cast<int>(deviceIndex);
            observation.referenceNs = referenceNs;
            observation.deviceTimeNs = deviceTime;
            observation.estimatedReferenceNs = estimatedNs;
            observation.residualNs = estimatedNs - referenceNs;
            observation.objectUid = "obj-20231122-bokeh";
            calibration.maxAbsResidualNs = std::max(
                calibration.maxAbsResidualNs,
                static_cast<int64_t>(std::llabs(observation.residualNs)));
            model.observations.push_back(observation);
        }
    }

    const DeviceCalibration& alpha = model.devices[0];
    const DeviceCalibration& beta = model.devices[1];
    model.betaMinusAlphaOffsetNs = beta.probeDeviceNs[0] - alpha.probeDeviceNs[0];
    model.betaMinusAlphaDriftPpm =
        (beta.deviceNsPerReferenceNs - alpha.deviceNsPerReferenceNs) * 1000000.0;
    return model;
}

Json observationToJson(const CaptureObservation& observation) {
    Json value = Json::object();
    value.set("task_index", observation.taskIndex);
    value.set("sequence", observation.sequence);
    value.set("device_index", observation.deviceIndex);
    value.set("reference_ns", static_cast<int64_t>(observation.referenceNs));
    value.set("device_time_ns", static_cast<int64_t>(observation.deviceTimeNs));
    value.set("estimated_reference_ns", static_cast<int64_t>(observation.estimatedReferenceNs));
    value.set("residual_ns", static_cast<int64_t>(observation.residualNs));
    value.set("object_uid", observation.objectUid);
    return value;
}

Json clockModelToJson(const ClockModel& model) {
    Json devices = Json::array();
    for (const DeviceCalibration& calibration : model.devices) {
        Json device = Json::object();
        device.set("id", calibration.spec.id);
        device.set("role", calibration.spec.role);
        device.set("configured_offset_ns", static_cast<int64_t>(calibration.spec.offsetNs));
        device.set("configured_drift_ppm", calibration.spec.driftPpm);
        Json probes = Json::array();
        for (int i = 0; i < 2; ++i) {
            Json probe = Json::object();
            probe.set("index", i);
            probe.set("reference_ns", static_cast<int64_t>(calibration.probeReferenceNs[i]));
            probe.set("device_ns", static_cast<int64_t>(calibration.probeDeviceNs[i]));
            probes.push(std::move(probe));
        }
        device.set("sync_probes", std::move(probes));
        device.set("reference_ns_per_device_ns", calibration.referenceNsPerDeviceNs);
        device.set("device_ns_per_reference_ns", calibration.deviceNsPerReferenceNs);
        device.set("origin_reference_ns", static_cast<int64_t>(calibration.originReferenceNs));
        device.set("origin_device_ns", static_cast<int64_t>(calibration.originDeviceNs));
        device.set("max_abs_residual_ns", static_cast<int64_t>(calibration.maxAbsResidualNs));
        devices.push(std::move(device));
    }

    Json result = Json::object();
    result.set("model", "affine-two-probe");
    result.set("devices", std::move(devices));
    result.set("beta_minus_alpha_offset_ns", static_cast<int64_t>(model.betaMinusAlphaOffsetNs));
    result.set("beta_minus_alpha_drift_ppm", model.betaMinusAlphaDriftPpm);
    result.set("max_abs_residual_ns", static_cast<int64_t>(std::max(
        model.devices[0].maxAbsResidualNs, model.devices[1].maxAbsResidualNs)));
    Json observations = Json::array();
    for (const CaptureObservation& observation : model.observations) {
        observations.push(observationToJson(observation));
    }
    result.set("observations", std::move(observations));
    return result;
}

