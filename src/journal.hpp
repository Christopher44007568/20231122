#pragma once

#include "common.hpp"

#include <fstream>
#include <string>
#include <vector>

namespace inst {

class Journal {
public:
    explicit Journal(const fs::path& path);

    void writeStep(const std::string& step,
                   const std::string& input,
                   const std::string& stateBefore,
                   const std::string& result,
                   const std::string& stateAfter,
                   bool ok,
                   const std::string& error,
                   const std::string& check = "{}");

    uint64_t nextSequence() const { return sequence_; }

private:
    std::ofstream out_;
    uint64_t sequence_ = 0;
};

std::vector<std::string> readJournalLines(const fs::path& path);

bool extractString(const std::string& line, const std::string& key,
                   std::string& value);
bool extractInt(const std::string& line, const std::string& key,
                int64_t& value);
bool extractBool(const std::string& line, const std::string& key, bool& value);

} // namespace inst
