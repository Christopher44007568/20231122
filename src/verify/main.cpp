#include "core/export.hpp"

#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "usage: vfcr-verify <record.json>\n";
        return 2;
    }
    const vfcr::VerifyResult result = vfcr::verifyExportFile(argv[1]);
    std::cout << "VFC REPLAY / INDEPENDENT RECORD REVIEW\n";
    std::cout << "artifact: " << argv[1] << "\n";
    std::cout << "record_sha256: "
              << (result.recordSha256.empty() ? "(unavailable)" : result.recordSha256) << "\n";
    std::cout << "evidence_head: "
              << (result.evidenceHead.empty() ? "(unavailable)" : result.evidenceHead) << "\n";
    std::cout << "checks:\n";
    for (const vfcr::VerifyCheck& check : result.checks) {
        std::cout << "  [" << (check.passed ? "PASS" : "FAIL") << "] " << check.id
                  << " - " << check.detail << "\n";
    }
    if (!result.ok) {
        std::cout << "RESULT: REJECTED (" << result.failures.size() << " failure(s))\n";
        return 1;
    }
    std::cout << "RESULT: VERIFIED\n";
    return 0;
}
