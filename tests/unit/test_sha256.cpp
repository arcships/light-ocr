#include <cstdint>
#include <string>

#include "test.hpp"
#include "util/sha256.hpp"

using light_ocr::internal::sha256_hex;

LIGHT_OCR_TEST(sha256_empty_vector) {
  const std::string value;
  EXPECT_EQ(sha256_hex(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

LIGHT_OCR_TEST(sha256_abc_vector) {
  const std::string value = "abc";
  EXPECT_EQ(sha256_hex(reinterpret_cast<const std::uint8_t*>(value.data()), value.size()),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}
