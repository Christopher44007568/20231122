#include "camtrak/sha256.hpp"
#include "test_framework.hpp"

using camtrak::sha256Hex;

TEST_CASE(sha256_known_vectors) {
  CHECK(sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  const std::string long_input(1000, 'a');
  CHECK(sha256Hex(long_input) ==
        "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3");
}

TEST_CASE(sha256_streaming_matches_oneshot) {
  camtrak::Sha256 hasher;
  const std::string input = "the quick brown fox jumps over the lazy dog";
  for (char ch : input) {
    std::uint8_t byte = static_cast<std::uint8_t>(ch);
    hasher.update(&byte, 1);
  }
  CHECK(camtrak::toHex(hasher.digest()) == sha256Hex(input));
}

TEST_CASE(sha256_multiblock_message) {
  const std::string input =
      "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  CHECK(sha256Hex(input) ==
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
