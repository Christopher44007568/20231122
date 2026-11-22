#include "format.hpp"
#include "hash.hpp"
#include "image.hpp"
#include "renderer.hpp"
#include "report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace msphere;

struct Options {
  enum class Mode { help, self_test, render, validate };
  Mode mode = Mode::help;
  fs::path input = "materials/original.material";
  fs::path output = "evidence/acceptance";
};

void print_usage() {
  std::cout
      << "msphere_energy - material sphere energy comparison evidence tool\n\n"
      << "Usage:\n"
      << "  msphere_energy --render <input.material> --out <dir>\n"
      << "  msphere_energy --validate <input.material>\n"
      << "  msphere_energy --self-test [--out <dir>]\n\n"
      << "Input format: MSPHERE1, header 64 bytes, material record 32 bytes,\n"
      << "reflection sphere RGBA8, then float32 energy integrals.\n";
}

bool parse_options(int argc, char** argv, Options* options,
                   std::string* error) {
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--help" || argument == "-h") {
      options->mode = Options::Mode::help;
      return true;
    }
    if (argument == "--self-test") {
      options->mode = Options::Mode::self_test;
    } else if (argument == "--render") {
      if (i + 1 >= argc) {
        *error = "--render requires an input path";
        return false;
      }
      options->mode = Options::Mode::render;
      options->input = argv[++i];
    } else if (argument == "--validate") {
      if (i + 1 >= argc) {
        *error = "--validate requires an input path";
        return false;
      }
      options->mode = Options::Mode::validate;
      options->input = argv[++i];
    } else if (argument == "--out") {
      if (i + 1 >= argc) {
        *error = "--out requires a directory path";
        return false;
      }
      options->output = argv[++i];
    } else {
      *error = "unknown argument: " + argument;
      return false;
    }
  }
  return true;
}

bool read_file_bytes(const fs::path& path, std::vector<uint8_t>* bytes,
                     std::string* error) {
  std::ifstream stream(path, std::ios::binary | std::ios::ate);
  if (!stream) {
    *error = "cannot open file for reading: " + path.string();
    return false;
  }
  const std::streamsize size = stream.tellg();
  stream.seekg(0, std::ios::beg);
  bytes->resize(static_cast<size_t>(size));
  stream.read(reinterpret_cast<char*>(bytes->data()), size);
  if (!stream) {
    *error = "cannot read file: " + path.string();
    return false;
  }
  return true;
}

bool write_file_bytes(const fs::path& path, const std::vector<uint8_t>& bytes,
                      std::string* error) {
  std::ofstream stream(path, std::ios::binary);
  if (!stream) {
    *error = "cannot open file for writing: " + path.string();
    return false;
  }
  stream.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  if (!stream) {
    *error = "cannot write file: " + path.string();
    return false;
  }
  return true;
}

bool file_sha256(const fs::path& path, std::string* hash, std::string* error) {
  std::vector<uint8_t> bytes;
  if (!read_file_bytes(path, &bytes, error)) {
    return false;
  }
  *hash = sha256_hex(bytes.data(), bytes.size());
  return true;
}

bool preserve_original(const ParsedInput& input, const fs::path& output,
                       const fs::path& preserved, std::string* sha_before,
                       std::string* sha_after, std::string* error) {
  *sha_before = sha256_hex(input.bytes.data(), input.bytes.size());
  if (!write_file_bytes(preserved, input.bytes, error)) {
    return false;
  }
  std::string copy_hash;
  if (!file_sha256(preserved, &copy_hash, error)) {
    return false;
  }
  if (copy_hash != *sha_before) {
    *error = "preserved copy hash differs from input";
    return false;
  }
  std::string disk_hash;
  if (!file_sha256(input.source_path, &disk_hash, error)) {
    return false;
  }
  *sha_after = disk_hash;
  std::ofstream manifest(output / "original.sha256");
  if (!manifest) {
    *error = "cannot write original.sha256";
    return false;
  }
  manifest << copy_hash << "  " << preserved.filename().string() << "\n";
  return static_cast<bool>(manifest);
}

