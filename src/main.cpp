// rpsc — render pipeline state cache: record / recover / replay / review.
#include "image.h"
#include "recover.h"
#include "renderer.h"
#include "report.h"
#include "scene.h"
#include "trace.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using namespace rpsc;
namespace fs = std::filesystem;

static std::string argValue(int argc, char** argv, const char* key,
                            const char* fallback = "") {
  for (int i = 1; i + 1 < argc; ++i)
    if (std::strcmp(argv[i], key) == 0) return argv[i + 1];
  return fallback;
}

static bool ensureDir(const std::string& path) {
  std::error_code ec;
  fs::create_directories(path, ec);
  return !ec;
}

static std::vector<uint8_t> readFile(const std::string& path, bool* ok) {
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) {
    *ok = false;
    return {};
  }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> b(static_cast<size_t>(n));
  *ok = std::fread(b.data(), 1, b.size(), f) == b.size();
  std::fclose(f);
  return *ok ? b : std::vector<uint8_t>{};
}

static bool writeFile(const std::string& path, const std::vector<uint8_t>& b) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(b.data(), 1, b.size(), f) == b.size();
  std::fclose(f);
  return ok;
}

// Executes the scene, then (v2 only) injects a Checkpoint after every Draw
// carrying the hashes just observed — the trace becomes self-verifying.
static int cmdRecord(int argc, char** argv) {
  std::string out = argValue(argc, argv, "--out");
  std::string format = argValue(argc, argv, "--format", "v2");
  std::string goldenDir = argValue(argc, argv, "--golden");
  if (out.empty()) {
    std::fprintf(stderr, "record: --out required\n");
    return 2;
  }
  ensureDir(fs::path(out).parent_path().string());
  if (!goldenDir.empty()) ensureDir(goldenDir);

  Renderer renderer;
  std::string err;
  if (!renderer.init(&err)) {
    std::fprintf(stderr, "renderer init: %s\n", err.c_str());
    return 1;
  }
  Trace t;
  t.records = buildScene();
  ReplayResult rr;
  if (!renderer.execute(t, goldenDir, &rr, &err)) {
    std::fprintf(stderr, "execute: %s\n", err.c_str());
    return 1;
  }
  if (format == "v1") {
    t.version = kVersionLegacy;
  } else {
    t.version = kVersionCurrent;
    // Inject checkpoints: snapshot seq equals record index here.
    std::vector<Record> withCp;
    for (size_t i = 0; i < t.records.size(); ++i) {
      withCp.push_back(t.records[i]);
      if (t.records[i].type == OpType::Draw && i < rr.ops.size() &&
          rr.ops[i].hasFbHash) {
        Record cp;
        cp.type = OpType::Checkpoint;
        cp.setU64(0, rr.ops[i].fbHash);
        cp.setU64(1, rr.ops[i].stateHash);
        withCp.push_back(cp);
      }
    }
    t.records = withCp;
  }
  if (!writeTrace(out, t, &err)) {
    std::fprintf(stderr, "write: %s\n", err.c_str());
    return 1;
  }
  if (!goldenDir.empty()) {
    if (!writeStateStreamJson(goldenDir + "/state_stream.json", rr, &err)) {
      std::fprintf(stderr, "golden stream: %s\n", err.c_str());
      return 1;
    }
  }
  std::printf("recorded %zu records (%s) -> %s\n", t.records.size(),
              format.c_str(), out.c_str());
  std::printf("cache: %u hits, %u misses; draws: %u\n", rr.cacheHits,
              rr.cacheMisses, rr.drawsExecuted);
  return 0;
}

static int cmdConvert(int argc, char** argv) {
  std::string in = argValue(argc, argv, "--in");
  std::string out = argValue(argc, argv, "--out");
  std::string err;
  Trace t;
  if (!readTraceStrict(in, &t, &err)) {
    std::fprintf(stderr, "convert: %s\n", err.c_str());
    return 1;
  }
  if (t.version != kVersionLegacy) {
    std::fprintf(stderr, "convert: input is not v1\n");
    return 1;
  }
  Trace v2 = convertV1toV2(t);
  if (!writeTrace(out, v2, &err)) {
    std::fprintf(stderr, "convert: %s\n", err.c_str());
    return 1;
  }
  std::printf("converted v1 -> v2: %zu records -> %s\n", v2.records.size(),
              out.c_str());
  return 0;
}

