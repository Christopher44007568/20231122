#pragma once

#include "analyzer.h"

#include <string>
#include <vector>

// Prints a human-readable summary table for one run.
void printSummary(const RunSummary& s);

// Writes machine-readable reports (JSON per run + a combined CSV).
bool writeJsonReport(const RunSummary& s, const std::string& path);
bool writeCsvSummary(const std::vector<RunSummary>& runs,
                     const std::string& path);
