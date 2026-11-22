#include "json.hpp"
#include "sha256.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << "\n";
    std::exit(1);
  }
}

std::string fieldOrder(const kfc::Json& object) {
  std::string result;
  for (const auto& item : object.asObject()) {
    result += item.first;
    result.push_back(',');
  }
  return result;
}

}  // namespace

int main() {
  const std::string source =
      R"({"z":1,"a":{"y":2,"b":3},"m":[1,2],"unicode":"\u4e2d\u6587"})";
  std::string error;
  const kfc::Json parsed = kfc::Json::parse(source, &error);
  require(error.empty(), "ordered JSON parses");
  require(fieldOrder(parsed) == "z,a,m,unicode,", "top-level field order");
  require(fieldOrder(parsed.at("a")) == "y,b,", "nested field order");

  const std::string dumped = parsed.dump(2);
  const kfc::Json reloaded = kfc::Json::parse(dumped, &error);
  require(error.empty(), "dump parses again");
  require(reloaded.dump(2) == dumped, "dump is stable after reload");
  require(fieldOrder(reloaded) == fieldOrder(parsed),
          "reload preserves top-level order");
  require(parsed.at("unicode").asString() == "\xe4\xb8\xad\xe6\x96\x87",
          "unicode escape decodes to UTF-8");

  require(kfc::Sha256::of("abc") ==
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256 known vector");

  std::cout << "ordered JSON and SHA-256 tests passed\n";
  return 0;
}
