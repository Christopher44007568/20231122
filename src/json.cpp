#include "json.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace vegd {

std::string utcNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &seconds);
#else
    gmtime_r(&seconds, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

bool writeJsonFile(const std::string& path, const Json& value,
                   std::string& error) {
    std::error_code ec;
    const std::filesystem::path target(path);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
        if (ec) {
            error = "cannot create output directory: " + ec.message();
            return false;
        }
    }
    const std::filesystem::path temporary = target.string() + ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            error = "cannot open output file: " + path;
            return false;
        }
        stream << value.dump(2) << '\n';
        stream.flush();
        if (!stream) {
            error = "cannot write output file: " + path;
            return false;
        }
    }
    std::filesystem::rename(temporary, target, ec);
    if (ec) {
        error = "cannot finalize output file: " + ec.message();
        return false;
    }
    return true;
}

bool readJsonFile(const std::string& path, Json& value, std::string& error) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        error = "cannot open JSON file: " + path;
        return false;
    }
    try {
        stream >> value;
    } catch (const std::exception& exception) {
        error = std::string("invalid JSON: ") + exception.what();
        return false;
    }
    return true;
}

Json sampleJson(const BoundarySample& sample) {
    Json out = Json::object();
    if (!sample.valid) {
        out["valid"] = false;
        return out;
    }
    out["valid"] = true;
    out["tile"] = sample.tile;
    out["acceptedCount"] = sample.acceptedCount;
    out["firstPos"] = Json::array(
        {sample.firstPos.x, sample.firstPos.y, sample.firstPos.z});
    out["tileHash"] = hex64(sample.tileHash);
    return out;
}

Json degradationsJson(const std::vector<std::string>& degradations) {
    Json out = Json::array();
    for (const std::string& degradation : degradations) {
        out.push_back(degradation);
    }
    return out;
}

}  // namespace vegd
