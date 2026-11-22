#include "runner.hpp"

#include "checksum.hpp"
#include "evidence.hpp"
#include "platform.hpp"
#include "sample_cases.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>

namespace rr {
namespace {

std::string utcNow() {
    const std::time_t now = std::time(nullptr);
    std::tm value{};
#if defined(_WIN32)
    gmtime_s(&value, &now);
#else
    gmtime_r(&now, &value);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &value);
    return std::string(buffer);
}

bool readInputFile(const std::filesystem::path& path,
                   std::size_t maxBytes,
                   std::string& text,
                   std::uint64_t& bytes,
                   bool& tooLarge,
                   std::string& error) {
    std::error_code code;
    const std::uintmax_t size = std::filesystem::file_size(path, code);
    if (code) {
        error = "cannot stat input file: " + code.message();
        return false;
    }
    bytes = static_cast<std::uint64_t>(size);
    if (size > maxBytes) {
        tooLarge = true;
        return true;
    }
    tooLarge = false;
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open input file";
        return false;
    }
    text.assign(std::istreambuf_iterator<char>(stream),
                std::istreambuf_iterator<char>());
    if (!stream.good() && !stream.eof()) {
        error = "cannot read input file";
        return false;
    }
    return true;
}

std::string oversizedPrefixHash(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::string prefix(64U * 1024U, '\0');
    stream.read(prefix.data(), static_cast<std::streamsize>(prefix.size()));
    prefix.resize(static_cast<std::size_t>(stream.gcount()));
    return sha256Hex(prefix);
}

std::uint32_t channelDelta(const std::array<std::uint8_t, 4>& a,
                           const std::array<std::uint8_t, 4>& b) {
    std::uint32_t delta = 0;
    for (std::size_t channel = 0; channel < 4U; ++channel) {
        delta = std::max(delta,
                         static_cast<std::uint32_t>(
                             std::abs(static_cast<int>(a[channel]) -
                                      static_cast<int>(b[channel]))));
    }
    return delta;
}

std::array<std::uint8_t, 4> samplePixel(const Image& image,
                                        int x,
                                        int y) {
    if (x < 0 || y < 0 ||
        static_cast<std::uint32_t>(x) >= image.width ||
        static_cast<std::uint32_t>(y) >= image.height) {
        return {0U, 0U, 0U, 0U};
    }
    return image.pixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

std::array<double, 2> toFramebuffer(double x,
                                    double y,
                                    const Rule& rule,
                                    std::uint32_t width,
                                    std::uint32_t height) {
    const double viewportX = rule.viewport_x;
    const double viewportY = rule.viewport_y;
    const double viewportWidth =
        rule.viewport_width < 0.0 ? static_cast<double>(width) : rule.viewport_width;
    const double viewportHeight =
        rule.viewport_height < 0.0 ? static_cast<double>(height) : rule.viewport_height;
    return {viewportX + (x + 1.0) * 0.5 * viewportWidth,
            viewportY + (y + 1.0) * 0.5 * viewportHeight};
}

void addEdgeSamples(std::vector<SharedEdgeEvidence>& edges,
                    const Scenario& scenario,
                    const Pass& pass,
                    const RenderPassResult& render,
                    const Limits& limits,
                    std::size_t& totalSamples) {
    for (const auto& source : scenario.shared_edges) {
        auto iterator = std::find_if(
            edges.begin(), edges.end(),
            [&source](const SharedEdgeEvidence& item) { return item.id == source.id; });
        if (iterator == edges.end()) {
            SharedEdgeEvidence evidence;
            evidence.id = source.id;
            evidence.drawA = source.draw_a;
            evidence.drawB = source.draw_b;
            evidence.a = source.a;
            evidence.b = source.b;
            edges.push_back(std::move(evidence));
            iterator = std::prev(edges.end());
        }
        if (totalSamples >= limits.max_reported_edge_samples) {
            continue;
        }
        const auto a = toFramebuffer(source.a[0], source.a[1], render.effectiveRule,
                                     scenario.width, scenario.height);
        const auto b = toFramebuffer(source.b[0], source.b[1], render.effectiveRule,
                                     scenario.width, scenario.height);
        const double midX = (a[0] + b[0]) * 0.5;
        const double midY = (a[1] + b[1]) * 0.5;
        const int centerX = static_cast<int>(std::lround(midX));
        const int centerY = static_cast<int>(std::lround(midY));
        const std::array<std::pair<const char*, std::pair<int, int>>, 5> probes{{
            {"midpoint", {centerX, centerY}},
            {"midpoint-x-1", {centerX - 1, centerY}},
            {"midpoint-x+1", {centerX + 1, centerY}},
            {"midpoint-y-1", {centerX, centerY - 1}},
            {"midpoint-y+1", {centerX, centerY + 1}},
        }};
        for (const auto& probe : probes) {
            if (totalSamples >= limits.max_reported_edge_samples) {
                break;
            }
            EdgeSampleEvidence sample;
            sample.pass = pass.id;
            sample.probe = probe.first;
            sample.x = probe.second.first;
            sample.y = probe.second.second;
            sample.rgba = samplePixel(render.finalImage, sample.x, sample.y);
            iterator->samples.push_back(std::move(sample));
            ++totalSamples;
        }
    }
}

}  // namespace