bool add_artifact(const fs::path& path, const std::string& role,
                  const std::string& layout,
                  std::vector<ArtifactEvidence>* artifacts,
                  std::string* error) {
  std::error_code ec;
  const uint64_t size = fs::file_size(path, ec);
  if (ec) {
    *error = "cannot stat artifact: " + path.string();
    return false;
  }
  std::string hash;
  if (!file_sha256(path, &hash, error)) {
    return false;
  }
  artifacts->push_back({role, path.generic_string(), size, hash, layout});
  return true;
}

struct EnergyEvaluation {
  std::vector<EnergySample> samples;
  uint64_t diff_pixels_inside_target = 0;
  uint64_t diff_pixels_outside_target = 0;
  double target_relative_change = 0.0;
};

bool path_is_within(const fs::path& child, const fs::path& parent) {
  std::error_code ec;
  const fs::path child_abs = fs::weakly_canonical(child, ec);
  if (ec) {
    return false;
  }
  const fs::path parent_abs = fs::weakly_canonical(parent, ec);
  if (ec) {
    return false;
  }
  const fs::path relative = child_abs.lexically_relative(parent_abs);
  return !relative.empty() && *relative.begin() != "..";
}

double pixel_luma(const RgbaImage& image, size_t pixel_index) {
  const double red = image.pixels[pixel_index * 4U + 0U];
  const double green = image.pixels[pixel_index * 4U + 1U];
  const double blue = image.pixels[pixel_index * 4U + 2U];
  return (0.2126 * red + 0.7152 * green + 0.0722 * blue) / 255.0;
}

EnergyEvaluation evaluate_energy(const ParsedInput& input,
                                 const RenderResult& render) {
  EnergyEvaluation evaluation;
  if (render.frames.size() < 2) {
    return evaluation;
  }
  const RgbaImage& baseline = render.frames[0].image;
  const RgbaImage& anomaly = render.frames[1].image;
  for (const InstancePlacement& placement : render.placements) {
    double baseline_sum = 0.0;
    double anomaly_sum = 0.0;
    uint64_t mask_pixels = 0;
    for (uint32_t y = placement.y0; y <= placement.y1 && y < baseline.height;
         ++y) {
      for (uint32_t x = placement.x0; x <= placement.x1 && x < baseline.width;
           ++x) {
        const size_t pixel = static_cast<size_t>(y) * baseline.width + x;
        if (std::max({baseline.pixels[pixel * 4U + 0U],
                      baseline.pixels[pixel * 4U + 1U],
                      baseline.pixels[pixel * 4U + 2U],
                      anomaly.pixels[pixel * 4U + 0U],
                      anomaly.pixels[pixel * 4U + 1U],
                      anomaly.pixels[pixel * 4U + 2U]}) < 8U) {
          continue;
        }
        baseline_sum += pixel_luma(baseline, pixel);
        anomaly_sum += pixel_luma(anomaly, pixel);
        ++mask_pixels;
      }
    }
    EnergySample sample;
    sample.instance_index = placement.index;
    sample.expected_energy = input.energy_integrals[placement.index];
    sample.x0 = placement.x0;
    sample.y0 = placement.y0;
    sample.x1 = placement.x1;
    sample.y1 = placement.y1;
    if (mask_pixels != 0U) {
      sample.baseline_mean_luma =
          baseline_sum / static_cast<double>(mask_pixels);
      sample.anomaly_mean_luma =
          anomaly_sum / static_cast<double>(mask_pixels);
      if (sample.baseline_mean_luma > 1e-9) {
        sample.relative_change =
            (sample.anomaly_mean_luma - sample.baseline_mean_luma) /
            sample.baseline_mean_luma;
      }
    }
    evaluation.samples.push_back(sample);
  }
  const InstancePlacement* target = nullptr;
  for (const InstancePlacement& placement : render.placements) {
    if (placement.index == render.anomaly.instance_index) {
      target = &placement;
      break;
    }
  }
  for (const EnergySample& sample : evaluation.samples) {
    if (sample.instance_index == render.anomaly.instance_index) {
      evaluation.target_relative_change = sample.relative_change;
      break;
    }
  }
  if (target == nullptr) {
    return evaluation;
  }
  for (uint32_t y = 0; y < baseline.height; ++y) {
    for (uint32_t x = 0; x < baseline.width; ++x) {
      const size_t pixel = static_cast<size_t>(y) * baseline.width + x;
      bool changed = false;
      for (size_t channel = 0; channel < 4U; ++channel) {
        if (baseline.pixels[pixel * 4U + channel] !=
            anomaly.pixels[pixel * 4U + channel]) {
          changed = true;
          break;
        }
      }
      if (!changed) {
        continue;
      }
      if (x >= target->x0 && x <= target->x1 && y >= target->y0 &&
          y <= target->y1) {
        ++evaluation.diff_pixels_inside_target;
      } else {
        ++evaluation.diff_pixels_outside_target;
      }
    }
  }
  return evaluation;
}