// Cuts the file in the middle of record K's payload, producing a trace
// whose last record is truncated — the defect the recovery pass must find.
static int cmdTruncate(int argc, char** argv) {
  std::string in = argValue(argc, argv, "--in");
  std::string out = argValue(argc, argv, "--out");
  long k = std::atol(argValue(argc, argv, "--at-record", "0").c_str());
  bool ok = false;
  std::vector<uint8_t> b = readFile(in, &ok);
  if (!ok || b.size() < kHeaderSize) {
    std::fprintf(stderr, "truncate: cannot read %s\n", in.c_str());
    return 1;
  }
  uint32_t version = uint32_t(b[4]) | (uint32_t(b[5]) << 8);
  size_t pos = kHeaderSize;
  size_t cut = 0;
  for (long i = 0; i <= k; ++i) {
    if (pos >= b.size()) {
      std::fprintf(stderr, "truncate: record %ld not present\n", k);
      return 1;
    }
    size_t recSize;
    if (version == kVersionLegacy) {
      recSize = kV1RecordSize;
    } else {
      uint16_t len = uint16_t(b[pos + 2]) | (uint16_t(b[pos + 3]) << 8);
      recSize = kV2HeaderSize + len + 4;
    }
    if (i == k) {
      // Mid-payload cut: keep the record header and half the payload.
      size_t headerPart = version == kVersionLegacy ? 4 : kV2HeaderSize;
      size_t payload = recSize - headerPart - (version == kVersionLegacy ? 0 : 4);
      cut = pos + headerPart + payload / 2;
    }
    pos += recSize;
  }
  b.resize(cut);
  if (!writeFile(out, b)) {
    std::fprintf(stderr, "truncate: cannot write %s\n", out.c_str());
    return 1;
  }
  std::printf("truncated %s at record %ld -> %s (%zu bytes, cut at %zu)\n",
              in.c_str(), k, out.c_str(), b.size(), cut);
  return 0;
}

static int cmdRecover(int argc, char** argv) {
  std::string in = argValue(argc, argv, "--in");
  std::string out = argValue(argc, argv, "--out");
  std::string reportPath = argValue(argc, argv, "--report");
  RecoveryReport rep;
  std::string err;
  if (!recoverTrace(in, out, &rep, &err)) {
    std::fprintf(stderr, "recover: %s\n", err.c_str());
    return 1;
  }
  std::printf("recover: v%u input, %zu/%zu records salvaged, defect: %s\n",
              rep.detectedVersion, rep.recordsRecovered,
              rep.recordsDeclared ? rep.recordsDeclared : rep.recordsRecovered,
              rep.clean ? "none (clean)" : rep.firstDefect.reason.c_str());
  if (!reportPath.empty()) {
    FILE* f = std::fopen(reportPath.c_str(), "w");
    if (f) {
      std::fprintf(f,
                   "{\n  \"input\":\"%s\",\n  \"output\":\"%s\",\n"
                   "  \"detectedVersion\":%u,\n  \"fileSize\":%zu,\n"
                   "  \"recordsDeclared\":%zu,\n  \"recordsRecovered\":%zu,\n"
                   "  \"bytesConsumed\":%zu,\n  \"convertedToV2\":%s,\n"
                   "  \"clean\":%s,\n",
                   rep.inputPath.c_str(), rep.outputPath.c_str(),
                   rep.detectedVersion, rep.fileSize, rep.recordsDeclared,
                   rep.recordsRecovered, rep.bytesConsumed,
                   rep.convertedToV2 ? "true" : "false",
                   rep.clean ? "true" : "false");
      if (rep.firstDefect.found) {
        const Defect& d = rep.firstDefect;
        std::fprintf(f,
                     "  \"firstDefect\":{\n    \"offset\":%zu,\n"
                     "    \"recordIndex\":%zu,\n    \"partialType\":%d,\n"
                     "    \"partialTypeName\":\"%s\",\n"
                     "    \"bytesPresent\":%zu,\n    \"bytesExpected\":%zu,\n"
                     "    \"reason\":\"%s\"\n  }\n",
                     d.offset, d.recordIndex, d.partialType,
                     d.partialType >= 1 && d.partialType <= 8
                         ? opName(OpType(d.partialType))
                         : "?",
                     d.bytesPresent, d.bytesExpected, d.reason.c_str());
      } else {
        std::fprintf(f, "  \"firstDefect\":null\n");
      }
      std::fprintf(f, "}\n");
      std::fclose(f);
    }
  }
  return 0;
}

