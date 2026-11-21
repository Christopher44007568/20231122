#pragma once

#include "core/replay.hpp"

#include <string>
#include <vector>

namespace vfcr {

struct ExportOptions {
    std::string directory;
    std::string screenshotFile = "frame.ppm";
    std::string screenshotSha256;
    int screenshotWidth = 0;
    int screenshotHeight = 0;
    std::string focusObjectId;
};

struct VerifyCheck {
    std::string id;
    bool passed = false;
    std::string detail;
};

struct VerifyResult {
    bool ok = false;
    std::string recordSha256;
    std::string evidenceHead;
    std::vector<VerifyCheck> checks;
    std::vector<std::string> failures;
};

Json buildExportRecord(const Session& session, const ExportOptions& options);
bool writeExportBundle(Session& session, const ExportOptions& options, std::string* recordPath,
                       std::string* error = nullptr);
bool loadExportRecord(const std::string& path, Session& session, std::string* error = nullptr);
VerifyResult verifyExportDocument(const Json& document, const std::string& baseDirectory = {});
VerifyResult verifyExportFile(const std::string& path);

}  // namespace vfcr
