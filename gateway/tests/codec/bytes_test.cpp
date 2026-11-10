#include "ugvc/codec/bytes.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>

using namespace ugvc::codec;

// ADR-002: 다바이트 필드는 big-endian (상위 바이트가 먼저)
TEST(Bytes, REQ_QUAL_001_write_is_big_endian) {
  Bytes out;
  write_u16_be(out, 0x1234);
  write_u32_be(out, 0x89ABCDEF);
  write_u64_be(out, 0x0102030405060708ULL);
  const Bytes expected = {0x12, 0x34, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x02,
                          0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_EQ(out, expected);
}

TEST(Bytes, REQ_QUAL_001_read_is_big_endian) {
  const std::uint8_t in[] = {0x12, 0x34, 0x89, 0xAB, 0xCD, 0xEF, 0x01, 0x02,
                             0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
  EXPECT_EQ(read_u16_be(in), 0x1234);
  EXPECT_EQ(read_u32_be(in + 2), 0x89ABCDEFU);
  EXPECT_EQ(read_u64_be(in + 6), 0x0102030405060708ULL);
}

// 위도·경도(i32) 음수가 2의 보수로 왕복되는지 (ICD 6.1 범위 양 끝)
TEST(Bytes, REQ_QUAL_001_i32_negative_roundtrip) {
  for (std::int32_t v : {-1800000000, -1, 0, 1800000000,
                         std::numeric_limits<std::int32_t>::min(),
                         std::numeric_limits<std::int32_t>::max()}) {
    Bytes out;
    write_i32_be(out, v);
    ASSERT_EQ(out.size(), 4U);
    EXPECT_EQ(read_i32_be(out.data()), v) << "value " << v;
  }
  Bytes minus_one;
  write_i32_be(minus_one, -1);
  EXPECT_EQ(minus_one, (Bytes{0xFF, 0xFF, 0xFF, 0xFF}));
}
