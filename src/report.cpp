#include "report.hpp"

#include <fstream>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;

namespace {

std::string escapeJson(const std::string& text) {
  std::string result;
  result.reserve(text.size() + 8);
  for (char value : text) {
    switch (value) {
      case '\\': result += "\\\\"; break;
      case '"': result += "\\\""; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += value; break;
    }
  }
  return result;
}

void writeMetrics(std::ostream& out, const RunEvidence& evidence,
                  const std::string& indent) {
  const CompareResult& result = evidence.compare;
  out << indent << "\"valid\": " << (evidence.valid ? "true" : "false") << ",\n";
  out << indent << "\"bias_constant\": " << evidence.bias.constant << ",\n";
  out << indent << "\"bias_slope\": " << evidence.bias.slope << ",\n";
  out << indent << "\"score\": " << evidence.score << ",\n";
  out << indent << "\"elapsed_ms\": " << evidence.elapsedMs << ",\n";
  out << indent << "\"classification\": \"" << escapeJson(result.classification)
      << "\",\n";
  out << indent << "\"focus_pixels\": " << result.focusPixels << ",\n";
  out << indent << "\"missing_pixels\": " << result.missingPixels << ",\n";
  out << indent << "\"added_pixels\": " << result.addedPixels << ",\n";
  out << indent << "\"changed_pixels\": " << result.changedPixels << ",\n";
  out << indent << "\"mean_abs_depth_delta\": " << result.meanAbsDepthDelta << "\n";
}

void writeBoundaryArray(std::ostream& out, const CompareResult& result,
                        const std::string& indent) {
  out << indent << "\"boundary_samples\": [\n";
  for (std::size_t index = 0; index < result.boundary.size(); ++index) {
    const BoundarySample& sample = result.boundary[index];
    out << indent << "  {\"label\": \"" << escapeJson(sample.label)
        << "\", \"x\": " << sample.point.x << ", \"y\": " << sample.point.y
        << ", \"before_object\": " << sample.before.objectId
        << ", \"before_depth\": " << sample.before.depth
        << ", \"after_object\": " << sample.after.objectId
        << ", \"after_depth\": " << sample.after.depth
        << ", \"depth_delta\": " << sample.depthDelta
        << ", \"object_changed\": "
        << (sample.objectChanged ? "true" : "false")
        << ", \"color_changed\": "
        << (sample.colorChanged ? "true" : "false") << "}";
    if (index + 1 < result.boundary.size()) {
      out << ",";
    }
    out << "\n";
  }
  out << indent << "]\n";
}

}  // namespace

bool writeObjectEvidence(const fs::path& directory, const ObjectRun& run,
                         const DepthParams& preset,
                         const std::vector<CandidateEvaluation>& candidates,
                         const fs::path& baselineImage, std::string& error) {
  std::error_code ec;
  fs::create_directories(directory, ec);
  if (ec) {
    error = "Cannot create evidence directory: " + ec.message();
    return false;
  }

  std::ofstream report(directory / "report.json", std::ios::trunc);
  if (!report) {
    error = "Cannot write report.json";
    return false;
  }
  report << std::fixed << std::setprecision(6);
  report << "{\n";
  report << "  \"tool\": \"screen-depth-recheck\",\n";
  report << "  \"format_version\": 1,\n";
  report << "  \"generated_at_utc\": \"" << utcNow() << "\",\n";
  report << "  \"object\": {\"id\": " << run.id << ", \"name\": \""
         << escapeJson(run.name) << "\"},\n";
  report << "  \"preset\": {\"name\": \"" << escapeJson(preset.name)
         << "\", \"near\": " << preset.nearZ << ", \"far\": " << preset.farZ
         << ", \"depth_bias_constant\": " << preset.depthBiasConstant
         << ", \"depth_bias_slope\": " << preset.depthBiasSlope
         << ", \"depth_clamp\": " << (preset.depthClamp ? "true" : "false")
         << ", \"viewport_min_depth\": " << preset.viewportMinDepth
         << ", \"viewport_max_depth\": " << preset.viewportMaxDepth << "},\n";
  report << "  \"images\": {\"baseline\": \"" << escapeJson(baselineImage.generic_string())
         << "\", \"extreme\": \"extreme.png\", \"corrected\": \"corrected.png\","
         << " \"diff\": \"diff.png\", \"corrected_diff\": \"corrected_diff.png\","
         << " \"compare\": \"compare.png\"},\n";
  report << "  \"extreme\": {\n";
  writeMetrics(report, run.extreme, "    ");
  report << "  },\n";
  report << "  \"corrected\": {\n";
  writeMetrics(report, run.corrected, "    ");
  report << "  },\n";
  report << "  \"extreme_boundary\": {\n";
  writeBoundaryArray(report, run.extreme.compare, "    ");
  report << "  },\n";
  report << "  \"corrected_boundary\": {\n";
  writeBoundaryArray(report, run.corrected.compare, "    ");
  report << "  }\n";
  report << "}\n";
  report.close();
  if (!report) {
    error = "Failed while writing report.json";
    return false;
  }

  std::ofstream boundary(directory / "boundary.csv", std::ios::trunc);
  boundary << "phase,label,x,y,before_valid,before_object,before_depth,"
              "after_valid,after_object,after_depth,depth_delta,"
              "object_changed,color_changed\n";
  const auto writeRows = [&](const char* phase, const CompareResult& compare) {
    for (const BoundarySample& sample : compare.boundary) {
      boundary << phase << "," << sample.label << "," << sample.point.x << ","
               << sample.point.y << "," << sample.before.valid << ","
               << sample.before.objectId << "," << sample.before.depth << ","
               << sample.after.valid << "," << sample.after.objectId << ","
               << sample.after.depth << "," << sample.depthDelta << ","
               << sample.objectChanged << "," << sample.colorChanged << "\n";
    }
  };
  writeRows("extreme", run.extreme.compare);
  writeRows("corrected", run.corrected.compare);
  boundary.close();

  std::ofstream candidateFile(directory / "candidates.csv", std::ios::trunc);
  candidateFile << "bias_constant,bias_slope,missing_pixels,added_pixels,"
                   "changed_pixels,score,selected\n";
  for (const CandidateEvaluation& candidate : candidates) {
    candidateFile << candidate.bias.constant << "," << candidate.bias.slope << ","
                  << candidate.compare.missingPixels << ","
                  << candidate.compare.addedPixels << ","
                  << candidate.compare.changedPixels << ","
                  << candidate.score << "," << (candidate.selected ? 1 : 0) << "\n";
  }
  candidateFile.close();
  if (!boundary || !candidateFile) {
    error = "Failed while writing evidence CSV files";
    return false;
  }
  return true;
}