struct RunState {
  uint64_t start_us = monotonic_microseconds();
  std::string started_at_utc = utc_now_iso8601();
  std::vector<PhaseEvent> phases;

  void mark(const std::string& name, const std::string& detail) {
    phases.push_back({name, utc_now_iso8601(),
                      monotonic_microseconds() - start_us, detail});
  }
};

void add_check(std::vector<AcceptanceCheck>* checks, const std::string& id,
               bool passed, const std::string& expected,
               const std::string& actual) {
  checks->push_back({id, passed, expected, actual});
}

InvalidInputEvidence run_invalid_case(const std::string& case_name,
                                      const fs::path& path,
                                      const RunState& run) {
  InvalidInputEvidence evidence;
  evidence.case_name = case_name;
  evidence.input_path = path.generic_string();
  evidence.checked_at_utc = utc_now_iso8601();
  evidence.elapsed_us = monotonic_microseconds() - run.start_us;
  std::error_code ec;
  evidence.file_size = fs::file_size(path, ec);
  if (ec) {
    evidence.file_size = 0;
  }
  evidence.max_bytes = kMaxInputBytes;
  const LoadResult loaded = load_and_validate(path, kMaxInputBytes);
  evidence.error_code = input_error_code_name(loaded.error.code);
  evidence.field = loaded.error.field;
  evidence.detail = loaded.error.detail;
  evidence.raw_offset = loaded.error.raw_offset;
  evidence.observed = loaded.error.observed;
  evidence.limit = loaded.error.limit;
  return evidence;
}

bool write_frame_artifacts(const RenderResult& render, const fs::path& frames_dir,
                           const DiffStats& diff,
                           std::vector<ArtifactEvidence>* artifacts,
                           std::string* error) {
  if (render.frames.size() < 2) {
    *error = "render did not produce two frames";
    return false;
  }
  const fs::path baseline_raw = frames_dir / "baseline.rgba";
  const fs::path anomaly_raw = frames_dir / "anomaly.rgba";
  const fs::path baseline_png = frames_dir / "baseline.png";
  const fs::path anomaly_png = frames_dir / "anomaly.png";
  const fs::path diff_png = frames_dir / "difference.png";
  if (!write_raw_rgba(baseline_raw, render.frames[0].image, error) ||
      !write_raw_rgba(anomaly_raw, render.frames[1].image, error) ||
      !write_png_rgb(baseline_png, render.frames[0].image, error) ||
      !write_png_rgb(anomaly_png, render.frames[1].image, error)) {
    return false;
  }
  const RgbaImage diff_image =
      make_diff_image(render.frames[0].image, render.frames[1].image, 8);
  if (!write_png_rgb(diff_png, diff_image, error)) {
    return false;
  }
  const std::string raw_layout =
      "RGBA8 row-major, width*height*4, first_diff_offset=" +
      std::to_string(diff.first_differing_raw_offset);
  return add_artifact(baseline_raw, "framebuffer.baseline.raw", raw_layout,
                      artifacts, error) &&
         add_artifact(anomaly_raw, "framebuffer.anomaly.raw", raw_layout,
                      artifacts, error) &&
         add_artifact(baseline_png, "framebuffer.baseline.png",
                      "PNG RGB8", artifacts, error) &&
         add_artifact(anomaly_png, "framebuffer.anomaly.png", "PNG RGB8",
                      artifacts, error) &&
         add_artifact(diff_png, "framebuffer.difference.png",
                      "PNG RGB8, channel delta * 8", artifacts, error);
}