struct ReplayOutputs {
  ReplayResult rr;
  Divergence div;
  std::vector<std::string> evidence;
};

// Replays a trace and, when a golden directory is given, compares the
// resulting state stream against the golden one, emitting per-draw diff
// images for draws whose framebuffer hash diverged.
static int runReplay(const std::string& tracePath, const std::string& goldenDir,
                     const std::string& framesDir, const std::string& diffDir,
                     const std::string& reportPath, Renderer* renderer,
                     ReplayOutputs* out) {
  std::string err;
  Trace t;
  if (!readTraceStrict(tracePath, &t, &err)) {
    std::fprintf(stderr, "replay: %s\n", err.c_str());
    return 1;
  }
  if (!framesDir.empty()) ensureDir(framesDir);
  if (!renderer->execute(t, framesDir, &out->rr, &err)) {
    std::fprintf(stderr, "replay execute: %s\n", err.c_str());
    return 1;
  }
  if (goldenDir.empty()) return 0;

  ReplayResult golden;
  if (!readStateStreamJson(goldenDir + "/state_stream.json", &golden, &err)) {
    std::fprintf(stderr, "replay: golden: %s\n", err.c_str());
    return 1;
  }
  out->div = findFirstDivergence(golden, out->rr);

  // Correlate framebuffer differences with draw calls: diff every draw
  // whose framebuffer hash differs from golden. Draws are matched by
  // ordinal, so Checkpoint records (v2-only) cannot skew alignment.
  if (!diffDir.empty()) ensureDir(diffDir);
  std::vector<const OpSnapshot*> gDraws, aDraws;
  for (const OpSnapshot& s : golden.ops)
    if (s.type == OpType::Draw) gDraws.push_back(&s);
  for (const OpSnapshot& s : out->rr.ops)
    if (s.type == OpType::Draw) aDraws.push_back(&s);
  size_t nd = gDraws.size() < aDraws.size() ? gDraws.size() : aDraws.size();
  for (size_t k = 0; k < nd; ++k) {
    bool hashDiff = gDraws[k]->hasFbHash && aDraws[k]->hasFbHash &&
                    gDraws[k]->fbHash != aDraws[k]->fbHash;
    if (!hashDiff || diffDir.empty()) continue;
    Image g, a, dimg;
    char name[64];
    std::snprintf(name, sizeof(name), "/draw_%02zu.ppm", k);
    std::string ioErr;
    if (loadPpm(goldenDir + name, &g, &ioErr) &&
        loadPpm(framesDir + name, &a, &ioErr)) {
      DiffStats ds = diffImages(g, a, &dimg);
      char dname[64];
      std::snprintf(dname, sizeof(dname), "/draw_%02zu_diff.ppm", k);
      savePpm(diffDir + dname, dimg, &ioErr);
      out->evidence.push_back(diffDir + dname);
      std::printf("draw %02zu: fb differs, %llu px, max delta %u -> %s\n", k,
                  (unsigned long long)ds.diffPixels, ds.maxDelta,
                  (diffDir + dname).c_str());
    }
  }
  // Missing tail: when the actual stream rendered fewer draws than golden
  // (e.g. records lost to truncation), diff the final frames so the
  // unrendered content is visible as an image.
  if (aDraws.size() < gDraws.size() && !diffDir.empty()) {
    Image g, a, dimg;
    char gname[64], aname[64];
    std::snprintf(gname, sizeof(gname), "/draw_%02zu.ppm", gDraws.size() - 1);
    std::snprintf(aname, sizeof(aname), "/draw_%02zu.ppm", aDraws.size() - 1);
    std::string ioErr;
    if (loadPpm(goldenDir + gname, &g, &ioErr) &&
        loadPpm(framesDir + aname, &a, &ioErr)) {
      DiffStats ds = diffImages(g, a, &dimg);
      std::string path = diffDir + "/final_frame_diff.ppm";
      savePpm(path, dimg, &ioErr);
      out->evidence.push_back(path);
      std::printf(
          "final frame: %zu draw(s) never replayed, %llu px differ -> %s\n",
          gDraws.size() - aDraws.size(), (unsigned long long)ds.diffPixels,
          path.c_str());
    }
  }
  if (!reportPath.empty()) {
    if (!writeReplayReportJson(reportPath, golden, out->rr, out->div,
                               out->evidence, &err)) {
      std::fprintf(stderr, "replay: %s\n", err.c_str());
      return 1;
    }
    out->evidence.push_back(reportPath);
  }
  if (out->div.found) {
    std::printf("FIRST ERRONEOUS STATE at seq %zu:\n", out->div.seq);
    for (const std::string& f : out->div.fields)
      std::printf("  - %s\n", f.c_str());
  } else {
    std::printf("no divergence from golden (%zu ops)\n", out->rr.ops.size());
  }
  return 0;
}

