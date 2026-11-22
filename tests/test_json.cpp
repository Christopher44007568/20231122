#include "camtrak/json.hpp"
#include "test_framework.hpp"

using camtrak::JsonParseError;
using camtrak::JsonValue;
using camtrak::parseJson;

TEST_CASE(json_roundtrip_preserves_member_order) {
  const std::string text =
      "{\n"
      "  \"zebra\": 1,\n"
      "  \"apple\": \"two\",\n"
      "  \"mango\": [3, 4],\n"
      "  \"nested\": {\"b\": true, \"a\": false}\n"
      "}\n";
  JsonParseError error;
  JsonValue value = parseJson(text, &error);
  CHECK(value.isObject());
  const auto& keys = value.keys();
  CHECK(keys.size() == 4);
  CHECK(keys[0] == "zebra");
  CHECK(keys[1] == "apple");
  CHECK(keys[2] == "mango");
  CHECK(keys[3] == "nested");
  CHECK(value.get("nested").keys()[0] == "b");

  const std::string redumped = value.dump();
  JsonValue reparsed = parseJson(redumped, &error);
  CHECK(reparsed.keys() == keys);
  CHECK(reparsed.dump() == redumped);
}

TEST_CASE(json_rejects_duplicate_keys) {
  JsonParseError error;
  JsonValue value = parseJson("{\"a\":1,\"a\":2}", &error);
  CHECK(value.isNull());
  CHECK(!error.message.empty());
}

TEST_CASE(json_set_existing_key_keeps_position) {
  JsonValue value = JsonValue::makeObject();
  value.set("first", JsonValue::makeNumber(1));
  value.set("second", JsonValue::makeNumber(2));
  value.set("first", JsonValue::makeNumber(9));
  CHECK(value.keys().size() == 2);
  CHECK(value.keys()[0] == "first");
  CHECK(value.get("first").asNumber() == 9);
}

TEST_CASE(json_handles_primitives_and_unicode) {
  JsonParseError error;
  JsonValue value =
      parseJson("[null, true, false, -1.5, 1e3, \"\\u00e9\"]", &error);
  CHECK(value.isArray());
  CHECK(value.size() == 6);
  CHECK(value.at(0).isNull());
  CHECK(value.at(1).asBool());
  CHECK(!value.at(2).asBool());
  CHECK(camtrak_test::near(value.at(3).asNumber(), -1.5));
  CHECK(camtrak_test::near(value.at(4).asNumber(), 1000.0));
  CHECK(value.at(5).asString() == "\xc3\xa9");
}

TEST_CASE(json_reports_error_position) {
  JsonParseError error;
  parseJson("{\n  \"a\": ,\n}", &error);
  CHECK(error.line == 2);
  CHECK(error.column >= 7);
}
