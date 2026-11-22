#include "evidence.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace vegd {

bool appendEvidence(const std::string& path, const std::string& event,
                    const Json& fields, std::string& error) {
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "cannot create evidence directory: " + ec.message();
            return false;
        }
    }
    Json line = Json::object();
    line["event"] = event;
    line["ts"] = utcNow();
    for (auto iterator = fields.begin(); iterator != fields.end(); ++iterator) {
        line[iterator.key()] = iterator.value();
    }
    std::ofstream stream(path, std::ios::binary | std::ios::app);
    if (!stream) {
        error = "cannot open evidence log: " + path;
        return false;
    }
    stream << line.dump() << '\n';
    stream.flush();
    if (!stream) {
        error = "cannot append evidence log: " + path;
        return false;
    }
    return true;
}

bool resetEvidence(const std::string& path, std::string& error) {
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        error = "cannot reset evidence log: " + path;
        return false;
    }
    return true;
}

}  // namespace vegd
