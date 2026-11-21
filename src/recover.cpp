#include "recover.h"

#include <cstdio>

namespace rpsc {

bool recoverTrace(const std::string& inputPath, const std::string& outputPath,
                  RecoveryReport* report, std::string* err) {
  FILE* f = std::fopen(inputPath.c_str(), "rb");
  if (!f) {
    *err = "cannot open: " + inputPath;
    return false;
  }
  std::fseek(f, 0, SEEK_END);
  long n = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> bytes(static_cast<size_t>(n));
  bool readOk = std::fread(bytes.data(), 1, bytes.size(), f) == bytes.size();
  std::fclose(f);
  if (!readOk) {
    *err = "short read: " + inputPath;
    return false;
  }

  report->inputPath = inputPath;
  report->outputPath = outputPath;
  report->fileSize = bytes.size();
  if (bytes.size() >= 8) {
    report->detectedVersion =
        uint32_t(bytes[4]) | (uint32_t(bytes[5]) << 8) |
        (uint32_t(bytes[6]) << 16) | (uint32_t(bytes[7]) << 24);
  }
  if (bytes.size() >= 12 && report->detectedVersion == kVersionCurrent) {
    const uint8_t* p = bytes.data() + 8;
    report->recordsDeclared = uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
                              (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
  }

  Trace salvaged;
  salvaged.version = kVersionCurrent;
  uint32_t version = 0;
  std::string walkErr;
  bool clean = walkRecords(bytes, &version, &report->firstDefect,
                           [&](const Record& r) {
                             salvaged.records.push_back(r);
                             return true;
                           },
                           &walkErr);
  if (report->firstDefect.found &&
      (report->firstDefect.reason == "BadMagic" ||
       report->firstDefect.reason == "BadVersion" ||
       report->firstDefect.reason == "HeaderCrcMismatch" ||
       report->firstDefect.reason == "TruncatedHeader(file header)")) {
    *err = "unrecoverable header: " + report->firstDefect.reason;
    return false;
  }
  report->clean = clean;
  report->convertedToV2 = (version == kVersionLegacy);
  report->recordsRecovered = salvaged.records.size();
  report->bytesConsumed = report->firstDefect.found ? report->firstDefect.offset
                                                    : bytes.size();

  if (!writeTrace(outputPath, salvaged, err)) return false;
  return true;
}

}  // namespace rpsc