int run_self_test(const Options& options) {
  RunState run;
  run.mark("run_start", "self-test process entered");
  if (path_is_within(options.input, options.output)) {
    std::cerr << "input must not be inside the output directory\n";
    return 2;
  }
  std::string error;
  std::error_code ec;
  fs::remove_all(options.output, ec);
  fs::create_directories(options.output / "original", ec);
  fs::create_directories(options.output / "frames", ec);
  fs::create_directories(options.output / "inputs", ec);
  if (ec) {
    std::cerr << "cannot create output directories: " << ec.message() << "\n";
    return 2;
  }

  const LoadResult loaded = load_and_validate(options.input);
  if (loaded.error) {
    std::cerr << "input rejected: " << input_error_code_name(loaded.error.code)
              << " at offset " << loaded.error.raw_offset << ": "
              << loaded.error.detail << "\n";
    return 2;
  }
  run.mark("input_validated",
           "materials=" + std::to_string(loaded.input.material_count) +
               ", bytes=" + std::to_string(loaded.input.file_size));

  const fs::path preserved_path =
      options.output / "original" / "original.material";
  std::string sha_before;
  std::string sha_after_copy;
  if (!preserve_original(loaded.input, options.output, preserved_path,
                         &sha_before, &sha_after_copy, &error)) {
    std::cerr << "preserve failed: " << error << "\n";
    return 2;
  }
  run.mark("original_preserved",
           "byte-for-byte copy at " + preserved_path.generic_string());

  const fs::path empty_path = options.output / "inputs" / "empty.material";
  {
    std::ofstream empty(empty_path, std::ios::binary);
    if (!empty) {
      std::cerr << "cannot create empty fixture\n";
      return 2;
    }
  }
  const InvalidInputEvidence empty_evidence =
      run_invalid_case("empty_input", empty_path, run);
  if (!write_invalid_input_report(options.output / "empty_input.json",
                                  empty_evidence, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  run.mark("empty_input_checked",
           "code=" + empty_evidence.error_code + ", raw_offset=" +
               std::to_string(empty_evidence.raw_offset));

  const fs::path oversized_path =
      options.output / "inputs" / "oversized.material";
  {
    std::ofstream oversized(oversized_path, std::ios::binary);
    if (!oversized) {
      std::cerr << "cannot create oversized fixture\n";
      return 2;
    }
  }
  fs::resize_file(oversized_path, kMaxInputBytes + 1U, ec);
  if (ec) {
    std::cerr << "cannot resize oversized fixture: " << ec.message() << "\n";
    return 2;
  }
  const InvalidInputEvidence oversized_evidence =
      run_invalid_case("oversized_input", oversized_path, run);
  if (!write_invalid_input_report(options.output / "oversized_input.json",
                                  oversized_evidence, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  run.mark("oversized_input_checked",
           "code=" + oversized_evidence.error_code + ", raw_offset=" +
               std::to_string(oversized_evidence.raw_offset));

  RenderOptions render_options;
  run.mark("render_start", "GLFW null platform + Vulkan offscreen");
  const RenderResult render = render_frames(loaded.input, render_options);
  if (!render.ok) {
    std::cerr << "render failed: " << render.error << "\n";
    return 2;
  }
  if (render.frames.size() != 2) {
    std::cerr << "render returned " << render.frames.size() << " frames\n";
    return 2;
  }
  run.mark("baseline_rendered",
           "gpu=" + std::to_string(render.frames[0].draw.gpu.elapsed_ns) +
               " ns");
  run.mark("anomaly_rendered",
           "gpu=" + std::to_string(render.frames[1].draw.gpu.elapsed_ns) +
               " ns");

  const DiffStats diff =
      compare_images(render.frames[0].image, render.frames[1].image);
  const EnergyEvaluation energy = evaluate_energy(loaded.input, render);
  const fs::path frames_dir = options.output / "frames";
  std::vector<ArtifactEvidence> artifacts;
  if (!write_frame_artifacts(render, frames_dir, diff, &artifacts, &error)) {
    std::cerr << "frame evidence failed: " << error << "\n";
    return 2;
  }
  run.mark("framebuffers_written",
           "raw RGBA + PNG + amplified difference");

  if (!add_artifact(preserved_path, "original.unmodified",
                    "byte-for-byte input copy", &artifacts, &error) ||
      !add_artifact(options.output / "original.sha256",
                    "original.sha256", "sha256sum compatible", &artifacts,
                    &error) ||
      !add_artifact(options.output / "empty_input.json",
                    "invalid.empty_input.json", "JSON", &artifacts, &error) ||
      !add_artifact(options.output / "oversized_input.json",
                    "invalid.oversized_input.json", "JSON", &artifacts,
                    &error)) {
    std::cerr << error << "\n";
    return 2;
  }

  std::string final_source_hash;
  if (!file_sha256(options.input, &final_source_hash, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  const bool source_unchanged =
      sha_before == sha_after_copy && sha_before == final_source_hash;

  std::vector<AcceptanceCheck> checks;
  add_check(&checks, "input.valid", true, "valid MSPHERE1 package", "valid");
  add_check(&checks, "original.byte_identical", source_unchanged,
            "source hash before == after", sha_before + " == " +
                                               final_source_hash);
  add_check(&checks, "input.offsets",
            loaded.input.material_offset == kHeaderSize &&
                loaded.input.reflection_offset ==
                    loaded.input.material_offset +
                        static_cast<uint64_t>(loaded.input.material_count) *
                            kMaterialSize &&
                loaded.input.energy_offset ==
                    loaded.input.reflection_offset +
                        static_cast<uint64_t>(loaded.input.reflection_width) *
                            loaded.input.reflection_height * 4U &&
                loaded.input.file_size ==
                    loaded.input.energy_offset +
                        static_cast<uint64_t>(loaded.input.material_count) *
                            kEnergySize,
            "material=64, reflection=mat_end, energy=reflection_end, "
            "file_size=energy_end",
            "material=" + std::to_string(loaded.input.material_offset) +
                ", reflection=" +
                std::to_string(loaded.input.reflection_offset) +
                ", energy=" + std::to_string(loaded.input.energy_offset) +
                ", file_size=" + std::to_string(loaded.input.file_size));
  add_check(&checks, "draw_calls.repeated_object",
            render.frames[0].draw.instance_count ==
                    loaded.input.material_count &&
                render.frames[1].draw.instance_count ==
                    loaded.input.material_count &&
                render.frames[0].draw.first_instance == 0 &&
                render.frames[1].draw.first_instance == 0,
            "two instanced draw calls, first_instance=0, instance_count=" +
                std::to_string(loaded.input.material_count),
            "baseline=" +
                std::to_string(render.frames[0].draw.instance_count) +
                ", anomaly=" +
                std::to_string(render.frames[1].draw.instance_count));
  add_check(&checks, "gpu.timestamps",
            render.frames[0].draw.gpu.valid &&
                render.frames[1].draw.gpu.valid &&
                render.frames[0].draw.gpu.elapsed_ns > 0.0 &&
                render.frames[1].draw.gpu.elapsed_ns > 0.0,
            "both frames have non-zero timestamp query results",
            "baseline=" +
                std::to_string(render.frames[0].draw.gpu.elapsed_ns) +
                " ns, anomaly=" +
                std::to_string(render.frames[1].draw.gpu.elapsed_ns) + " ns");
  add_check(&checks, "framebuffer.extent",
            render.frames[0].image.valid() && render.frames[1].image.valid() &&
                render.frames[0].image.width == render_options.width &&
                render.frames[0].image.height == render_options.height,
            "960x540 RGBA8 readback", "960x540 RGBA8 readback");
  add_check(&checks, "image.difference",
            diff.differing_pixels > 1000U,
            ">1000 differing pixels",
            std::to_string(diff.differing_pixels) + " pixels");
  add_check(&checks, "image.difference.localized",
            energy.diff_pixels_outside_target == 0U &&
                energy.diff_pixels_inside_target > 0U,
            "all differences inside anomaly instance screen cell",
            "inside=" + std::to_string(energy.diff_pixels_inside_target) +
                ", outside=" +
                std::to_string(energy.diff_pixels_outside_target));
  add_check(&checks, "energy.target_change",
            std::abs(energy.target_relative_change) >= 0.05,
            "|relative change| >= 0.05",
            std::to_string(energy.target_relative_change));
  add_check(&checks, "empty_input.rejected",
            empty_evidence.error_code == "empty" &&
                empty_evidence.raw_offset == 0U,
            "empty, raw_offset=0", empty_evidence.error_code + ", raw_offset=" +
                                       std::to_string(empty_evidence.raw_offset));
  add_check(&checks, "oversized_input.rejected",
            oversized_evidence.error_code == "too_large" &&
                oversized_evidence.raw_offset == kMaxInputBytes &&
                oversized_evidence.observed == kMaxInputBytes + 1U,
            "too_large at first rejected byte 8388608",
            oversized_evidence.error_code + ", raw_offset=" +
                std::to_string(oversized_evidence.raw_offset) + ", observed=" +
                std::to_string(oversized_evidence.observed));
  add_check(&checks, "resource.reflection_layout",
            !render.images.empty() &&
                render.images.back().layout_after ==
                    "SHADER_READ_ONLY_OPTIMAL",
            "reflection sphere uploaded to SHADER_READ_ONLY_OPTIMAL",
            render.images.empty() ? "missing" : render.images.back().layout_after);
  run.mark("checks_evaluated",
           std::to_string(std::count_if(
                              checks.begin(), checks.end(),
                              [](const AcceptanceCheck& check) {
                                return check.passed;
                              })) +
               "/" + std::to_string(checks.size()) + " passed");

  EvidenceReport report;
  report.title = "材质球能量对照：重复对象异常证据";
  report.run_started_utc = run.started_at_utc;
  report.run_finished_utc = utc_now_iso8601();
  report.run_elapsed_us = monotonic_microseconds() - run.start_us;
  report.source_path = options.input.generic_string();
  report.preserved_path = preserved_path.generic_string();
  report.source_sha256_before = sha_before;
  report.source_sha256_after = final_source_hash;
  report.source_unchanged = source_unchanged;
  report.input = &loaded.input;
  report.render = &render;
  report.diff = diff;
  report.phases = run.phases;
  report.checks = checks;
  report.artifacts = artifacts;
  report.invalid_inputs = {empty_evidence, oversized_evidence};
  report.energy_samples = energy.samples;

  const fs::path report_path = options.output / "report.json";
  const fs::path acceptance_path = options.output / "acceptance.md";
  if (!write_evidence_report(report_path, report, &error) ||
      !write_acceptance_markdown(acceptance_path, report, &error)) {
    std::cerr << "report write failed: " << error << "\n";
    return 2;
  }
  run.mark("report_written", report_path.generic_string());
  run.mark("acceptance_written", acceptance_path.generic_string());
  run.mark("run_end", "all evidence captured and serialized");
  report.phases = run.phases;
  report.run_finished_utc = utc_now_iso8601();
  report.run_elapsed_us = monotonic_microseconds() - run.start_us;
  if (!write_evidence_report(report_path, report, &error) ||
      !write_acceptance_markdown(acceptance_path, report, &error)) {
    std::cerr << "final report write failed: " << error << "\n";
    return 2;
  }

  const bool all_passed =
      std::all_of(checks.begin(), checks.end(),
                  [](const AcceptanceCheck& check) { return check.passed; });
  std::cout << "self-test " << (all_passed ? "PASS" : "FAIL") << "\n"
            << "report: " << report_path.generic_string() << "\n"
            << "acceptance: " << acceptance_path.generic_string() << "\n"
            << "diff_pixels: " << diff.differing_pixels << "\n"
            << "first_diff_raw_offset: " << diff.first_differing_raw_offset
            << "\n"
            << "anomaly_file_offset: "
            << render.anomaly.source_file_offset << "\n"
            << "gpu_ns: " << render.frames[0].draw.gpu.elapsed_ns << ", "
            << render.frames[1].draw.gpu.elapsed_ns << "\n";
  return all_passed ? 0 : 3;
}

int run_render(const Options& options) {
  RunState run;
  run.mark("run_start", "render command entered");
  if (path_is_within(options.input, options.output)) {
    std::cerr << "input must not be inside the output directory\n";
    return 2;
  }
  std::string error;
  std::error_code ec;
  fs::create_directories(options.output / "original", ec);
  fs::create_directories(options.output / "frames", ec);
  if (ec) {
    std::cerr << "cannot create output directories: " << ec.message() << "\n";
    return 2;
  }
  const LoadResult loaded = load_and_validate(options.input);
  if (loaded.error) {
    std::cerr << "input rejected: " << input_error_code_name(loaded.error.code)
              << " at offset " << loaded.error.raw_offset << ": "
              << loaded.error.detail << "\n";
    return 2;
  }
  run.mark("input_validated",
           "materials=" + std::to_string(loaded.input.material_count));
  const fs::path preserved_path =
      options.output / "original" / "original.material";
  std::string sha_before;
  std::string copy_hash;
  if (!preserve_original(loaded.input, options.output, preserved_path,
                         &sha_before, &copy_hash, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  run.mark("original_preserved", preserved_path.generic_string());
  RenderOptions render_options;
  const RenderResult render = render_frames(loaded.input, render_options);
  if (!render.ok) {
    std::cerr << "render failed: " << render.error << "\n";
    return 2;
  }
  run.mark("baseline_rendered",
           "gpu=" + std::to_string(render.frames[0].draw.gpu.elapsed_ns) +
               " ns");
  run.mark("anomaly_rendered",
           "gpu=" + std::to_string(render.frames[1].draw.gpu.elapsed_ns) +
               " ns");
  const DiffStats diff =
      compare_images(render.frames[0].image, render.frames[1].image);
  const EnergyEvaluation energy = evaluate_energy(loaded.input, render);
  std::vector<ArtifactEvidence> artifacts;
  if (!write_frame_artifacts(render, options.output / "frames", diff,
                             &artifacts, &error) ||
      !add_artifact(preserved_path, "original.unmodified",
                    "byte-for-byte input copy", &artifacts, &error) ||
      !add_artifact(options.output / "original.sha256",
                    "original.sha256", "sha256sum compatible", &artifacts,
                    &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  run.mark("framebuffers_written", "raw RGBA + PNG + difference");
  std::string final_source_hash;
  if (!file_sha256(options.input, &final_source_hash, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  std::vector<AcceptanceCheck> checks;
  add_check(&checks, "original.byte_identical",
            sha_before == copy_hash && sha_before == final_source_hash,
            "source hash before == preserved copy == source hash after",
            final_source_hash);
  add_check(&checks, "draw_calls.repeated_object",
            render.frames.size() == 2U &&
                render.frames[0].draw.instance_count ==
                    loaded.input.material_count,
            "one instanced draw per frame", "instance_count=" +
                                                std::to_string(render.frames[0]
                                                                   .draw
                                                                   .instance_count));
  add_check(&checks, "gpu.timestamps",
            render.frames[0].draw.gpu.valid && render.frames[1].draw.gpu.valid,
            "timestamp queries available", "recorded");
  add_check(&checks, "image.difference", diff.differing_pixels > 0U,
            "non-zero difference", std::to_string(diff.differing_pixels));
  run.mark("checks_evaluated", "render checks updated");
  EvidenceReport report;
  report.title = "材质球能量对照：离屏渲染证据";
  report.run_started_utc = run.started_at_utc;
  report.run_finished_utc = utc_now_iso8601();
  report.run_elapsed_us = monotonic_microseconds() - run.start_us;
  report.source_path = options.input.generic_string();
  report.preserved_path = preserved_path.generic_string();
  report.source_sha256_before = sha_before;
  report.source_sha256_after = final_source_hash;
  report.source_unchanged = sha_before == final_source_hash;
  report.input = &loaded.input;
  report.render = &render;
  report.diff = diff;
  report.phases = run.phases;
  report.checks = checks;
  report.artifacts = artifacts;
  report.energy_samples = energy.samples;
  const fs::path report_path = options.output / "report.json";
  const fs::path acceptance_path = options.output / "acceptance.md";
  if (!write_evidence_report(report_path, report, &error) ||
      !write_acceptance_markdown(acceptance_path, report, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  run.mark("report_written", report_path.generic_string());
  run.mark("run_end", "render evidence finalized");
  report.phases = run.phases;
  report.run_finished_utc = utc_now_iso8601();
  report.run_elapsed_us = monotonic_microseconds() - run.start_us;
  if (!write_evidence_report(report_path, report, &error) ||
      !write_acceptance_markdown(acceptance_path, report, &error)) {
    std::cerr << error << "\n";
    return 2;
  }
  std::cout << "rendered " << options.input.generic_string() << "\n"
            << "report: " << report_path.generic_string() << "\n"
            << "diff_pixels: " << diff.differing_pixels << "\n";
  return 0;
}

int run_validate(const Options& options) {
  const LoadResult loaded = load_and_validate(options.input);
  if (loaded.error) {
    std::cout << "{\"valid\":false,\"error_code\":\""
              << input_error_code_name(loaded.error.code) << "\",\"field\":\""
              << loaded.error.field << "\",\"raw_offset\":"
              << loaded.error.raw_offset << ",\"observed\":"
              << loaded.error.observed << ",\"limit\":"
              << loaded.error.limit << "}\n";
    return 2;
  }
  std::cout << "{\"valid\":true,\"materials\":"
            << loaded.input.material_count << ",\"reflection_width\":"
            << loaded.input.reflection_width << ",\"reflection_height\":"
            << loaded.input.reflection_height << ",\"file_size\":"
            << loaded.input.file_size << ",\"material_offset\":"
            << loaded.input.material_offset << ",\"reflection_offset\":"
            << loaded.input.reflection_offset << ",\"energy_offset\":"
            << loaded.input.energy_offset << "}\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  Options options;
  std::string error;
  if (!parse_options(argc, argv, &options, &error)) {
    std::cerr << "argument error: " << error << "\n\n";
    print_usage();
    return 2;
  }
  switch (options.mode) {
    case Options::Mode::help:
      print_usage();
      return argc == 1 ? 2 : 0;
    case Options::Mode::self_test:
      return run_self_test(options);
    case Options::Mode::render:
      return run_render(options);
    case Options::Mode::validate:
      return run_validate(options);
  }
  return 2;
}
