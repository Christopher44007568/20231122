#include "format.hpp"
#include "image.hpp"
#include "renderer.hpp"

#include <cstdio>
#include <filesystem>

int main(int argc, char** argv) {
  const std::filesystem::path input =
      argc > 1 ? argv[1] : "materials/original.material";
  msphere::LoadResult loaded = msphere::load_and_validate(input);
  if (loaded.error) {
    std::fprintf(stderr, "load: %s at %llu\n",
                 loaded.error.detail.c_str(),
                 static_cast<unsigned long long>(loaded.error.raw_offset));
    return 1;
  }
  msphere::RenderOptions options;
  msphere::RenderResult result =
      msphere::render_frames(loaded.input, options);
  if (!result.ok) {
    std::fprintf(stderr, "render: %s\n", result.error.c_str());
    return 2;
  }
  std::string error;
  if (!msphere::write_png_rgb("build/smoke-baseline.png",
                              result.frames[0].image, &error) ||
      !msphere::write_png_rgb("build/smoke-anomaly.png",
                              result.frames[1].image, &error)) {
    std::fprintf(stderr, "png: %s\n", error.c_str());
    return 3;
  }
  const msphere::DiffStats diff =
      msphere::compare_images(result.frames[0].image, result.frames[1].image);
  std::printf("device=%s platform=%s\ndiff_pixels=%llu first_offset=%llu "
              "bbox=(%u,%u)-(%u,%u) gpu_ns=%.3f\n",
              result.info.device_name.c_str(),
              result.info.glfw_platform.c_str(),
              static_cast<unsigned long long>(diff.differing_pixels),
              static_cast<unsigned long long>(diff.first_differing_raw_offset),
              diff.bounds.min_x, diff.bounds.min_y, diff.bounds.max_x,
              diff.bounds.max_y, result.frames[0].draw.gpu.elapsed_ns);
  return diff.differing_pixels == 0 ? 4 : 0;
}
