#pragma once

#include "core/replay.hpp"

#include <string>

namespace vfcr {

std::string defaultStateDirectory();
std::string defaultStatePath();
bool saveSessionFile(const std::string& path, Session& session, std::string* error = nullptr);
bool loadSessionFile(const std::string& path, Session& session, std::string* error = nullptr);
bool validateObjectFieldOrder(const Json& objectDocument, std::string* error = nullptr);
std::string sessionContentDigest(const Session& session);

}  // namespace vfcr
