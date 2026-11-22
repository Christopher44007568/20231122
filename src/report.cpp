#include "report.h"

#include <fstream>
#include <map>
#include <sstream>

namespace {
std::string hex64(uint64_t v) {
  char buf[19];
  snprintf(buf, sizeof(buf), "0x%016llx", (unsigned long long)v);
  return buf;
}
}  // namespace

ReviewResult buildReview(const trace::Trace& reference,
                         const trace::Trace& candidate,
                         const std::vector<uint64_t>& replayFb,
                         uint64_t candidateFinalHash,
                         const Image& referenceFinal,
                         const Image& candidateFinal,
                         Image* diffOut) {
  ReviewResult res;
  res.referenceVersion = reference.version;
  res.candidateVersion = candidate.version;
  res.candidateRecovered = candidate.recovered;
  res.truncatedAtIndex = candidate.truncatedAtIndex;
  res.droppedBytes = candidate.droppedBytes;

  std::map<uint32_t, const trace::Record*> candByIndex;
  for (const auto& r : candidate.records) {
    uint32_t i = r.type == trace::RecordType::kDraw ? r.draw.index : r.res.index;
    candByIndex[i] = &r;
  }
  // Replay hashes line up with candidate draw records in order.
  std::map<uint32_t, uint64_t> replayHashByIndex;
  size_t n = 0;
  for (const auto& r : candidate.records)
    if (r.type == trace::RecordType::kDraw && n < replayFb.size())
      replayHashByIndex[r.draw.index] = replayFb[n++];

  bool fbEvidence = reference.version >= trace::kVersionCurrent;

  for (const auto& ref : reference.records) {
    CompareRow row;
    if (ref.type == trace::RecordType::kResourceState) {
      const trace::ResourceStateRecord& s = ref.res;
      row.index = s.index;
      row.kind = "resource";
      row.name = s.name + " " + trace::layoutName(s.oldState) + "->" +
                 trace::layoutName(s.newState);
      auto it = candByIndex.find(s.index);
      if (it == candByIndex.end() ||
          it->second->type != trace::RecordType::kResourceState) {
        row.missing = true;
        row.stateOk = false;
        row.detail = "resource-state record lost";
      } else {
        const trace::ResourceStateRecord& c = it->second->res;
        row.stateOk = c.oldState == s.oldState && c.newState == s.newState &&
                      c.resourceId == s.resourceId;
        if (!row.stateOk)
          row.detail = "transition mismatch: got " +
                       trace::layoutName(c.oldState) + "->" +
                       trace::layoutName(c.newState);
      }
    } else {
      const trace::DrawRecord& d = ref.draw;
      row.index = d.index;
      row.kind = "draw";
      row.name = d.name;
      row.expectedState = d.stateHash;
      row.expectedFb = d.fbHashAfter;
      auto it = candByIndex.find(d.index);
      if (it == candByIndex.end() ||
          it->second->type != trace::RecordType::kDraw) {
        row.missing = true;
        row.stateOk = false;
        row.fbOk = false;
        row.detail = "draw call lost (not present in candidate)";
      } else {
        const trace::DrawRecord& c = it->second->draw;
        row.actualState = c.stateHash;
        row.stateOk = c.stateHash == d.stateHash &&
                      c.pipelineId == d.pipelineId &&
                      c.vertexCount == d.vertexCount &&
                      c.fbTarget == d.fbTarget;
        if (!row.stateOk) row.detail = "GPU state hash mismatch";
        if (fbEvidence) {
          auto h = replayHashByIndex.find(d.index);
          if (h != replayHashByIndex.end()) {
            row.actualFb = h->second;
            row.fbOk = h->second == d.fbHashAfter;
            if (!row.fbOk) row.detail = "framebuffer hash mismatch after draw";
          }
        }
      }
    }
    res.rows.push_back(row);
  }

  for (const auto& row : res.rows) {
    if (row.missing || !row.stateOk || !row.fbOk) {
      res.firstError.found = true;
      res.firstError.index = row.index;
      if (row.missing)
        res.firstError.kind = row.kind == "draw" ? "missing-draw"
                                                 : "missing-resource-state";
      else if (!row.stateOk)
        res.firstError.kind = row.kind == "draw" ? "state-mismatch"
                                                 : "resource-state-mismatch";
      else
        res.firstError.kind = "fb-mismatch";
      std::ostringstream ss;
      ss << "step " << row.index << " [" << row.kind << "] " << row.name
         << ": " << row.detail;
      res.firstError.summary = ss.str();
      break;
    }
  }

  res.referenceFinalHash = reference.finalFbHash;
  res.candidateFinalHash = candidateFinalHash;
  if (diffOut && referenceFinal.width && candidateFinal.width) {
    *diffOut = diffHeatmap(referenceFinal, candidateFinal,
                           &res.finalDiffPixels, &res.finalMaxDiff);
  }
  return res;
}

