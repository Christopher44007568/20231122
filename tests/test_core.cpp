#include "checksum.hpp"
#include "image.hpp"
#include "json.hpp"
#include "model.hpp"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace {

int failures = 0;

#define CHECK(condition)                                                        \
    do {                                                                        \
        if (!(condition)) {                                                     \
            std::cerr << "FAIL " << __FILE__ << ':' << __LINE__ << "  "       \
                      << #condition << '\n';                                    \
            ++failures;                                                         \
        }                                                                       \
    } while (false)

void testJson() {
    rr::JsonValue value;
    rr::JsonError error;
    rr::JsonParser parser;
    CHECK(parser.parse("{\"a\":[1,2,3],\"b\":\"\\u4f60\\u597d\",\"c\":true}",
                       value, error));
    CHECK(value.isObject());
    CHECK(value.find("a") != nullptr);
    CHECK(value.find("a")->asArray().size() == 3U);
    CHECK(value.find("b")->asString() == "\xE4\xBD\xA0\xE5\xA5\xBD");
    CHECK(value.find("c")->asBool());
    CHECK(!parser.parse("{\"a\":1,}", value, error));
    CHECK(!parser.parse("01", value, error));
    CHECK(!parser.parse("[1,]", value, error));
    CHECK(!parser.parse("1e9999", value, error));
    CHECK(rr::jsonEscape("a\n\"b") == "\"a\\n\\\"b\"");
}

void testChecksum() {
    CHECK(rr::sha256Hex("") ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(rr::sha256Hex("abc") ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

void testEmptyInput() {
    rr::LoadResult result = rr::loadScenario("");
    CHECK(result.ok());
    CHECK(result.scenario.empty_input);
    CHECK(result.scenario.passes.empty());
    CHECK(result.scenario.width > 0U);
}

void testModelValidation() {
    const std::string valid = R"({
      "schema":"raster-regression/v1",
      "name":"unit",
      "framebuffer":{"width":8,"height":8},
      "vertices":[
        {"position":[-1,-1,0,1],"rgba":[255,0,0,255]},
        {"position":[1,-1,0,1],"rgba":[255,0,0,255]},
        {"position":[0,1,0,1],"rgba":[255,0,0,255]}
      ],
      "indices":[0,1,2],
      "rules":[{"id":"r","cull_mode":"none"}],
      "draws":[{"id":"d","first_index":0,"index_count":3}],
      "passes":[{"id":"p","rule":"r","draws":["d"]}],
      "shared_edges":[{"id":"e","draw_a":"d","draw_b":"d","a":[0,0],"b":[1,1]}],
      "reference_pixels":[{"pass":"p","x":4,"y":4,"rgba":[255,0,0,255]}]
    })";
    rr::LoadResult result = rr::loadScenario(valid);
    CHECK(result.ok());
    CHECK(result.scenario.rules.size() == 1U);

    result = rr::loadScenario(
        R"({"rules":[{"id":"r"},{"id":"r"}]})");
    CHECK(!result.ok());

    result = rr::loadScenario(R"({"framebuffer":{"width":0,"height":8}})");
    CHECK(!result.ok());
    result = rr::loadScenario(
        R"({"vertices":[{"position":[1]}],"indices":[0],"draws":[],"passes":[]})");
    CHECK(!result.ok());
    result = rr::loadScenario(R"({
      "vertices":[{"position":[0,0,0,1]}],
      "indices":[0],
      "rules":[{"id":"r"}],
      "draws":[{"id":"d","first_index":0,"index_count":1}],
      "passes":[{"id":"p","rule":"r","draws":["d"]}],
      "reference_pixels":[{"pass":"p","x":99,"y":0,"rgba":[0,0,0,255]}]
    })");
    CHECK(!result.ok());
    result = rr::loadScenario(R"({
      "vertices":[{"position":[0,0,0,1]}],
      "indices":[0],
      "rules":[{"id":"r"}],
      "draws":[{"id":"d","first_index":0,"index_count":1}],
      "passes":[{"id":"p","rule":"r","draws":["d"]},
                {"id":"p2","rule":"r","draws":["d"]}],
      "boundary":{"parameter":"line_width","before_pass":"p","after_pass":"p","threshold":1}
    })");
    CHECK(!result.ok());
}

void testImageDiff() {
    rr::Image before;
    before.width = 2;
    before.height = 1;
    before.rgba = {0, 0, 0, 255, 10, 10, 10, 255};
    rr::Image after = before;
    after.rgba[4] = 13;
    rr::DiffStats stats = rr::compareImages(before, after);
    CHECK(stats.comparable);
    CHECK(stats.changedPixels == 1U);
    CHECK(stats.maxChannelDelta == 3U);
    CHECK(stats.firstDiffX == 1U);
    CHECK(stats.firstDiffY == 0U);
    CHECK(stats.box.minX == 1U && stats.box.maxX == 1U);
    const rr::ReferenceCheck check =
        rr::checkReference(after, 1, 0, {10, 10, 10, 255}, 3);
    CHECK(check.passCheck);
    CHECK(check.maxDelta == 3U);
}

void testPpm() {
    const auto directory =
        std::filesystem::temp_directory_path() / "rr-test-ppm";
    const auto path = directory / "frame.ppm";
    rr::Image image;
    image.width = 1;
    image.height = 1;
    image.rgba = {1, 2, 3, 255};
    CHECK(rr::writePpm(path.string(), image));
    CHECK(std::filesystem::exists(path));
    CHECK(std::filesystem::file_size(path) == 14U);
    std::filesystem::remove_all(directory);
}

}  // namespace

int main() {
    testJson();
    testChecksum();
    testEmptyInput();
    testModelValidation();
    testImageDiff();
    testPpm();
    if (failures != 0) {
        std::cerr << failures << " test(s) failed\n";
        return 1;
    }
    std::cout << "all core tests passed\n";
    return 0;
}
