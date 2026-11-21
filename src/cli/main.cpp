#include "../core/analyzer.hpp"
#include "../core/parser.hpp"
#include "../core/render.hpp"

#include <cstring>
#include <fstream>
#include <iostream>

using namespace gcdump;

namespace {

void printUsage(const char* argv0) {
    std::cerr
        << "usage: " << argv0
        << " [--format text|markdown] [--summary-only] <dump-file>\n"
        << "exit codes: 0 ok (manual checks may remain), 2 blocking failure, "
           "3 usage/IO error\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string path;
    ReportFormat format = ReportFormat::PlainText;
    bool summaryOnly = false;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--format") == 0 && i + 1 < argc) {
            std::string v = argv[++i];
            format = (v == "markdown") ? ReportFormat::Markdown
                                       : ReportFormat::PlainText;
        } else if (std::strcmp(argv[i], "--summary-only") == 0) {
            summaryOnly = true;
        } else if (argv[i][0] == '-') {
            printUsage(argv[0]);
            return 3;
        } else {
            path = argv[i];
        }
    }
    if (path.empty()) {
        printUsage(argv[0]);
        return 3;
    }

    std::ifstream file(path);
    if (!file) {
        std::cerr << "cannot open dump file: " << path << "\n";
        return 3;
    }
    ParseResult parsed = parseDump(file);
    Report report = analyze(std::move(parsed), path);

    if (summaryOnly)
        std::cout << renderSummaryLine(report) << "\n";
    else
        std::cout << renderReport(report, format);

    return report.failures.empty() ? 0 : 2;
}