RunSummary runRegression(const RunOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    const auto finish = [&](RunSummary summary,
                            RunEvidence& evidence,
                            EvidenceWriter& writer) -> RunSummary {
        const auto ended = std::chrono::steady_clock::now();
        evidence.durationMilliseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(ended - started)
                .count());
        std::string error;
        if (!writer.finish(std::move(evidence), error)) {
            summary.exitCode = 1;
            summary.status = "evidence_error";
        }
        return summary;
    };

    RunEvidence evidence;
    evidence.generatedAt = utcNow();
    evidence.inputPath = options.inputPath.string();
    evidence.limits = options.limits;
    evidence.platform = initializePlatform();
    evidence.scenarioName = "unloaded";

    EvidenceWriter writer(options.outputDirectory);
    std::string error;
    if (!writer.initialize(error)) {
        return {1, "evidence_error", options.outputDirectory.string(), 0};
    }

    std::string text;
    std::uint64_t inputBytes = 0;
    bool tooLarge = false;
    if (!readInputFile(options.inputPath, options.limits.max_input_bytes, text,
                       inputBytes, tooLarge, error)) {
        evidence.status = "input_error";
        evidence.errors.push_back({"input", error});
        return finish({1, evidence.status, options.outputDirectory.string(), 0},
                      evidence, writer);
    }
    evidence.inputBytes = inputBytes;
    if (tooLarge) {
        evidence.status = "input_rejected";
        evidence.inputSha256 = oversizedPrefixHash(options.inputPath);
        evidence.inputSha256Scope = "first_64KiB";
        evidence.errors.push_back(
            {"input", "input size exceeds max_input_bytes; no rendering was attempted"});
        std::ostringstream marker;
        marker << "input_too_large\n"
               << "path=" << options.inputPath.string() << "\n"
               << "bytes=" << inputBytes << "\n"
               << "max_input_bytes=" << options.limits.max_input_bytes << "\n"
               << "sha256_prefix=" << evidence.inputSha256 << "\n";
        writer.saveText("input/rejection.txt", marker.str(), nullptr, error);
        return finish({1, evidence.status, options.outputDirectory.string(), 0},
                      evidence, writer);
    }
    evidence.inputSha256 = sha256Hex(text);
    evidence.emptyInput =
        text.empty() || std::all_of(text.begin(), text.end(), [](char c) {
            return c == ' ' || c == '\t' || c == '\n' || c == '\r';
        });
    if (!writer.saveText("input/input.json", text, nullptr, error)) {
        evidence.status = "evidence_error";
        evidence.errors.push_back({"evidence", error});
        return finish({1, evidence.status, options.outputDirectory.string(), 0},
                      evidence, writer);
    }

    LoadResult loaded = loadScenario(text, options.limits);
    evidence.scenarioName = loaded.scenario.name;
    evidence.warnings = loaded.warnings;
    evidence.errors = loaded.errors;
    if (!loaded.ok()) {
        evidence.status = "input_error";
        return finish({1, evidence.status, options.outputDirectory.string(), 0},
                      evidence, writer);
    }

    Scenario scenario = std::move(loaded.scenario);
    evidence.scenarioName = scenario.name;
    evidence.scenarioWidth = scenario.width;
    evidence.scenarioHeight = scenario.height;
    evidence.vertices = scenario.vertices.size();
    evidence.indices = scenario.indices.size();
    evidence.rules = scenario.rules.size();
    evidence.draws = scenario.draws.size();
    evidence.passes = scenario.passes.size();
    evidence.sharedEdges = scenario.shared_edges.size();
    evidence.referencePixels = scenario.reference_pixels.size();
    for (const auto& pass : scenario.passes) {
        evidence.totalDrawSubmissions += pass.draws.size();
    }

    VulkanRenderer renderer;
    bool rendererReady = renderer.initialize(options.render, error);
    if (rendererReady) {
        evidence.device = renderer.deviceInfo();
    } else if (!scenario.passes.empty()) {
        evidence.status = "runtime_error";
        evidence.errors.push_back({"vulkan", error});
        return finish({1, evidence.status, options.outputDirectory.string(), 0},
                      evidence, writer);
    } else {
        evidence.warnings.push_back(
            {"vulkan", "renderer initialization failed for an empty scene: " + error});
    }

    for (const auto& edge : scenario.shared_edges) {
        SharedEdgeEvidence item;
        item.id = edge.id;
        item.drawA = edge.draw_a;
        item.drawB = edge.draw_b;
        item.a = edge.a;
        item.b = edge.b;
        evidence.edgeEvidence.push_back(std::move(item));
    }

    std::optional<Image> boundaryBefore;
    std::optional<Image> boundaryAfter;
    std::optional<Rule> boundaryBeforeRule;
    std::optional<Rule> boundaryAfterRule;
    std::size_t edgeSampleCount = 0;
    std::size_t savedImageBudget = options.limits.max_saved_draw_images;
    std::uint64_t referenceFailures = 0;
    bool renderingFailed = false;

    for (std::size_t passSequence = 0; passSequence < scenario.passes.size();
         ++passSequence) {
        const Pass& pass = scenario.passes[passSequence];
        RenderOptions passOptions = options.render;
        passOptions.savedDrawIndices.clear();
        if (savedImageBudget > 0U && !pass.draws.empty()) {
            passOptions.savedDrawIndices.insert(0);
            --savedImageBudget;
        }
        if (savedImageBudget > 0U && pass.draws.size() > 1U) {
            passOptions.savedDrawIndices.insert(pass.draws.size() - 1U);
            --savedImageBudget;
        }

        RenderPassResult render = renderer.renderPass(scenario, pass, passOptions);
        if (!render.ok) {
            renderingFailed = true;
            evidence.errors.push_back(
                {"passes[" + pass.id + "]", render.error});
            break;
        }
        const std::string passDirectory =
            "passes/" + std::to_string(passSequence) + "-" + safeName(pass.id);
        PassEvidence passEvidence;
        passEvidence.id = pass.id;
        passEvidence.ruleId = pass.rule;
        passEvidence.effectiveRule = render.effectiveRule;
        passEvidence.warnings = render.warnings;
        passEvidence.initialSha256 = render.initialSha256;
        passEvidence.finalSha256 = render.finalSha256;
        passEvidence.elapsedMicroseconds = render.elapsedMicroseconds;
        passEvidence.framebufferWidth = scenario.width;
        passEvidence.framebufferHeight = scenario.height;
        passEvidence.vertexBufferBytes =
            scenario.vertices.size() * sizeof(Vertex);
        passEvidence.indexBufferBytes =
            scenario.indices.size() * sizeof(std::uint32_t);
        passEvidence.drawCount = render.draws.size();
        passEvidence.changedFromClear =
            compareImages(render.initialImage, render.finalImage);

        if (!writer.saveImage(passDirectory + "/frame.ppm", render.finalImage,
                              &passEvidence.framePpm, error) ||
            !writer.savePam(passDirectory + "/frame.pam", render.finalImage,
                            &passEvidence.framePam, error) ||
            !writer.saveDiff(passDirectory + "/change-from-clear.ppm",
                             render.initialImage, render.finalImage,
                             &passEvidence.changeDiffPpm, error)) {
            renderingFailed = true;
            evidence.errors.push_back({"evidence", error});
            break;
        }

        for (const auto& draw : render.draws) {
            DrawEvidence drawEvidence;
            drawEvidence.sequence = draw.sequence;
            drawEvidence.id = draw.id;
            drawEvidence.firstIndex = draw.firstIndex;
            drawEvidence.indexCount = draw.indexCount;
            drawEvidence.instanceCount = draw.instanceCount;
            drawEvidence.beforeSha256 = draw.beforeSha256;
            drawEvidence.afterSha256 = draw.afterSha256;
            drawEvidence.change = draw.change;
            if (draw.hasSavedImage) {
                std::ostringstream relative;
                relative << passDirectory << "/draw-" << draw.sequence
                         << "-after.ppm";
                if (!writer.saveImage(relative.str(), draw.savedImage,
                                      &drawEvidence.savedFramePath, error)) {
                    renderingFailed = true;
                    evidence.errors.push_back({"evidence", error});
                    break;
                }
                drawEvidence.savedFrameSha256 = sha256Hex(draw.savedImage.rgba);
            }
            passEvidence.draws.push_back(std::move(drawEvidence));
        }
        if (renderingFailed) {
            break;
        }

        for (const auto& reference : scenario.reference_pixels) {
            if (!reference.pass.empty() && reference.pass != pass.id) {
                continue;
            }
            ReferenceCheck check =
                checkReference(render.finalImage, reference.x, reference.y,
                               reference.rgba, reference.tolerance);
            check.pass = pass.id;
            ++passEvidence.references.total;
            if (!check.passCheck) {
                ++passEvidence.references.failed;
                ++referenceFailures;
            }
            if (passEvidence.references.reported.size() <
                options.limits.max_reported_reference_pixels) {
                ReferenceEvidence item;
                item.pass = pass.id;
                item.x = reference.x;
                item.y = reference.y;
                item.expected = reference.rgba;
                item.actual = check.actual;
                item.tolerance = reference.tolerance;
                item.maxDelta = check.maxDelta;
                item.passed = check.passCheck;
                passEvidence.references.reported.push_back(std::move(item));
            } else {
                ++passEvidence.references.omitted;
            }
        }

        addEdgeSamples(evidence.edgeEvidence, scenario, pass, render,
                       options.limits, edgeSampleCount);

        if (scenario.boundary &&
            pass.id == scenario.boundary->before_pass) {
            boundaryBefore = render.finalImage;
            boundaryBeforeRule = render.effectiveRule;
        }
        if (scenario.boundary &&
            pass.id == scenario.boundary->after_pass) {
            boundaryAfter = render.finalImage;
            boundaryAfterRule = render.effectiveRule;
        }
        evidence.passEvidence.push_back(std::move(passEvidence));
    }

    if (scenario.boundary) {
        BoundaryEvidence boundary;
        boundary.present = true;
        boundary.parameter = scenario.boundary->parameter;
        boundary.threshold = scenario.boundary->threshold;
        boundary.beforePass = scenario.boundary->before_pass;
        boundary.afterPass = scenario.boundary->after_pass;
        if (boundaryBeforeRule) {
            bool found = false;
            boundary.beforeValue =
                ruleParameter(*boundaryBeforeRule, boundary.parameter, &found);
            if (const Pass* beforePass =
                    findPass(scenario, scenario.boundary->before_pass)) {
                if (const Rule* requested = findRule(scenario, beforePass->rule)) {
                    boundary.beforeRequestedValue =
                        ruleParameter(*requested, boundary.parameter, &found);
                }
            }
            if (!found) {
                boundary.beforeValue = "null";
                evidence.warnings.push_back(
                    {"boundary.parameter",
                     "parameter is not a standard rule field; value is null"});
            }
        }
        if (boundaryAfterRule) {
            bool found = false;
            boundary.afterValue =
                ruleParameter(*boundaryAfterRule, boundary.parameter, &found);
            if (const Pass* afterPass =
                    findPass(scenario, scenario.boundary->after_pass)) {
                if (const Rule* requested = findRule(scenario, afterPass->rule)) {
                    boundary.afterRequestedValue =
                        ruleParameter(*requested, boundary.parameter, &found);
                }
            }
            if (!found) {
                boundary.afterValue = "null";
            }
        }
        if (boundaryBefore && boundaryAfter) {
            if (!writer.saveImage("boundary/before.ppm", *boundaryBefore,
                                  &boundary.beforeImage, error) ||
                !writer.saveImage("boundary/after.ppm", *boundaryAfter,
                                  &boundary.afterImage, error) ||
                !writer.saveDiff("boundary/diff.ppm", *boundaryBefore,
                                 *boundaryAfter, &boundary.diffImage, error)) {
                evidence.errors.push_back({"evidence", error});
                renderingFailed = true;
            } else {
                boundary.diff =
                    compareImages(*boundaryBefore, *boundaryAfter);
                if (scenario.boundary->has_sample_pixel) {
                    boundary.hasSamplePixel = true;
                    boundary.sampleX = scenario.boundary->sample_x;
                    boundary.sampleY = scenario.boundary->sample_y;
                    boundary.beforeRgba =
                        boundaryBefore->pixel(boundary.sampleX, boundary.sampleY);
                    boundary.afterRgba =
                        boundaryAfter->pixel(boundary.sampleX, boundary.sampleY);
                    boundary.sampleMaxDelta =
                        channelDelta(boundary.beforeRgba, boundary.afterRgba);
                }
            }
        } else {
            evidence.warnings.push_back(
                {"boundary",
                 "one or both boundary passes were not rendered; no boundary diff"});
        }
        evidence.boundary = std::move(boundary);
    }

    if (renderingFailed) {
        evidence.status = evidence.passEvidence.empty() ? "runtime_error" : "partial";
    } else if (referenceFailures > 0U) {
        evidence.status = "reference_mismatch";
    } else {
        evidence.status = "ok";
    }

    RunSummary summary;
    summary.status = evidence.status;
    summary.outputDirectory = options.outputDirectory.string();
    summary.referenceFailures = referenceFailures;
    summary.exitCode = evidence.status == "ok"
                           ? 0
                           : (evidence.status == "reference_mismatch" ? 2 : 1);
    return finish(std::move(summary), evidence, writer);
}

}  // namespace rr


