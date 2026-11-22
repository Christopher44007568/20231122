#pragma once

#include "audit_types.h"

#include <string>

namespace audit {

struct ReportPaths {
  std::string jsonPath;
  std::string htmlPath;
  std::string rawInputPath;
};

std::string buildRawInputLedger(const AuditBundle& bundle);
std::string buildJsonReport(const AuditBundle& bundle);
std::string buildHtmlReport(const AuditBundle& bundle);
ReportPaths writeReports(const AuditBundle& bundle,
                         const std::string& outputDir);

}  // namespace audit