static int cmdReplay(int argc, char** argv) {
  std::string trace = argValue(argc, argv, "--trace");
  std::string golden = argValue(argc, argv, "--golden");
  std::string frames = argValue(argc, argv, "--frames");
  std::string diffs = argValue(argc, argv, "--diffs");
  std::string report = argValue(argc, argv, "--report");
  Renderer renderer;
  std::string err;
  if (!renderer.init(&err)) {
    std::fprintf(stderr, "renderer init: %s\n", err.c_str());
    return 1;
  }
  ReplayOutputs out;
  return runReplay(trace, golden, frames, diffs, report, &renderer, &out);
}

// Full reviewable pipeline:
//   1. record master trace (v2, with checkpoints) + golden state stream
//   2. record legacy trace (v1), convert to v2, replay -> compat proof
//   3. truncate the master mid-record -> damaged artifact
//   4. recover -> salvaged v2 trace + recovery report
//   5. replay recovered vs golden -> first erroneous state + diff images
//   6. write REVIEW.md tying draws, resource states, framebuffers and
//      image diffs together, with all evidence paths.
static int cmdReview(int argc, char** argv) {
  std::string outDir = argValue(argc, argv, "--out", "artifacts/review");
  ensureDir(outDir);
  std::string goldenDir = outDir + "/golden";
  std::string err;

  std::string master = outDir + "/master.rpsc2";
  {
    const char* a[] = {"rpsc", "record", "--out", master.c_str(),
                       "--golden", goldenDir.c_str()};
    if (cmdRecord(6, const_cast<char**>(a)) != 0) return 1;
  }
  std::string legacy = outDir + "/legacy.rpsc1";
  {
    const char* a[] = {"rpsc", "record", "--out", legacy.c_str(),
                       "--format", "v1"};
    if (cmdRecord(6, const_cast<char**>(a)) != 0) return 1;
  }
  std::string legacyConv = outDir + "/legacy_conv.rpsc2";
  {
    const char* a[] = {"rpsc", "convert", "--in", legacy.c_str(),
                       "--out", legacyConv.c_str()};
    if (cmdConvert(6, const_cast<char**>(a)) != 0) return 1;
  }

  // The truncated record: cut inside the BindPipeline record preceding
  // draw #4. In the v2 file, every Draw is followed by a Checkpoint, so
  // scene record 11 sits at file record 11 + 3 checkpoints = 14.
  size_t sceneIdx = sceneTruncationRecordIndex();
  std::vector<Record> scene = buildScene();
  long fileIdx = long(sceneIdx);
  for (size_t i = 0; i < sceneIdx; ++i)
    if (scene[i].type == OpType::Draw) ++fileIdx;
  std::string truncated = outDir + "/truncated.rpsc2";
  {
    std::string k = std::to_string(fileIdx);
    const char* a[] = {"rpsc", "truncate", "--in", master.c_str(),
                       "--out", truncated.c_str(), "--at-record", k.c_str()};
    if (cmdTruncate(8, const_cast<char**>(a)) != 0) return 1;
  }
  std::string recovered = outDir + "/recovered.rpsc2";
  std::string recoveryJson = outDir + "/recovery_report.json";
  {
    const char* a[] = {"rpsc", "recover", "--in", truncated.c_str(),
                       "--out", recovered.c_str(), "--report",
                       recoveryJson.c_str()};
    if (cmdRecover(8, const_cast<char**>(a)) != 0) return 1;
  }
  // The strict reader must reject the truncated file — integrity evidence.
  std::string strictErr;
  {
    Trace tmp;
    if (readTraceStrict(truncated, &tmp, &strictErr)) strictErr.clear();
  }

  Renderer renderer;
  if (!renderer.init(&err)) {
    std::fprintf(stderr, "renderer init: %s\n", err.c_str());
    return 1;
  }
  ReplayOutputs legacyOut, recoveredOut;
  std::string legacyFrames = outDir + "/frames_legacy";
  std::string legacyReport = outDir + "/replay_legacy.json";
  if (runReplay(legacyConv, goldenDir, legacyFrames, "", legacyReport,
                &renderer, &legacyOut) != 0)
    return 1;
  std::string recFrames = outDir + "/frames_recovered";
  std::string recDiffs = outDir + "/diffs";
  std::string recReport = outDir + "/replay_recovered.json";
  if (runReplay(recovered, goldenDir, recFrames, recDiffs, recReport,
                &renderer, &recoveredOut) != 0)
    return 1;

  // Human-readable review document.
  RecoveryReport rep;
  {
    // Re-read recovery facts from the JSON we just wrote is overkill;
    // recoverTrace is cheap and deterministic, so just re-run it in memory.
    std::string tmpErr;
    recoverTrace(truncated, recovered, &rep, &tmpErr);
  }
  FILE* f = std::fopen((outDir + "/REVIEW.md").c_str(), "w");
  if (!f) {
    std::fprintf(stderr, "cannot write REVIEW.md\n");
    return 1;
  }
  std::fprintf(f, "# Render Pipeline State Cache — Review\n\n");
  std::fprintf(f, "## Flow\n\n");
  std::fprintf(f,
               "1. `master.rpsc2` recorded (v2, self-verifying checkpoints)\n"
               "2. `legacy.rpsc1` recorded (old format) and converted to "
               "`legacy_conv.rpsc2`\n"
               "3. `truncated.rpsc2` produced by cutting record %ld "
               "mid-payload\n"
               "4. `recovered.rpsc2` salvaged (%zu records kept)\n"
               "5. Recovered trace replayed against golden state stream\n\n",
               fileIdx, rep.recordsRecovered);
  std::fprintf(f, "## First erroneous state\n\n");
  if (recoveredOut.div.found) {
    const Divergence& d = recoveredOut.div;
    std::fprintf(f, "- **Sequence**: op #%zu\n", d.seq);
    if (d.hasExpected)
      std::fprintf(f, "- **Expected**: %s (pipeline %u, state %s)\n",
                   opName(d.expected.type), d.expected.pipelineId,
                   hex64(d.expected.stateHash).c_str());
    std::fprintf(f, "- **Actual**: %s\n",
                 d.hasActual ? "divergent state (see diffs)"
                             : "missing — stream ends here");
    std::fprintf(f, "- **Field differences**:\n");
    for (const std::string& s : d.fields) std::fprintf(f, "  - %s\n", s.c_str());
    if (rep.firstDefect.found)
      std::fprintf(f,
                   "- **Root cause**: record #%zu (%s) truncated at file "
                   "offset %zu (%zu/%zu bytes present, %s)\n",
                   rep.firstDefect.recordIndex,
                   rep.firstDefect.partialType >= 1
                       ? opName(OpType(rep.firstDefect.partialType))
                       : "?",
                   rep.firstDefect.offset, rep.firstDefect.bytesPresent,
                   rep.firstDefect.bytesExpected,
                   rep.firstDefect.reason.c_str());
  } else {
    std::fprintf(f, "None — replay matches golden.\n");
  }
  if (!strictErr.empty())
    std::fprintf(f, "\nStrict reader rejects the truncated trace: `%s`\n",
                 strictErr.c_str());
  std::fprintf(f, "\n## Compatibility\n\n");
  std::fprintf(f, "- Legacy v1 trace converted and replayed: %s\n",
               legacyOut.div.found ? "DIVERGENCE (see replay_legacy.json)"
                                   : "matches golden (no divergence)");
  std::fprintf(f, "- Recovery output is always v2, single replay path.\n");
  std::fprintf(f, "\n## Pipeline state cache\n\n");
  std::fprintf(f, "- Recovered replay: %u hits, %u misses, %u draws executed\n",
               recoveredOut.rr.cacheHits, recoveredOut.rr.cacheMisses,
               recoveredOut.rr.drawsExecuted);
  std::fprintf(f, "- Checkpoint self-verification: %zu mismatch(es) in the "
                  "salvaged prefix (0 = prefix is bit-exact)\n",
               recoveredOut.rr.checkpointMismatches.size());

  // Correlate draw calls, resource states and framebuffers: one row per
  // golden draw, matched against the recovered replay by ordinal.
  ReplayResult goldenStream;
  std::string gsErr;
  bool gsOk = readStateStreamJson(goldenDir + "/state_stream.json",
                                  &goldenStream, &gsErr);
  if (gsOk) {
    std::vector<const OpSnapshot*> gd, ad;
    for (const OpSnapshot& s : goldenStream.ops)
      if (s.type == OpType::Draw) gd.push_back(&s);
    for (const OpSnapshot& s : recoveredOut.rr.ops)
      if (s.type == OpType::Draw) ad.push_back(&s);
    std::fprintf(f, "\n## Draw call correlation\n\n");
    std::fprintf(f,
                 "| draw | pipeline | image layout | golden fbHash | replay "
                 "fbHash | result |\n");
    std::fprintf(f, "|---|---|---|---|---|---|\n");
    for (size_t k = 0; k < gd.size(); ++k) {
      const OpSnapshot* e = gd[k];
      if (k < ad.size()) {
        const OpSnapshot* a = ad[k];
        bool match = e->fbHash == a->fbHash;
        std::fprintf(f, "| %zu | %u | %u | %s | %s | %s |\n", k,
                     a->pipelineId, a->colorImageLayout,
                     hex64(e->fbHash).c_str(), hex64(a->fbHash).c_str(),
                     match ? "match" : "DIVERGED");
      } else {
        std::fprintf(f, "| %zu | %u | %u | %s | - | MISSING (record lost) |\n",
                     k, e->pipelineId, e->colorImageLayout,
                     hex64(e->fbHash).c_str());
      }
    }
  }
  std::fprintf(f, "\n## Evidence\n\n");
  const char* files[] = {"master.rpsc2",   "legacy.rpsc1",
                         "legacy_conv.rpsc2", "truncated.rpsc2",
                         "recovered.rpsc2", "recovery_report.json",
                         "replay_legacy.json", "replay_recovered.json",
                         "golden/state_stream.json"};
  for (const char* fn : files) std::fprintf(f, "- `%s/%s`\n", outDir.c_str(), fn);
  std::fprintf(f, "- `%s/draw_*.ppm` (golden frames)\n", goldenDir.c_str());
  std::fprintf(f, "- `%s/draw_*.ppm` (recovered replay frames)\n",
               recFrames.c_str());
  for (const std::string& e : recoveredOut.evidence)
    std::fprintf(f, "- `%s`\n", e.c_str());
  std::fclose(f);
  std::printf("\nreview complete -> %s/REVIEW.md\n", outDir.c_str());
  return 0;
}

static int usage() {
  std::fprintf(stderr,
               "usage:\n"
               "  rpsc record --out FILE [--format v1|v2] [--golden DIR]\n"
               "  rpsc convert --in FILE.rpsc1 --out FILE.rpsc2\n"
               "  rpsc truncate --in FILE --out FILE --at-record K\n"
               "  rpsc recover --in FILE --out FILE [--report JSON]\n"
               "  rpsc replay --trace FILE [--golden DIR] [--frames DIR]\n"
               "              [--diffs DIR] [--report JSON]\n"
               "  rpsc review [--out DIR]\n");
  return 2;
}

int main(int argc, char** argv) {
  if (argc < 2) return usage();
  std::string cmd = argv[1];
  if (cmd == "record") return cmdRecord(argc, argv);
  if (cmd == "convert") return cmdConvert(argc, argv);
  if (cmd == "truncate") return cmdTruncate(argc, argv);
  if (cmd == "recover") return cmdRecover(argc, argv);
  if (cmd == "replay") return cmdReplay(argc, argv);
  if (cmd == "review") return cmdReview(argc, argv);
  return usage();
}
