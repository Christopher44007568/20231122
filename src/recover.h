#pragma once
// Recovery pass for damaged RPSC traces.
//
// Strategy: validate the header, then walk records with CRC/length checks
// (v2) or fixed-size alignment (v1). The salvageable prefix is rewritten as
// a well-formed v2 file with a corrected record count, so downstream replay
// always consumes a valid trace. The first defect is reported precisely
// (offset, record index, partial type, present/expected bytes).
#include "trace.h"

namespace rpsc {

struct RecoveryReport {
  std::string inputPath;
  std::string outputPath;
  uint32_t detectedVersion = 0;
  size_t fileSize = 0;
  size_t recordsDeclared = 0;   // header count (v2), 0 when unknown
  size_t recordsRecovered = 0;
  size_t bytesConsumed = 0;     // bytes that made it into the recovered file
  Defect firstDefect;
  bool convertedToV2 = false;   // legacy input upgraded during recovery
  bool clean = false;           // input had no defect at all
};

// Recovers `inputPath` into `outputPath` (always v2). Fails only on
// unrecoverable header damage or I/O errors.
bool recoverTrace(const std::string& inputPath, const std::string& outputPath,
                  RecoveryReport* report, std::string* err);

}  // namespace rpsc
