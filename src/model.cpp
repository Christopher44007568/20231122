#include "model.h"

#include <cmath>

Config defaultConfig() {
    return Config{};
}

Json configToJson(const Config& config) {
    Json value = Json::object();
    value.set("width", config.width);
    value.set("height", config.height);
    value.set("frames", config.frames);
    value.set("pause_after_tasks", config.pauseAfterTasks);
    value.set("memory_budget_bytes", static_cast<int64_t>(config.memoryBudgetBytes));
    value.set("pressure_budget_bytes", static_cast<int64_t>(config.pressureBudgetBytes));
    value.set("backend", config.backend);
    value.set("seed", static_cast<int64_t>(config.seed));
    value.set("object_uid", "obj-20231122-bokeh");
    return value;
}

Json renderPlanToJson(const RenderPlan& plan) {
    Json value = Json::object();
    value.set("requested_backend", plan.requestedBackend);
    value.set("effective_backend", plan.effectiveBackend);
    value.set("degraded", plan.degraded);
    value.set("reason", plan.reason);
    value.set("budget_bytes", static_cast<int64_t>(plan.budgetBytes));
    value.set("full_transient_bytes", static_cast<int64_t>(plan.fullTransientBytes));
    value.set("band_height", plan.bandHeight);
    value.set("band_count", plan.bandCount);
    return value;
}

Json resourceEventToJson(const ResourceEvent& event) {
    Json value = Json::object();
    value.set("sequence", event.sequence);
    value.set("stage", event.stage);
    value.set("severity", event.severity);
    value.set("code", event.code);
    value.set("requested_bytes", static_cast<int64_t>(event.requestedBytes));
    value.set("available_bytes", static_cast<int64_t>(event.availableBytes));
    value.set("action", event.action);
    value.set("detail", event.detail);
    return value;
}