void writeReportJson(const std::string& path, const ReviewResult& r,
                     const std::vector<std::string>& evidenceFiles) {
  std::ostringstream j;
  j << "{\n";
  j << "  \"format\": {\"reference\": \"v" << r.referenceVersion
    << "\", \"candidate\": \"v" << r.candidateVersion << "\"},\n";
  j << "  \"recovery\": {\"recovered\": "
    << (r.candidateRecovered ? "true" : "false")
    << ", \"truncatedAtIndex\": " << r.truncatedAtIndex
    << ", \"droppedBytes\": " << r.droppedBytes << "},\n";
  j << "  \"firstError\": {";
  if (r.firstError.found) {
    j << "\n    \"found\": true,\n    \"index\": " << r.firstError.index
      << ",\n    \"kind\": \"" << r.firstError.kind << "\",\n"
      << "    \"summary\": \"" << r.firstError.summary << "\"\n  },\n";
  } else {
    j << "\"found\": false},\n";
  }
  j << "  \"finalImage\": {\n"
    << "    \"referenceHash\": \"" << hex64(r.referenceFinalHash) << "\",\n"
    << "    \"candidateHash\": \"" << hex64(r.candidateFinalHash) << "\",\n"
    << "    \"diffPixels\": " << r.finalDiffPixels << ",\n"
    << "    \"maxChannelDiff\": " << r.finalMaxDiff << "\n  },\n";
  j << "  \"records\": [\n";
  for (size_t i = 0; i < r.rows.size(); ++i) {
    const CompareRow& row = r.rows[i];
    j << "    {\"index\": " << row.index << ", \"kind\": \"" << row.kind
      << "\", \"name\": \"" << row.name << "\", "
      << "\"state\": \"" << (row.missing ? "missing"
                              : (row.stateOk && row.fbOk) ? "ok" : "error")
      << "\"";
    if (row.kind == "draw") {
      j << ", \"stateHash\": {\"expected\": \"" << hex64(row.expectedState)
        << "\", \"actual\": \"" << hex64(row.actualState) << "\"}";
      if (row.expectedFb || row.actualFb)
        j << ", \"fbHashAfter\": {\"expected\": \"" << hex64(row.expectedFb)
          << "\", \"actual\": \"" << hex64(row.actualFb) << "\"}";
    }
    if (!row.detail.empty()) j << ", \"detail\": \"" << row.detail << "\"";
    j << "}" << (i + 1 < r.rows.size() ? "," : "") << "\n";
  }
  j << "  ],\n  \"evidence\": [";
  for (size_t i = 0; i < evidenceFiles.size(); ++i) {
    j << (i ? ", " : "") << "\"" << evidenceFiles[i] << "\"";
  }
  j << "]\n}\n";
  std::ofstream out(path);
  if (!out) throw std::runtime_error("cannot write " + path);
  out << j.str();
}
