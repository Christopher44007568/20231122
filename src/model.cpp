#include "model.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>

namespace rr {
namespace {

std::uint8_t toByte(double value) {
    const double clamped = std::max(0.0, std::min(255.0, value));
    return static_cast<std::uint8_t>(std::lround(clamped));
}

bool isInteger(double value) {
    return std::isfinite(value) && std::floor(value) == value;
}

bool readByteArray(const JsonValue& value,
                   std::array<std::uint8_t, 4>& out,
                   std::string& reason) {
    if (!value.isArray() || value.asArray().size() != 4U) {
        reason = "expected an array of four channel values";
        return false;
    }
    for (const auto& channel : value.asArray()) {
        if (!channel.isNumber() || !std::isfinite(channel.asNumber())) {
            reason = "channels must be finite numbers";
            return false;
        }
    }
    for (std::size_t i = 0; i < 4U; ++i) {
        out[i] = toByte(value.asArray()[i].asNumber());
    }
    return true;
}

bool readUnsigned(const JsonValue& value,
                  std::uint32_t& out,
                  std::string& reason,
                  std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max()) {
    if (!value.isNumber() || !isInteger(value.asNumber())) {
        reason = "expected an integer";
        return false;
    }
    const double number = value.asNumber();
    if (number < 0.0 || number > static_cast<double>(maximum)) {
        reason = "integer is outside the supported range";
        return false;
    }
    out = static_cast<std::uint32_t>(number);
    return true;
}

bool readFinite(const JsonValue& value, double& out, std::string& reason) {
    if (!value.isNumber() || !std::isfinite(value.asNumber())) {
        reason = "expected a finite number";
        return false;
    }
    out = value.asNumber();
    return true;
}

std::string readString(const JsonValue& value, const std::string& fallback = {}) {
    return value.isString() ? value.asString() : fallback;
}

bool oneOf(std::string_view value, std::initializer_list<std::string_view> choices) {
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

}  // namespace

bool nearlyEqual(double a, double b) {
    const double scale = std::max({1.0, std::fabs(a), std::fabs(b)});
    return std::fabs(a - b) <= std::numeric_limits<double>::epsilon() * scale * 8.0;
}

bool finiteArray(const JsonValue& value, std::size_t count) {
    if (!value.isArray() || value.asArray().size() != count) {
        return false;
    }
    for (const auto& item : value.asArray()) {
        if (!item.isNumber() || !std::isfinite(item.asNumber())) {
            return false;
        }
    }
    return true;
}

const Rule* findRule(const Scenario& scenario, std::string_view id) {
    for (const auto& rule : scenario.rules) {
        if (rule.id == id) {
            return &rule;
        }
    }
    return nullptr;
}

const Draw* findDraw(const Scenario& scenario, std::string_view id) {
    for (const auto& draw : scenario.draws) {
        if (draw.id == id) {
            return &draw;
        }
    }
    return nullptr;
}

const Pass* findPass(const Scenario& scenario, std::string_view id) {
    for (const auto& pass : scenario.passes) {
        if (pass.id == id) {
            return &pass;
        }
    }
    return nullptr;
}

std::string ruleParameter(const Rule& rule, std::string_view parameter, bool* found) {
    if (found) {
        *found = true;
    }
    if (parameter == "line_width") return jsonNumber(rule.line_width);
    if (parameter == "polygon_mode") return jsonEscape(rule.polygon_mode);
    if (parameter == "cull_mode") return jsonEscape(rule.cull_mode);
    if (parameter == "front_face") return jsonEscape(rule.front_face);
    if (parameter == "depth_clamp") return rule.depth_clamp ? "true" : "false";
    if (parameter == "rasterizer_discard") return rule.rasterizer_discard ? "true" : "false";
    if (parameter == "depth_bias_constant") return jsonNumber(rule.depth_bias_constant);
    if (parameter == "depth_bias_slope") return jsonNumber(rule.depth_bias_slope);
    if (parameter == "depth_bias_clamp") return jsonNumber(rule.depth_bias_clamp);
    if (parameter == "viewport_x") return jsonNumber(rule.viewport_x);
    if (parameter == "viewport_y") return jsonNumber(rule.viewport_y);
    if (parameter == "viewport_width") return jsonNumber(rule.viewport_width);
    if (parameter == "viewport_height") return jsonNumber(rule.viewport_height);
    if (parameter == "scissor_x") return jsonNumber(rule.scissor_x);
    if (parameter == "scissor_y") return jsonNumber(rule.scissor_y);
    if (parameter == "scissor_width") return jsonNumber(rule.scissor_width);
    if (parameter == "scissor_height") return jsonNumber(rule.scissor_height);
    if (parameter == "additive_blend") return rule.additive_blend ? "true" : "false";
    if (found) {
        *found = false;
    }
    return "null";
}

JsonValue ruleToJson(const Rule& rule) {
    JsonValue object = JsonValue::object();
    auto& fields = object.asObject();
    fields.emplace_back("id", JsonValue::string(rule.id));
    fields.emplace_back("polygon_mode", JsonValue::string(rule.polygon_mode));
    fields.emplace_back("cull_mode", JsonValue::string(rule.cull_mode));
    fields.emplace_back("front_face", JsonValue::string(rule.front_face));
    fields.emplace_back("line_width", JsonValue::number(rule.line_width));
    fields.emplace_back("depth_clamp", JsonValue::boolean(rule.depth_clamp));
    fields.emplace_back("rasterizer_discard", JsonValue::boolean(rule.rasterizer_discard));
    fields.emplace_back("depth_bias_constant", JsonValue::number(rule.depth_bias_constant));
    fields.emplace_back("depth_bias_slope", JsonValue::number(rule.depth_bias_slope));
    fields.emplace_back("depth_bias_clamp", JsonValue::number(rule.depth_bias_clamp));
    fields.emplace_back("viewport_x", JsonValue::number(rule.viewport_x));
    fields.emplace_back("viewport_y", JsonValue::number(rule.viewport_y));
    fields.emplace_back("viewport_width", JsonValue::number(rule.viewport_width));
    fields.emplace_back("viewport_height", JsonValue::number(rule.viewport_height));
    fields.emplace_back("scissor_x", JsonValue::number(rule.scissor_x));
    fields.emplace_back("scissor_y", JsonValue::number(rule.scissor_y));
    fields.emplace_back("scissor_width", JsonValue::number(rule.scissor_width));
    fields.emplace_back("scissor_height", JsonValue::number(rule.scissor_height));
    fields.emplace_back("additive_blend", JsonValue::boolean(rule.additive_blend));
    return object;
}

namespace {

void addError(LoadResult& result, std::string path, std::string message) {
    result.errors.push_back({std::move(path), std::move(message)});
}

void addWarning(LoadResult& result, std::string path, std::string message) {
    result.warnings.push_back({std::move(path), std::move(message)});
}

bool parseRule(const JsonValue& value,
               std::size_t index,
               LoadResult& result,
               Rule& rule) {
    const std::string base = "rules[" + std::to_string(index) + "]";
    if (!value.isObject()) {
        addError(result, base, "expected an object");
        return false;
    }
    if (const JsonValue* id = value.find("id")) {
        rule.id = readString(*id);
    }
    if (rule.id.empty()) {
        addError(result, base + ".id", "non-empty id is required");
    }
    if (const JsonValue* field = value.find("polygon_mode")) {
        rule.polygon_mode = readString(*field);
        if (!oneOf(rule.polygon_mode, {"fill", "line", "point"})) {
            addError(result, base + ".polygon_mode", "expected fill, line, or point");
        }
    }
    if (const JsonValue* field = value.find("cull_mode")) {
        rule.cull_mode = readString(*field);
        if (!oneOf(rule.cull_mode, {"none", "front", "back"})) {
            addError(result, base + ".cull_mode", "expected none, front, or back");
        }
    }
    if (const JsonValue* field = value.find("front_face")) {
        rule.front_face = readString(*field);
        if (!oneOf(rule.front_face, {"ccw", "cw"})) {
            addError(result, base + ".front_face", "expected ccw or cw");
        }
    }
    std::string reason;
    if (const JsonValue* field = value.find("line_width")) {
        if (!readFinite(*field, rule.line_width, reason) || rule.line_width <= 0.0) {
            addError(result, base + ".line_width",
                     reason.empty() ? "must be greater than zero" : reason);
        }
    }
    if (const JsonValue* field = value.find("depth_clamp")) {
        if (!field->isBool()) addError(result, base + ".depth_clamp", "expected boolean");
        rule.depth_clamp = field->asBool();
    }
    if (const JsonValue* field = value.find("rasterizer_discard")) {
        if (!field->isBool()) addError(result, base + ".rasterizer_discard", "expected boolean");
        rule.rasterizer_discard = field->asBool();
    }
    for (const auto& item : std::initializer_list<std::pair<const char*, double*>>{
             {"depth_bias_constant", &rule.depth_bias_constant},
             {"depth_bias_slope", &rule.depth_bias_slope},
             {"depth_bias_clamp", &rule.depth_bias_clamp},
             {"viewport_x", &rule.viewport_x},
             {"viewport_y", &rule.viewport_y},
             {"viewport_width", &rule.viewport_width},
             {"viewport_height", &rule.viewport_height},
             {"scissor_x", &rule.scissor_x},
             {"scissor_y", &rule.scissor_y},
             {"scissor_width", &rule.scissor_width},
             {"scissor_height", &rule.scissor_height}}) {
        if (const JsonValue* field = value.find(item.first)) {
            if (!readFinite(*field, *item.second, reason)) {
                addError(result, base + "." + item.first, reason);
            }
        }
    }
    if (rule.viewport_width == 0.0 || rule.viewport_height == 0.0 ||
        rule.scissor_width == 0.0 || rule.scissor_height == 0.0) {
        addError(result, base, "viewport/scissor dimensions must not be zero");
    }
    if (const JsonValue* field = value.find("additive_blend")) {
        if (!field->isBool()) addError(result, base + ".additive_blend", "expected boolean");
        rule.additive_blend = field->asBool();
    }
    return true;
}

}  // namespace

LoadResult loadScenario(std::string_view text, const Limits& limits) {
    LoadResult result;
    if (text.empty() ||
        std::all_of(text.begin(), text.end(), [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        })) {
        result.scenario.name = "empty-input";
        result.scenario.width = 16;
        result.scenario.height = 16;
        result.scenario.empty_input = true;
        addWarning(result, "input", "empty input accepted as an empty regression scene");
        return result;
    }

    JsonValue root;
    JsonParser parser;
    if (!parser.parse(text, root, result.json_error)) {
        addError(result, "input",
                 result.json_error.message + " at line " +
                     std::to_string(result.json_error.line) + ", column " +
                     std::to_string(result.json_error.column));
        return result;
    }
    if (!root.isObject()) {
        addError(result, "input", "root JSON value must be an object");
        return result;
    }
    if (const JsonValue* schema = root.find("schema")) {
        if (!schema->isString() || schema->asString() != kSchema) {
            addError(result, "schema", std::string("expected \"") + kSchema + "\"");
        }
    }
    result.scenario.name = root.find("name") ? readString(*root.find("name"), "unnamed")
                                             : "unnamed";
    if (result.scenario.name.empty()) {
        result.scenario.name = "unnamed";
    }

    if (const JsonValue* framebuffer = root.find("framebuffer")) {
        if (!framebuffer->isObject()) {
            addError(result, "framebuffer", "expected an object");
        } else {
            if (const JsonValue* width = framebuffer->find("width")) {
                std::string reason;
                if (!readUnsigned(*width, result.scenario.width, reason,
                                  limits.max_framebuffer_dimension) ||
                    result.scenario.width == 0U) {
                    addError(result, "framebuffer.width",
                             reason.empty() ? "must be in [1, max_framebuffer_dimension]" : reason);
                }
            }
            if (const JsonValue* height = framebuffer->find("height")) {
                std::string reason;
                if (!readUnsigned(*height, result.scenario.height, reason,
                                  limits.max_framebuffer_dimension) ||
                    result.scenario.height == 0U) {
                    addError(result, "framebuffer.height",
                             reason.empty() ? "must be in [1, max_framebuffer_dimension]" : reason);
                }
            }
        }
    }
    if (const JsonValue* clear = root.find("clear_color")) {
        std::string reason;
        if (!readByteArray(*clear, result.scenario.clear_color, reason)) {
            addError(result, "clear_color", reason);
        }
    }

    if (const JsonValue* vertices = root.find("vertices")) {
        if (!vertices->isArray()) {
            addError(result, "vertices", "expected an array");
        } else if (vertices->asArray().size() > limits.max_vertices) {
            addError(result, "vertices", "vertex count exceeds max_vertices");
        } else {
            result.scenario.vertices.reserve(vertices->asArray().size());
            for (std::size_t i = 0; i < vertices->asArray().size(); ++i) {
                const JsonValue& value = vertices->asArray()[i];
                const std::string base = "vertices[" + std::to_string(i) + "]";
                if (!value.isObject()) {
                    addError(result, base, "expected an object");
                    continue;
                }
                Vertex vertex;
                const JsonValue* position = value.find("position");
                if (!position || !position->isArray() ||
                    (position->asArray().size() != 2U && position->asArray().size() != 4U)) {
                    addError(result, base + ".position", "expected [x,y] or [x,y,z,w]");
                } else {
                    double values[4] = {0.0, 0.0, 0.0, 1.0};
                    for (std::size_t component = 0; component < position->asArray().size();
                         ++component) {
                        if (!position->asArray()[component].isNumber() ||
                            !std::isfinite(position->asArray()[component].asNumber())) {
                            addError(result, base + ".position", "components must be finite");
                            break;
                        }
                        values[component] = position->asArray()[component].asNumber();
                    }
                    vertex.x = static_cast<float>(values[0]);
                    vertex.y = static_cast<float>(values[1]);
                    vertex.z = static_cast<float>(values[2]);
                    vertex.w = static_cast<float>(values[3]);
                    if (vertex.w == 0.0F) {
                        addWarning(result, base + ".position", "w=0 creates a clipped/invalid vertex");
                    }
                }
                if (const JsonValue* color = value.find("rgba")) {
                    std::string reason;
                    if (!readByteArray(*color, vertex.rgba, reason)) {
                        addError(result, base + ".rgba", reason);
                    }
                }
                result.scenario.vertices.push_back(vertex);
            }
        }
    }

    if (const JsonValue* indices = root.find("indices")) {
        if (!indices->isArray()) {
            addError(result, "indices", "expected an array");
        } else if (indices->asArray().size() > limits.max_indices) {
            addError(result, "indices", "index count exceeds max_indices");
        } else {
            result.scenario.indices.reserve(indices->asArray().size());
            for (std::size_t i = 0; i < indices->asArray().size(); ++i) {
                std::uint32_t index = 0;
                std::string reason;
                if (!readUnsigned(indices->asArray()[i], index, reason)) {
                    addError(result, "indices[" + std::to_string(i) + "]", reason);
                    continue;
                }
                if (index >= result.scenario.vertices.size() &&
                    !result.scenario.vertices.empty()) {
                    addError(result, "indices[" + std::to_string(i) + "]",
                             "index is outside the vertex array");
                }
                result.scenario.indices.push_back(index);
            }
        }
    } else if (!result.scenario.vertices.empty()) {
        result.scenario.indices.reserve(result.scenario.vertices.size());
        for (std::size_t i = 0; i < result.scenario.vertices.size(); ++i) {
            result.scenario.indices.push_back(static_cast<std::uint32_t>(i));
        }
        addWarning(result, "indices", "indices omitted; generated sequential indices");
    }

    const JsonValue* rules = root.find("rules");
    if (!rules) {
        rules = root.find("rasterization_rules");
    }
    if (rules) {
        if (!rules->isArray()) {
            addError(result, "rules", "expected an array");
        } else if (rules->asArray().size() > limits.max_rules) {
            addError(result, "rules", "rule count exceeds max_rules");
        } else {
            std::set<std::string> ids;
            result.scenario.rules.reserve(rules->asArray().size());
            for (std::size_t i = 0; i < rules->asArray().size(); ++i) {
                Rule rule;
                parseRule(rules->asArray()[i], i, result, rule);
                if (!rule.id.empty() && !ids.insert(rule.id).second) {
                    addError(result, "rules[" + std::to_string(i) + "].id",
                             "duplicate rule id");
                }
                result.scenario.rules.push_back(std::move(rule));
            }
        }
    }

    if (const JsonValue* draws = root.find("draws")) {
        if (!draws->isArray()) {
            addError(result, "draws", "expected an array");
        } else if (draws->asArray().size() > limits.max_draws) {
            addError(result, "draws", "draw count exceeds max_draws");
        } else {
            std::set<std::string> ids;
            result.scenario.draws.reserve(draws->asArray().size());
            for (std::size_t i = 0; i < draws->asArray().size(); ++i) {
                const JsonValue& value = draws->asArray()[i];
                const std::string base = "draws[" + std::to_string(i) + "]";
                if (!value.isObject()) {
                    addError(result, base, "expected an object");
                    continue;
                }
                Draw draw;
                if (const JsonValue* id = value.find("id")) {
                    draw.id = readString(*id);
                }
                if (draw.id.empty()) {
                    addError(result, base + ".id", "non-empty id is required");
                } else if (!ids.insert(draw.id).second) {
                    addError(result, base + ".id", "duplicate draw id");
                }
                std::string reason;
                if (const JsonValue* field = value.find("first_index")) {
                    if (!readUnsigned(*field, draw.first_index, reason)) {
                        addError(result, base + ".first_index", reason);
                    }
                }
                if (const JsonValue* field = value.find("index_count")) {
                    if (!readUnsigned(*field, draw.index_count, reason)) {
                        addError(result, base + ".index_count", reason);
                    }
                } else {
                    addError(result, base + ".index_count", "index_count is required");
                }
                if (const JsonValue* field = value.find("instance_count")) {
                    if (!readUnsigned(*field, draw.instance_count, reason, 1'000'000U)) {
                        addError(result, base + ".instance_count", reason);
                    }
                }
                const std::uint64_t end =
                    static_cast<std::uint64_t>(draw.first_index) + draw.index_count;
                if (end > result.scenario.indices.size()) {
                    addError(result, base,
                             "first_index + index_count exceeds the index buffer");
                }
                result.scenario.draws.push_back(std::move(draw));
            }
        }
    }

    if (const JsonValue* passes = root.find("passes")) {
        if (!passes->isArray()) {
            addError(result, "passes", "expected an array");
        } else if (passes->asArray().size() > limits.max_passes) {
            addError(result, "passes", "pass count exceeds max_passes");
        } else {
            std::set<std::string> ids;
            std::size_t submissions = 0U;
            result.scenario.passes.reserve(passes->asArray().size());
            for (std::size_t i = 0; i < passes->asArray().size(); ++i) {
                const JsonValue& value = passes->asArray()[i];
                const std::string base = "passes[" + std::to_string(i) + "]";
                if (!value.isObject()) {
                    addError(result, base, "expected an object");
                    continue;
                }
                Pass pass;
                if (const JsonValue* id = value.find("id")) {
                    pass.id = readString(*id);
                }
                if (pass.id.empty()) {
                    addError(result, base + ".id", "non-empty id is required");
                } else if (!ids.insert(pass.id).second) {
                    addError(result, base + ".id", "duplicate pass id");
                }
                if (const JsonValue* rule = value.find("rule")) {
                    pass.rule = readString(*rule);
                }
                if (!findRule(result.scenario, pass.rule)) {
                    addError(result, base + ".rule", "rule id does not exist");
                }
                if (const JsonValue* drawList = value.find("draws")) {
                    if (!drawList->isArray()) {
                        addError(result, base + ".draws", "expected an array");
                    } else if (drawList->asArray().size() > limits.max_draws_per_pass) {
                        addError(result, base + ".draws",
                                 "draw count exceeds max_draws_per_pass");
                    } else {
                        for (std::size_t drawIndex = 0; drawIndex < drawList->asArray().size();
                             ++drawIndex) {
                            if (!drawList->asArray()[drawIndex].isString()) {
                                addError(result, base + ".draws[" + std::to_string(drawIndex) + "]",
                                         "expected a draw id string");
                                continue;
                            }
                            const std::string& draw = drawList->asArray()[drawIndex].asString();
                            if (!findDraw(result.scenario, draw)) {
                                addError(result,
                                         base + ".draws[" + std::to_string(drawIndex) + "]",
                                         "draw id does not exist");
                            }
                            pass.draws.push_back(draw);
                        }
                    }
                }
                submissions += pass.draws.size();
                result.scenario.passes.push_back(std::move(pass));
            }
            if (submissions > limits.max_total_draw_submissions) {
                addError(result, "passes",
                         "total draw submissions exceeds max_total_draw_submissions");
            }
        }
    }

    const JsonValue* sharedEdges = root.find("shared_edges");
    if (sharedEdges) {
        if (!sharedEdges->isArray()) {
            addError(result, "shared_edges", "expected an array");
        } else if (sharedEdges->asArray().size() > limits.max_shared_edges) {
            addError(result, "shared_edges", "shared edge count exceeds max_shared_edges");
        } else {
            std::set<std::string> ids;
            result.scenario.shared_edges.reserve(sharedEdges->asArray().size());
            for (std::size_t i = 0; i < sharedEdges->asArray().size(); ++i) {
                const JsonValue& value = sharedEdges->asArray()[i];
                const std::string base = "shared_edges[" + std::to_string(i) + "]";
                if (!value.isObject()) {
                    addError(result, base, "expected an object");
                    continue;
                }
                SharedEdge edge;
                if (const JsonValue* id = value.find("id")) {
                    edge.id = readString(*id, "edge-" + std::to_string(i));
                }
                if (edge.id.empty()) {
                    edge.id = "edge-" + std::to_string(i);
                }
                if (!ids.insert(edge.id).second) {
                    addError(result, base + ".id", "duplicate shared edge id");
                }
                if (const JsonValue* a = value.find("draw_a")) {
                    edge.draw_a = readString(*a);
                }
                if (const JsonValue* b = value.find("draw_b")) {
                    edge.draw_b = readString(*b);
                }
                if (!findDraw(result.scenario, edge.draw_a)) {
                    addError(result, base + ".draw_a", "draw id does not exist");
                }
                if (!findDraw(result.scenario, edge.draw_b)) {
                    addError(result, base + ".draw_b", "draw id does not exist");
                }
                const JsonValue* a = value.find("a");
                const JsonValue* b = value.find("b");
                if (!a || !finiteArray(*a, 2U) || !b || !finiteArray(*b, 2U)) {
                    addError(result, base, "a and b must be finite [x,y] clip-space points");
                } else {
                    edge.a = {a->asArray()[0].asNumber(), a->asArray()[1].asNumber()};
                    edge.b = {b->asArray()[0].asNumber(), b->asArray()[1].asNumber()};
                }
                result.scenario.shared_edges.push_back(std::move(edge));
            }
        }
    }

    const JsonValue* references = root.find("reference_pixels");
    if (references) {
        if (!references->isArray()) {
            addError(result, "reference_pixels", "expected an array");
        } else if (references->asArray().size() > limits.max_reference_pixels) {
            addError(result, "reference_pixels",
                     "reference pixel count exceeds max_reference_pixels");
        } else {
            result.scenario.reference_pixels.reserve(references->asArray().size());
            for (std::size_t i = 0; i < references->asArray().size(); ++i) {
                const JsonValue& value = references->asArray()[i];
                const std::string base = "reference_pixels[" + std::to_string(i) + "]";
                if (!value.isObject()) {
                    addError(result, base, "expected an object");
                    continue;
                }
                ReferencePixel pixel;
                if (const JsonValue* pass = value.find("pass")) {
                    pixel.pass = readString(*pass);
                    if (!pixel.pass.empty() && !findPass(result.scenario, pixel.pass)) {
                        addError(result, base + ".pass", "pass id does not exist");
                    }
                }
                std::string reason;
                const JsonValue* x = value.find("x");
                const JsonValue* y = value.find("y");
                if (!x || !readUnsigned(*x, pixel.x, reason)) {
                    addError(result, base + ".x", reason.empty() ? "x is required" : reason);
                }
                if (!y || !readUnsigned(*y, pixel.y, reason)) {
                    addError(result, base + ".y", reason.empty() ? "y is required" : reason);
                }
                if (x && y && (pixel.x >= result.scenario.width ||
                               pixel.y >= result.scenario.height)) {
                    addError(result, base, "reference pixel is outside the framebuffer");
                }
                const JsonValue* rgba = value.find("rgba");
                if (!rgba || !readByteArray(*rgba, pixel.rgba, reason)) {
                    addError(result, base + ".rgba", reason.empty() ? "rgba is required" : reason);
                }
                if (const JsonValue* tolerance = value.find("tolerance")) {
                    if (!readUnsigned(*tolerance, pixel.tolerance, reason, 255U)) {
                        addError(result, base + ".tolerance", reason);
                    }
                }
                result.scenario.reference_pixels.push_back(std::move(pixel));
            }
        }
    }

    if (const JsonValue* boundary = root.find("boundary")) {
        if (!boundary->isObject()) {
            addError(result, "boundary", "expected an object");
        } else {
            Boundary parsed;
            parsed.parameter = boundary->find("parameter")
                                   ? readString(*boundary->find("parameter"))
                                   : std::string();
            parsed.before_pass = boundary->find("before_pass")
                                     ? readString(*boundary->find("before_pass"))
                                     : std::string();
            parsed.after_pass = boundary->find("after_pass")
                                    ? readString(*boundary->find("after_pass"))
                                    : std::string();
            if (parsed.parameter.empty()) {
                addError(result, "boundary.parameter", "parameter is required");
            }
            if (!findPass(result.scenario, parsed.before_pass)) {
                addError(result, "boundary.before_pass", "pass id does not exist");
            }
            if (!findPass(result.scenario, parsed.after_pass)) {
                addError(result, "boundary.after_pass", "pass id does not exist");
            }
            if (parsed.before_pass == parsed.after_pass) {
                addError(result, "boundary", "before_pass and after_pass must differ");
            }
            std::string reason;
            const JsonValue* threshold = boundary->find("threshold");
            if (!threshold || !readFinite(*threshold, parsed.threshold, reason)) {
                addError(result, "boundary.threshold",
                         reason.empty() ? "threshold is required" : reason);
            }
            if (const JsonValue* sample = boundary->find("sample_pixel")) {
                if (sample->isArray() && sample->asArray().size() == 2U) {
                    std::uint32_t x = 0;
                    std::uint32_t y = 0;
                    if (!readUnsigned(sample->asArray()[0], x, reason) ||
                        !readUnsigned(sample->asArray()[1], y, reason)) {
                        addError(result, "boundary.sample_pixel", reason);
                    } else {
                        parsed.has_sample_pixel = true;
                        parsed.sample_x = x;
                        parsed.sample_y = y;
                    }
                } else if (sample->isObject()) {
                    const JsonValue* x = sample->find("x");
                    const JsonValue* y = sample->find("y");
                    if (!x || !y || !readUnsigned(*x, parsed.sample_x, reason) ||
                        !readUnsigned(*y, parsed.sample_y, reason)) {
                        addError(result, "boundary.sample_pixel", "expected {x,y}");
                    } else {
                        parsed.has_sample_pixel = true;
                    }
                } else {
                    addError(result, "boundary.sample_pixel", "expected [x,y] or {x,y}");
                }
                if (parsed.has_sample_pixel &&
                    (parsed.sample_x >= result.scenario.width ||
                     parsed.sample_y >= result.scenario.height)) {
                    addError(result, "boundary.sample_pixel",
                             "sample pixel is outside the framebuffer");
                }
            }
            result.scenario.boundary = parsed;
        }
    }

    if (result.scenario.passes.empty()) {
        addWarning(result, "passes", "no passes; scenario produces no draw calls");
    }

    return result;
}

}  // namespace rr
