#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace tcpv {

std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& data);
bool writeFileBytes(const std::string& path, const std::vector<uint8_t>& data);
bool fileExists(const std::string& path);
void makeDirs(const std::string& path);

std::string sha256Hex(const uint8_t* data, size_t len);
std::string sha256File(const std::string& path);

std::string jsonEscape(const std::string& s);
std::string nowIso8601();
std::string formatDouble(double v, int precision = 4);

} // namespace tcpv
