#include "evidence.h"
#include "json.h"
#include "util.h"
#include <cmath>
#include <fstream>
#include <sstream>

namespace tcpv {

bool EventLog::open(const std::string& path) {
  path_ = path;
  seq_ = 0;
  return writeFile(path, "");
}

void EventLog::close() {}

void EventLog::emit(const std::string& line) {
  std::ofstream f(path_, std::ios::app);
  f << line << "\n";
  seq_++;
}

static std::string base(uint64_t seq, const char* type) {
  return std::string("{\"seq\":") + std::to_string(seq) + ",\"ts\":\"" +
         nowIso8601() + "\",\"type\":\"" + type + "\"";
}

void EventLog::lifecycle(const std::string& phase, const std::string& detail) {
  emit(base(seq_, "lifecycle") + ",\"phase\":\"" + jsonEscape(phase) +
       "\",\"detail\":\"" + jsonEscape(detail) + "\"}");
}

void EventLog::resourceState(const std::string& resourceId,
                             const std::string& kind, const std::string& from,
                             const std::string& to,
                             const std::string& drawCallId) {
  emit(base(seq_, "resource_state") + ",\"resource\":\"" +
       jsonEscape(resourceId) + "\",\"kind\":\"" + jsonEscape(kind) +
       "\",\"from\":\"" + jsonEscape(from) + "\",\"to\":\"" + jsonEscape(to) +
       "\",\"drawCall\":\"" + jsonEscape(drawCallId) + "\"}");
}

void EventLog::framebuffer(const std::string& framebufferId,
                           const std::string& format, uint32_t w, uint32_t h,
                           const std::string& drawCallId) {
  emit(base(seq_, "framebuffer") + ",\"framebuffer\":\"" +
       jsonEscape(framebufferId) + "\",\"format\":\"" + jsonEscape(format) +
       "\",\"width\":" + std::to_string(w) +
       ",\"height\":" + std::to_string(h) + ",\"drawCall\":\"" +
       jsonEscape(drawCallId) + "\"}");
}

void EventLog::drawCall(const std::string& drawCallId,
                        const std::string& resourceId,
                        const std::string& framebufferId, uint32_t vertexCount,
                        const std::string& pipeline) {
  emit(base(seq_, "draw_call") + ",\"drawCall\":\"" + jsonEscape(drawCallId) +
       "\",\"resource\":\"" + jsonEscape(resourceId) +
       "\",\"framebuffer\":\"" + jsonEscape(framebufferId) +
       "\",\"vertexCount\":" + std::to_string(vertexCount) +
       ",\"pipeline\":\"" + jsonEscape(pipeline) + "\"}");
}

void EventLog::capture(const std::string& drawCallId, const std::string& pngPath,
                       const std::string& sha256) {
  emit(base(seq_, "capture") + ",\"drawCall\":\"" + jsonEscape(drawCallId) +
       "\",\"png\":\"" + jsonEscape(pngPath) + "\",\"sha256\":\"" + sha256 +
       "\"}");
}

void EventLog::metric(const std::string& drawCallId, const std::string& name,
                      double value) {
  emit(base(seq_, "metric") + ",\"drawCall\":\"" + jsonEscape(drawCallId) +
       "\",\"name\":\"" + jsonEscape(name) +
       "\",\"value\":" + formatDouble(value, 4) + "}");
}

void EventLog::cancellation(const std::string& atStep, const std::string& reason) {
  emit(base(seq_, "cancellation") + ",\"atStep\":\"" + jsonEscape(atStep) +
       "\",\"reason\":\"" + jsonEscape(reason) + "\"}");
}

void EventLog::error(const std::string& where, const std::string& message) {
  emit(base(seq_, "error") + ",\"where\":\"" + jsonEscape(where) +
       "\",\"message\":\"" + jsonEscape(message) + "\"}");
}

// ---- manifest -------------------------------------------------------------

bool writeManifest(const std::string& path, const RunManifest& m) {
  std::ostringstream o;
  o << "{\n  \"schema\": " << m.schema << ",\n";
  o << "  \"runId\": \"" << jsonEscape(m.runId) << "\",\n";
  o << "  \"startedAt\": \"" << m.startedAt << "\",\n";
  o << "  \"finishedAt\": \"" << m.finishedAt << "\",\n";
  o << "  \"status\": \"" << m.status << "\",\n";
  o << "  \"device\": \"" << jsonEscape(m.device) << "\",\n";
  o << "  \"texture\": \"" << jsonEscape(m.texture) << "\",\n";
  o << "  \"width\": " << m.width << ", \"height\": " << m.height << ",\n";
  if (!m.cancelReason.empty())
    o << "  \"cancelReason\": \"" << jsonEscape(m.cancelReason) << "\",\n";
  auto strList = [&](const char* key, const std::vector<std::string>& v) {
    o << "  \"" << key << "\": [";
    for (size_t i = 0; i < v.size(); i++)
      o << (i ? ", " : "") << "\"" << jsonEscape(v[i]) << "\"";
    o << "],\n";
  };
  strList("completedSteps", m.completedSteps);
  strList("skippedSteps", m.skippedSteps);
  o << "  \"variants\": [\n";
  for (size_t i = 0; i < m.variants.size(); i++) {
    const VariantMetrics& v = m.variants[i];
    o << "    {\"codec\": \"" << v.codec << "\", \"generation\": \""
      << v.generation << "\", \"drawCall\": \"" << v.drawCallId
      << "\", \"psnr\": " << formatDouble(v.psnr, 3)
      << ", \"meanAbsError\": " << formatDouble(v.meanAbsError, 4)
      << ", \"maxChannelError\": " << v.maxChannelError
      << ", \"pass\": " << (v.pass ? "true" : "false")
      << ", \"refPng\": \"" << v.refPng << "\", \"decPng\": \"" << v.decPng
      << "\", \"diffPng\": \"" << v.diffPng << "\"}"
      << (i + 1 < m.variants.size() ? ",\n" : "\n");
  }
  o << "  ],\n";
  o << "  \"artifacts\": {\n";
  size_t i = 0;
  for (const auto& kv : m.artifacts) {
    o << "    \"" << jsonEscape(kv.first) << "\": \"" << kv.second << "\""
      << (++i < m.artifacts.size() ? ",\n" : "\n");
  }
  o << "  }\n}\n";
  return writeFile(path, o.str());
}

bool readManifest(const std::string& path, RunManifest& m, std::string& err) {
  Json root;
  if (!parseJson(readFile(path), root, err)) return false;
  int schema = int(root.getNum("schema", 1));
  m.schema = schema;
  if (schema == 1) {
    // Legacy v1 layout: flat fields, "results" instead of "variants".
    m.runId = root.getStr("run_id");
    m.startedAt = root.getStr("started");
    m.finishedAt = root.getStr("finished");
    m.status = root.getStr("status", "COMPLETED");
    m.device = root.getStr("gpu");
    m.texture = root.getStr("texture");
    m.width = uint32_t(root.getNum("w"));
    m.height = uint32_t(root.getNum("h"));
    if (const Json* res = root.find("results")) {
      for (const Json& r : res->arr) {
        VariantMetrics v;
        v.codec = r.getStr("codec");
        v.generation = r.getStr("gen", "legacy");
        v.psnr = r.getNum("psnr");
        v.pass = r.getNum("psnr") >= 30.0;
        m.variants.push_back(v);
      }
    }
    return true;
  }
  m.runId = root.getStr("runId");
  m.startedAt = root.getStr("startedAt");
  m.finishedAt = root.getStr("finishedAt");
  m.status = root.getStr("status");
  m.device = root.getStr("device");
  m.texture = root.getStr("texture");
  m.width = uint32_t(root.getNum("width"));
  m.height = uint32_t(root.getNum("height"));
  m.cancelReason = root.getStr("cancelReason");
  auto readList = [&](const char* key, std::vector<std::string>& out) {
    if (const Json* v = root.find(key))
      for (const Json& s : v->arr) out.push_back(s.str);
  };
  readList("completedSteps", m.completedSteps);
  readList("skippedSteps", m.skippedSteps);
  if (const Json* vs = root.find("variants")) {
    for (const Json& r : vs->arr) {
      VariantMetrics v;
      v.codec = r.getStr("codec");
      v.generation = r.getStr("generation");
      v.drawCallId = r.getStr("drawCall");
      v.psnr = r.getNum("psnr");
      v.meanAbsError = r.getNum("meanAbsError");
      v.maxChannelError = uint32_t(r.getNum("maxChannelError"));
      if (const Json* p = r.find("pass")) v.pass = p->boolean;
      v.refPng = r.getStr("refPng");
      v.decPng = r.getStr("decPng");
      v.diffPng = r.getStr("diffPng");
      m.variants.push_back(v);
    }
  }
  if (const Json* arts = root.find("artifacts"))
    for (const auto& kv : arts->obj) m.artifacts[kv.first] = kv.second.str;
  return true;
}

// ---- pixel metrics ----------------------------------------------------------

static bool sameSize(const Image& a, const Image& b) {
  return a.width == b.width && a.height == b.height && a.width > 0;
}

double computePsnr(const Image& a, const Image& b) {
  if (!sameSize(a, b)) return 0.0;
  double se = 0.0;
  size_t n = a.rgba.size();
  for (size_t i = 0; i < n; i++) {
    double d = double(a.rgba[i]) - double(b.rgba[i]);
    se += d * d;
  }
  if (se == 0.0) return 99.0;
  double mse = se / double(n);
  return 10.0 * std::log10(255.0 * 255.0 / mse);
}

double computeMeanAbsError(const Image& a, const Image& b) {
  if (!sameSize(a, b)) return 255.0;
  double sum = 0.0;
  for (size_t i = 0; i < a.rgba.size(); i++)
    sum += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
  return sum / double(a.rgba.size());
}

uint32_t computeMaxChannelError(const Image& a, const Image& b) {
  if (!sameSize(a, b)) return 255;
  uint32_t mx = 0;
  for (size_t i = 0; i < a.rgba.size(); i++) {
    uint32_t d = uint32_t(std::abs(int(a.rgba[i]) - int(b.rgba[i])));
    if (d > mx) mx = d;
  }
  return mx;
}

Image makeDiffHeatmap(const Image& a, const Image& b) {
  Image out(a.width, a.height);
  if (!sameSize(a, b)) return out;
  for (uint32_t y = 0; y < a.height; y++)
    for (uint32_t x = 0; x < a.width; x++) {
      const uint8_t* pa = a.px(x, y);
      const uint8_t* pb = b.px(x, y);
      int d = 0;
      for (int c = 0; c < 4; c++)
        d = std::max(d, std::abs(int(pa[c]) - int(pb[c])));
      uint8_t* o = out.px(x, y);
      o[0] = uint8_t(std::min(255, d * 4)); // red heat
      o[1] = uint8_t(std::max(0, 255 - d * 4));
      o[2] = 0;
      o[3] = 255;
    }
  return out;
}

} // namespace tcpv
