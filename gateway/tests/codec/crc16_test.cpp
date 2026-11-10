#include "ugvc/codec/crc16.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

using ugvc::codec::crc16_ccitt_false;

namespace {

std::uint16_t crc_of(const char* ascii) {
  return crc16_ccitt_false(reinterpret_cast<const std::uint8_t*>(ascii), std::strlen(ascii));
}

}  // namespace

// TC-COM-002-02: CRC-16/CCITT-FALSE 표준 검사 벡터
TEST(Crc16, REQ_COM_002_standard_vector) {
  EXPECT_EQ(crc_of("123456789"), 0x29B1);
}

// 길이 0이면 초기값 그대로 (경계 확인용, TC 보조)
TEST(Crc16, REQ_COM_002_empty_input_is_init) {
  EXPECT_EQ(crc16_ccitt_false(nullptr, 0), 0xFFFF);
}

// 1비트만 달라도 CRC가 달라짐 (TC-COM-002-07의 전제)
TEST(Crc16, REQ_COM_002_single_bit_flip_changes_crc) {
  std::uint8_t a[] = {0xAA, 0x55, 0x01, 0x01};
  std::uint8_t b[] = {0xAA, 0x55, 0x01, 0x00};
  EXPECT_NE(crc16_ccitt_false(a, sizeof a), crc16_ccitt_false(b, sizeof b));
}
