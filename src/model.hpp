#pragma once

#include "json.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rr {

static constexpr const char* kSchema = "raster-regression/v1";

struct Limits {
    std::size_t max_input_bytes = 64U * 1024U * 1024U;
    std::size_t max_vertices = 1'000'000U;
    std::size_t max_indices = 4'000'000U;
    std::size_t max_rules = 256U;
    std::size_t max_draws = 100'000U;
    std::size_t max_passes = 128U;
    std::size_t max_draws_per_pass = 100'000U;
    std::size_t max_reference_pixels = 1'000'000U;
    std::size_t max_shared_edges = 100'000U;
    std::uint32_t max_framebuffer_dimension = 4096U;
    std::size_t max_total_draw_submissions = 100'000U;
    std::size_t max_saved_draw_images = 32U;
    std::size_t max_reported_reference_pixels = 4096U;
    std::size_t max_reported_edge_samples = 10'000U;
};

struct Vertex {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
    float w = 1.0F;
    std::array<std::uint8_t, 4> rgba{255U, 255U, 255U, 255U};
};

struct Rule {
    std::string id;
    std::string polygon_mode = "fill";
    std::string cull_mode = "none";
    std::string front_face = "ccw";
    double line_width = 1.0;
    bool depth_clamp = false;
    bool rasterizer_discard = false;
    double depth_bias_constant = 0.0;
    double depth_bias_slope = 0.0;
    double depth_bias_clamp = 0.0;
    double viewport_x = 0.0;
    double viewport_y = 0.0;
    double viewport_width = -1.0;
    double viewport_height = -1.0;
    double scissor_x = 0.0;
    double scissor_y = 0.0;
    double scissor_width = -1.0;
    double scissor_height = -1.0;
    bool additive_blend = false;
};

struct Draw {
    std::string id;
    std::uint32_t first_index = 0;
    std::uint32_t index_count = 0;
    std::uint32_t instance_count = 1;
};

struct Pass {
    std::string id;
    std::string rule;
    std::vector<std::string> draws;
};

struct SharedEdge {
    std::string id;
    std::string draw_a;
    std::string draw_b;
    std::array<double, 2> a{0.0, 0.0};
    std::array<double, 2> b{0.0, 0.0};
};

struct ReferencePixel {
    std::string pass;
    std::uint32_t x = 0;
    std::uint32_t y = 0;
    std::array<std::uint8_t, 4> rgba{0U, 0U, 0U, 0U};
    std::uint32_t tolerance = 0;
};

struct Boundary {
    std::string parameter;
    std::string before_pass;
    std::string after_pass;
    double threshold = 0.0;
    bool has_sample_pixel = false;
    std::uint32_t sample_x = 0;
    std::uint32_t sample_y = 0;
};

struct Scenario {
    std::string name;
    std::uint32_t width = 64;
    std::uint32_t height = 64;
    std::array<std::uint8_t, 4> clear_color{16U, 16U, 24U, 255U};
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<Rule> rules;
    std::vector<Draw> draws;
    std::vector<Pass> passes;
    std::vector<SharedEdge> shared_edges;
    std::vector<ReferencePixel> reference_pixels;
    std::optional<Boundary> boundary;
    bool empty_input = false;
};

struct Diagnostic {
    std::string path;
    std::string message;
};

struct LoadResult {
    Scenario scenario;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;
    JsonError json_error;
    bool ok() const { return errors.empty(); }
};

LoadResult loadScenario(std::string_view text, const Limits& limits = {});
const Rule* findRule(const Scenario& scenario, std::string_view id);
const Draw* findDraw(const Scenario& scenario, std::string_view id);
const Pass* findPass(const Scenario& scenario, std::string_view id);

std::string ruleParameter(const Rule& rule, std::string_view parameter, bool* found);
JsonValue ruleToJson(const Rule& rule);

bool nearlyEqual(double a, double b);
bool finiteArray(const JsonValue& value, std::size_t count);

}  // namespace rr
